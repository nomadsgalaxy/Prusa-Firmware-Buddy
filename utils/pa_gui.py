"""Minimal Tk GUI for PA development work on a USB-connected Prusa printer.

Features:
- Serial connect/disconnect (port + baud configurable).
- Live console with coloured TX/RX lines; command entry; quick-action buttons.
- Auto-detects BEGIN PA_CAPTURE ... END PA_CAPTURE blocks, saves each capture
  to a timestamped .log file in the repo root, and redraws the load-vs-time
  plot in the right pane with phase-marker overlays.
- Status bar with connection state and parsed hotend/bed temperatures.

Depends on pyserial + matplotlib + tkinter (stdlib).

Usage:
    python utils/pa_gui.py [--port COM5] [--baud 115200]
"""

from __future__ import annotations

import argparse
import queue
import re
import sys
import threading
import time
import tkinter as tk
import tkinter.filedialog as filedialog
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from tkinter import ttk
from typing import Callable, Optional

import serial  # pyserial
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.figure import Figure

REPO_ROOT = Path(__file__).resolve().parent.parent
# All PA development artefacts live under pa_dev/. Captures go in captures/
# so they don't pile up at the repo root.
PA_DEV_DIR = REPO_ROOT / "pa_dev"
CAPTURE_DIR = PA_DEV_DIR / "captures"
CAPTURE_DIR.mkdir(parents=True, exist_ok=True)

# --- Parsing -----------------------------------------------------------------

TEMP_LINE_RE = re.compile(
    r"T:(?P<t>-?\d+\.\d+)/(?P<t_tgt>-?\d+\.\d+)\s+B:(?P<b>-?\d+\.\d+)/(?P<b_tgt>-?\d+\.\d+)"
)
SAMPLE_RE = re.compile(r"^PA,(?P<ts>\d+),(?P<load>-?\d+(?:\.\d+)?)$")
PHASE_RE = re.compile(r"^PA_PHASE,(?P<name>[^,]+),(?P<ts>\d+)$")

# Commanded filament feed rate [mm/s] for each M573 phase.
# Mirrors the kPhases table in
# lib/Marlin/.../gcode/feature/pressure_advance/M573.cpp — keep in sync if
# that table changes. Rates during "start" / "end" book-ends are 0 (nothing
# is being extruded at the marker itself).
PHASE_FEEDRATE_MM_S: dict[str, float] = {
    "start": 0.0,
    "slow_baseline": 5.0,
    "fast_step": 13.333,
    "slow_decay": 5.0,
    "end": 0.0,
}

# ----- PA-test G-code parser ------------------------------------------------

# Matches Marlin G1 moves (optionally with leading G0).
RE_GCODE_MOVE = re.compile(r"^\s*G[01]\b(?P<args>.*?)(?:;.*)?$", re.IGNORECASE)
RE_GCODE_M900 = re.compile(r"^\s*M900\b(?P<args>.*?)(?:;.*)?$", re.IGNORECASE)
RE_GCODE_G90 = re.compile(r"^\s*G90\b", re.IGNORECASE)
RE_GCODE_G91 = re.compile(r"^\s*G91\b", re.IGNORECASE)
RE_GCODE_M82 = re.compile(r"^\s*M82\b", re.IGNORECASE)
RE_GCODE_M83 = re.compile(r"^\s*M83\b", re.IGNORECASE)
RE_GCODE_G92 = re.compile(r"^\s*G92\b(?P<args>.*?)(?:;.*)?$", re.IGNORECASE)
RE_PARAM = re.compile(r"(?P<key>[XYZEKF])\s*(?P<val>-?\d+(?:\.\d+)?)", re.IGNORECASE)


def parse_patest_gcode(text: str) -> list[tuple[float, float, float, float, float]]:
    """Return list of extruding segments: (x0, y0, x1, y1, k_factor).

    Only records G1 moves that both translate in XY and extrude (positive E
    delta). Tracks absolute/relative mode for XYZ and E separately (G90/G91
    and M82/M83, Marlin's per-axis convention). G92 resets without moving.
    """
    abs_xyz = True
    abs_e = True
    x = y = z = e = 0.0
    k = 0.0
    segments: list[tuple[float, float, float, float, float]] = []

    for raw in text.splitlines():
        if RE_GCODE_G90.match(raw):
            abs_xyz = True
            continue
        if RE_GCODE_G91.match(raw):
            abs_xyz = False
            continue
        if RE_GCODE_M82.match(raw):
            abs_e = True
            continue
        if RE_GCODE_M83.match(raw):
            abs_e = False
            continue

        m92 = RE_GCODE_G92.match(raw)
        if m92:
            for pm in RE_PARAM.finditer(m92.group("args")):
                v = float(pm.group("val"))
                key = pm.group("key").upper()
                if key == "X": x = v
                elif key == "Y": y = v
                elif key == "Z": z = v
                elif key == "E": e = v
            continue

        m900 = RE_GCODE_M900.match(raw)
        if m900:
            for pm in RE_PARAM.finditer(m900.group("args")):
                if pm.group("key").upper() == "K":
                    k = float(pm.group("val"))
            continue

        mv = RE_GCODE_MOVE.match(raw)
        if not mv:
            continue
        nx, ny, nz, ne = x, y, z, e
        for pm in RE_PARAM.finditer(mv.group("args")):
            v = float(pm.group("val"))
            key = pm.group("key").upper()
            if key == "X": nx = v if abs_xyz else x + v
            elif key == "Y": ny = v if abs_xyz else y + v
            elif key == "Z": nz = v if abs_xyz else z + v
            elif key == "E": ne = v if abs_e else e + v
        de = ne - e
        dx = nx - x
        dy = ny - y
        if de > 1e-6 and (abs(dx) > 1e-6 or abs(dy) > 1e-6):
            segments.append((x, y, nx, ny, k))
        x, y, z, e = nx, ny, nz, ne
    return segments


@dataclass
class Capture:
    """A single BEGIN/END PA_CAPTURE block collected from the serial stream."""

    t_us: list[int] = field(default_factory=list)
    load_g: list[float] = field(default_factory=list)
    phases: list[tuple[str, int]] = field(default_factory=list)
    sample_count: Optional[int] = None
    dropped_count: Optional[int] = None
    started_at: datetime = field(default_factory=datetime.now)
    raw_lines: list[str] = field(default_factory=list)


# --- Serial backend ----------------------------------------------------------


class SerialClient:
    """pyserial wrapper with a reader thread that emits one callback per line."""

    def __init__(self, on_line: Callable[[str], None]) -> None:
        self._on_line = on_line
        self._port: Optional[serial.Serial] = None
        self._reader: Optional[threading.Thread] = None
        self._stop = threading.Event()

    @property
    def is_open(self) -> bool:
        return self._port is not None and self._port.is_open

    def open(self, port: str, baud: int) -> None:
        if self.is_open:
            raise RuntimeError("already open")
        self._port = serial.Serial(port, baud, timeout=0.2)
        self._stop.clear()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def close(self) -> None:
        self._stop.set()
        p = self._port
        self._port = None
        if p is not None:
            try:
                p.close()
            except Exception:
                pass

    def send(self, line: str) -> None:
        if not self.is_open:
            raise RuntimeError("port not open")
        assert self._port is not None
        self._port.write((line + "\n").encode("ascii", errors="replace"))
        self._port.flush()

    def _read_loop(self) -> None:
        buf = b""
        while not self._stop.is_set() and self._port is not None:
            try:
                chunk = self._port.read(512)
            except serial.SerialException:
                break
            if not chunk:
                continue
            buf += chunk
            while True:
                nl = buf.find(b"\n")
                if nl < 0:
                    break
                line = buf[:nl].rstrip(b"\r").decode("ascii", errors="replace")
                buf = buf[nl + 1:]
                try:
                    self._on_line(line)
                except Exception:
                    # Never let a UI bug take down the reader thread.
                    pass


# --- GUI ---------------------------------------------------------------------


class App:
    def __init__(self, root: tk.Tk, port: str, baud: int) -> None:
        self.root = root
        self.root.title("Prusa PA dev")
        self.root.geometry("1280x760")

        # Thread-safe queue from reader thread to Tk main loop.
        self._inbox: queue.Queue[str] = queue.Queue()

        # Serial state.
        self.port_var = tk.StringVar(value=port)
        self.baud_var = tk.StringVar(value=str(baud))
        self.conn_var = tk.StringVar(value="disconnected")
        self.temp_var = tk.StringVar(value="T: --  B: --")
        self.capture_var = tk.StringVar(value="capture: idle")

        self.serial = SerialClient(on_line=self._on_rx_line)

        # Capture state: accumulates when we're inside a BEGIN/END block.
        self._cap: Optional[Capture] = None
        self._last_capture: Optional[Capture] = None
        # Live-update throttling: avoid redrawing on every sample at ~320 Hz.
        self._cap_last_redraw_t: float = 0.0
        self._cap_last_drawn_count: int = 0
        self._LIVE_REDRAW_MIN_INTERVAL_S: float = 0.15
        self._LIVE_REDRAW_MIN_NEW_SAMPLES: int = 16

        self._build_layout()
        self._tick()

        # Best-effort: load the most recent capture in pa_dev/captures/ so the
        # user sees a plot immediately on startup.
        default = self._latest_capture_file()
        if default is not None:
            try:
                self._load_capture_from_file(default)
                self._log(f"[ui] loaded {default.name}", "sys")
            except Exception as exc:
                self._log(f"[ui] could not auto-load {default.name}: {exc}", "err")

    # ---- Layout

    def _build_layout(self) -> None:
        self.root.columnconfigure(0, weight=1)
        self.root.columnconfigure(1, weight=2)
        self.root.rowconfigure(2, weight=1)

        # Row 0: connection bar
        bar = ttk.Frame(self.root, padding=(8, 6))
        bar.grid(row=0, column=0, columnspan=2, sticky="ew")
        ttk.Label(bar, text="Port:").pack(side="left")
        ttk.Entry(bar, textvariable=self.port_var, width=8).pack(side="left", padx=4)
        ttk.Label(bar, text="Baud:").pack(side="left")
        ttk.Entry(bar, textvariable=self.baud_var, width=8).pack(side="left", padx=4)
        self.connect_btn = ttk.Button(bar, text="Connect", command=self._toggle_connect)
        self.connect_btn.pack(side="left", padx=(8, 4))
        ttk.Label(bar, textvariable=self.conn_var, foreground="#555").pack(side="left", padx=8)
        ttk.Label(bar, textvariable=self.temp_var).pack(side="right", padx=8)

        # Row 1: quick actions
        qa = ttk.Frame(self.root, padding=(8, 0))
        qa.grid(row=1, column=0, columnspan=2, sticky="ew")
        quick = [
            ("Home (G28)", "G28"),
            ("Preheat 215", "M104 S215"),
            ("Cool", "M104 S0"),
            ("Safe pos", "G1 X10 Y5 Z5 F3000"),
            ("Temp report on", "M155 S2"),
            ("Temp report off", "M155 S0"),
            ("Run M573", "M573"),
        ]
        for (label, cmd) in quick:
            ttk.Button(qa, text=label, command=lambda c=cmd: self._send_cmd(c)).pack(
                side="left", padx=2
            )
        ttk.Button(qa, text="Clear log", command=self._clear_log).pack(side="right", padx=2)

        # Row 2 left: console
        console_frame = ttk.Frame(self.root, padding=(8, 4))
        console_frame.grid(row=2, column=0, sticky="nsew")
        console_frame.rowconfigure(0, weight=1)
        console_frame.columnconfigure(0, weight=1)
        self.console = tk.Text(
            console_frame,
            wrap="none",
            font=("Consolas", 10),
            bg="#111",
            fg="#ddd",
            insertbackground="#fff",
            state="disabled",
            height=20,
        )
        self.console.grid(row=0, column=0, sticky="nsew")
        vsb = ttk.Scrollbar(console_frame, orient="vertical", command=self.console.yview)
        vsb.grid(row=0, column=1, sticky="ns")
        self.console.configure(yscrollcommand=vsb.set)

        # Text tags for colouring.
        self.console.tag_configure("tx", foreground="#7ab6ff")
        self.console.tag_configure("rx", foreground="#e6e6e6")
        self.console.tag_configure("sys", foreground="#a0e0a0")
        self.console.tag_configure("err", foreground="#ff8080")
        self.console.tag_configure("cap", foreground="#ffd180")

        # Row 3 left: command entry
        entry_frame = ttk.Frame(self.root, padding=(8, 4))
        entry_frame.grid(row=3, column=0, sticky="ew")
        entry_frame.columnconfigure(0, weight=1)
        self.cmd_var = tk.StringVar()
        cmd_entry = ttk.Entry(entry_frame, textvariable=self.cmd_var)
        cmd_entry.grid(row=0, column=0, sticky="ew", padx=(0, 4))
        cmd_entry.bind("<Return>", lambda e: self._send_from_entry())
        ttk.Button(entry_frame, text="Send", command=self._send_from_entry).grid(row=0, column=1)

        # Row 2-3 right: notebook with three tabs.
        nb = ttk.Notebook(self.root)
        nb.grid(row=2, column=1, rowspan=2, sticky="nsew", padx=8, pady=4)
        cap_tab = ttk.Frame(nb)
        rate_tab = ttk.Frame(nb)
        pat_tab = ttk.Frame(nb)
        nb.add(cap_tab, text="Load vs time")
        nb.add(rate_tab, text="Load vs extrusion rate")
        nb.add(pat_tab, text="PA test preview")
        self._build_capture_tab(cap_tab)
        self._build_rate_tab(rate_tab)
        self._build_patest_tab(pat_tab)

        # Row 4: status bar
        status = ttk.Frame(self.root, padding=(8, 2))
        status.grid(row=4, column=0, columnspan=2, sticky="ew")
        ttk.Label(status, text="RX lines coloured; TX commands prefixed '>>'.", foreground="#888").pack(side="left")

    # ---- Tabs

    def _build_capture_tab(self, parent: ttk.Frame) -> None:
        """Tab 1: load (g) vs time (s) — raw signal + MA(33) overlay + phase vlines."""
        parent.rowconfigure(0, weight=1)
        parent.columnconfigure(0, weight=1)
        self.fig = Figure(figsize=(6, 4), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self._render_empty_plot(self.ax, "Load vs time (no capture yet)", "time [s]")
        self.canvas = FigureCanvasTkAgg(self.fig, master=parent)
        self.canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")
        toolbar_frame = ttk.Frame(parent)
        toolbar_frame.grid(row=1, column=0, sticky="ew")
        NavigationToolbar2Tk(self.canvas, toolbar_frame)
        ctrl = ttk.Frame(parent, padding=(4, 4))
        ctrl.grid(row=2, column=0, sticky="ew")
        ttk.Button(ctrl, text="Load capture...", command=self._load_capture_dialog).pack(
            side="left", padx=2
        )
        ttk.Button(ctrl, text="Save PNG...", command=self._save_png).pack(side="left", padx=2)
        ttk.Label(ctrl, textvariable=self.capture_var).pack(side="right", padx=6)

    def _build_rate_tab(self, parent: ttk.Frame) -> None:
        """Tab 2: load (g) vs commanded filament feed rate (mm/s), coloured by phase."""
        parent.rowconfigure(0, weight=1)
        parent.columnconfigure(0, weight=1)
        self.rate_fig = Figure(figsize=(6, 4), dpi=100)
        self.rate_ax = self.rate_fig.add_subplot(111)
        self._render_empty_plot(
            self.rate_ax,
            "Load vs extrusion rate (no capture yet)",
            "filament feed rate [mm/s]",
        )
        self.rate_canvas = FigureCanvasTkAgg(self.rate_fig, master=parent)
        self.rate_canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")
        tb = ttk.Frame(parent)
        tb.grid(row=1, column=0, sticky="ew")
        NavigationToolbar2Tk(self.rate_canvas, tb)
        ctrl = ttk.Frame(parent, padding=(4, 4))
        ctrl.grid(row=2, column=0, sticky="ew")
        ttk.Label(
            ctrl,
            text=(
                "Rate derived from the phase each sample falls in (see "
                "PHASE_FEEDRATE_MM_S in pa_gui.py). Transition samples are mis-"
                "assigned while phase markers still use queue-time — known firmware bug."
            ),
            foreground="#888",
            wraplength=560,
            justify="left",
        ).pack(side="left", padx=4)

    def _build_patest_tab(self, parent: ttk.Frame) -> None:
        """Tab 3: load a translated Marlin G-code PA test and render extruding segments, coloured by K."""
        parent.rowconfigure(0, weight=1)
        parent.columnconfigure(0, weight=1)
        self.pa_fig = Figure(figsize=(6, 4), dpi=100)
        self.pa_ax = self.pa_fig.add_subplot(111)
        self._render_empty_patest_plot()
        self.pa_canvas = FigureCanvasTkAgg(self.pa_fig, master=parent)
        self.pa_canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")
        tb = ttk.Frame(parent)
        tb.grid(row=1, column=0, sticky="ew")
        NavigationToolbar2Tk(self.pa_canvas, tb)
        ctrl = ttk.Frame(parent, padding=(4, 4))
        ctrl.grid(row=2, column=0, sticky="ew")
        self.patest_var = tk.StringVar(value="no PA test loaded")
        ttk.Button(
            ctrl, text="Load Marlin PA test G-code...", command=self._load_patest_dialog
        ).pack(side="left", padx=2)
        ttk.Label(ctrl, textvariable=self.patest_var, foreground="#888").pack(
            side="right", padx=6
        )

    # ---- Console I/O

    def _log(self, text: str, tag: str = "rx") -> None:
        self.console.configure(state="normal")
        self.console.insert("end", text + "\n", tag)
        self.console.see("end")
        self.console.configure(state="disabled")

    def _clear_log(self) -> None:
        self.console.configure(state="normal")
        self.console.delete("1.0", "end")
        self.console.configure(state="disabled")

    # ---- Serial control

    def _toggle_connect(self) -> None:
        if self.serial.is_open:
            self.serial.close()
            self.conn_var.set("disconnected")
            self.connect_btn.configure(text="Connect")
            self._log("[ui] disconnected", "sys")
            return
        port = self.port_var.get().strip()
        try:
            baud = int(self.baud_var.get())
        except ValueError:
            self._log("[ui] bad baud", "err")
            return
        try:
            self.serial.open(port, baud)
        except serial.SerialException as exc:
            self._log(f"[ui] open failed: {exc}", "err")
            return
        self.conn_var.set(f"connected {port}@{baud}")
        self.connect_btn.configure(text="Disconnect")
        self._log(f"[ui] opened {port}@{baud}", "sys")

    def _send_cmd(self, cmd: str) -> None:
        if not self.serial.is_open:
            self._log("[ui] not connected", "err")
            return
        try:
            self.serial.send(cmd)
        except Exception as exc:
            self._log(f"[ui] send failed: {exc}", "err")
            return
        self._log(f">> {cmd}", "tx")

    def _send_from_entry(self) -> None:
        cmd = self.cmd_var.get().strip()
        if not cmd:
            return
        self._send_cmd(cmd)
        self.cmd_var.set("")

    # ---- RX plumbing

    def _on_rx_line(self, line: str) -> None:
        # Called from the reader thread — just enqueue.
        self._inbox.put(line)

    def _tick(self) -> None:
        # Drain inbox on the Tk main thread.
        drained = 0
        while drained < 500:
            try:
                line = self._inbox.get_nowait()
            except queue.Empty:
                break
            drained += 1
            self._handle_line(line)
        # Live-update: while a capture is in progress, redraw the plots on a
        # throttle so the user sees samples stream in. We skip if no new
        # samples have arrived or we redrew too recently.
        cap = self._cap
        if cap is not None and cap.t_us:
            n = len(cap.t_us)
            now = time.monotonic()
            new_samples = n - self._cap_last_drawn_count
            elapsed = now - self._cap_last_redraw_t
            if (
                new_samples >= self._LIVE_REDRAW_MIN_NEW_SAMPLES
                and elapsed >= self._LIVE_REDRAW_MIN_INTERVAL_S
            ):
                self._cap_last_drawn_count = n
                self._cap_last_redraw_t = now
                try:
                    self._redraw_time(cap)
                    self._redraw_rate(cap)
                    self.capture_var.set(f"capture: collecting... ({n} samples)")
                except Exception as exc:
                    # Never let a plotting hiccup bring down the UI loop.
                    self._log(f"[ui] live redraw error: {exc}", "err")
        self.root.after(40, self._tick)

    def _handle_line(self, line: str) -> None:
        tag = "rx"
        # Update temp HUD.
        m = TEMP_LINE_RE.search(line)
        if m:
            self.temp_var.set(
                f"T: {m.group('t')}/{m.group('t_tgt')}  B: {m.group('b')}/{m.group('b_tgt')}"
            )

        # Capture state machine.
        if "BEGIN PA_CAPTURE" in line:
            self._cap = Capture()
            self._cap_last_drawn_count = 0
            self._cap_last_redraw_t = 0.0
            self.capture_var.set("capture: collecting...")
            tag = "cap"
        elif self._cap is not None:
            m_samp = SAMPLE_RE.match(line)
            m_phase = PHASE_RE.match(line)
            if line.startswith("PA_SAMPLES="):
                # format: "PA_SAMPLES=1536 PA_DROPPED=207"
                parts = dict(
                    p.split("=", 1) for p in line.split() if "=" in p
                )
                self._cap.sample_count = int(parts.get("PA_SAMPLES", 0))
                self._cap.dropped_count = int(parts.get("PA_DROPPED", 0))
                tag = "cap"
            elif m_phase:
                self._cap.phases.append((m_phase.group("name"), int(m_phase.group("ts"))))
                tag = "cap"
            elif m_samp:
                self._cap.t_us.append(int(m_samp.group("ts")))
                self._cap.load_g.append(float(m_samp.group("load")))
                # Don't log every sample — would drown the console.
                return
            elif "END PA_CAPTURE" in line:
                self._finalise_capture()
                tag = "cap"
            self._cap.raw_lines.append(line)

        self._log("<< " + line, tag)

    def _finalise_capture(self) -> None:
        cap = self._cap
        self._cap = None
        if cap is None:
            return
        self._last_capture = cap
        # Persist to disk so the raw run is never lost.
        stamp = cap.started_at.strftime("%Y%m%d_%H%M%S")
        out = CAPTURE_DIR / f"pa_capture_{stamp}.log"
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write("BEGIN PA_CAPTURE\n")
            if cap.sample_count is not None:
                f.write(f"PA_SAMPLES={cap.sample_count} PA_DROPPED={cap.dropped_count}\n")
            for name, ts in cap.phases:
                f.write(f"PA_PHASE,{name},{ts}\n")
            for t, v in zip(cap.t_us, cap.load_g):
                f.write(f"PA,{t},{v}\n")
            f.write("END PA_CAPTURE\n")
        self._log(
            f"[ui] saved capture: {out.name}  ({len(cap.t_us)} samples, {cap.dropped_count or 0} dropped)",
            "sys",
        )
        self.capture_var.set(
            f"capture: {out.name}  ({len(cap.t_us)} samples)"
        )
        self._redraw(cap)

    # ---- Plot

    def _render_empty_plot(self, ax, title: str, xlabel: str) -> None:
        ax.clear()
        ax.set_title(title)
        ax.set_xlabel(xlabel)
        ax.set_ylabel("load [g]")
        ax.grid(True, alpha=0.3)

    def _render_empty_patest_plot(self) -> None:
        self.pa_ax.clear()
        self.pa_ax.set_title("PA test preview (no G-code loaded)")
        self.pa_ax.set_xlabel("X [mm]")
        self.pa_ax.set_ylabel("Y [mm]")
        self.pa_ax.set_aspect("equal", adjustable="datalim")
        self.pa_ax.grid(True, alpha=0.3)

    def _redraw(self, cap: Capture) -> None:
        """Repaint both the load-vs-time and load-vs-rate tabs."""
        self._redraw_time(cap)
        self._redraw_rate(cap)

    def _redraw_time(self, cap: Capture) -> None:
        self.ax.clear()
        if not cap.t_us:
            self._render_empty_plot(self.ax, "Load vs time (no capture yet)", "time [s]")
            self.canvas.draw_idle()
            return

        t0 = cap.t_us[0]
        xs = [(t - t0) / 1e6 for t in cap.t_us]
        ys = cap.load_g

        self.ax.plot(xs, ys, linewidth=0.7, color="#1a73e8", alpha=0.8, label="raw")
        # Moving-average overlay to make the low-frequency shape visible.
        if len(ys) >= 33:
            win = 33
            smooth = []
            acc = 0.0
            for i, v in enumerate(ys):
                acc += v
                if i >= win:
                    acc -= ys[i - win]
                denom = min(i + 1, win)
                smooth.append(acc / denom)
            self.ax.plot(xs, smooth, linewidth=1.6, color="#e8711a", label=f"MA({win})")

        # Phase markers (note: queue-time, not execute-time — known bug).
        colors = ["#777", "#888", "#999", "#aaa", "#666"]
        for (name, ts), c in zip(cap.phases, colors):
            x = (ts - t0) / 1e6
            self.ax.axvline(x, color=c, linestyle="--", linewidth=0.8)
            self.ax.text(x, self.ax.get_ylim()[1] * 0.95 if self.ax.get_ylim()[1] != 0 else 0, name,
                         rotation=90, fontsize=7, color=c, ha="right", va="top")

        title_bits = ["Load vs time"]
        if cap.sample_count is not None:
            title_bits.append(f"{cap.sample_count} samples, {cap.dropped_count} dropped")
        self.ax.set_title("  ·  ".join(title_bits))
        self.ax.set_xlabel("time [s] (relative to first sample)")
        self.ax.set_ylabel("load [g]")
        self.ax.grid(True, alpha=0.3)
        self.ax.legend(loc="upper left", fontsize=8)
        self.canvas.draw_idle()

    def _redraw_rate(self, cap: Capture) -> None:
        """Scatter load vs commanded filament feed rate, coloured by phase."""
        self.rate_ax.clear()
        if not cap.t_us or not cap.phases:
            self._render_empty_plot(
                self.rate_ax,
                "Load vs extrusion rate (no capture yet)",
                "filament feed rate [mm/s]",
            )
            self.rate_canvas.draw_idle()
            return

        # Phases are in timestamp order; walk samples and assign each to the
        # most recent phase marker at or before its timestamp.
        phase_order = sorted(cap.phases, key=lambda p: p[1])
        per_phase_x: dict[str, list[float]] = {}
        per_phase_y: dict[str, list[float]] = {}
        pi = 0
        current = "start"
        for ts, load in zip(cap.t_us, cap.load_g):
            while pi < len(phase_order) and phase_order[pi][1] <= ts:
                current = phase_order[pi][0]
                pi += 1
            rate = PHASE_FEEDRATE_MM_S.get(current, 0.0)
            per_phase_x.setdefault(current, []).append(rate)
            per_phase_y.setdefault(current, []).append(load)

        phase_colors = {
            "start": "#888",
            "slow_baseline": "#1a73e8",
            "fast_step": "#e8711a",
            "slow_decay": "#2ca02c",
            "end": "#888",
        }
        for name in ("slow_baseline", "fast_step", "slow_decay", "start", "end"):
            if name not in per_phase_x:
                continue
            self.rate_ax.scatter(
                per_phase_x[name],
                per_phase_y[name],
                s=6,
                alpha=0.35,
                color=phase_colors.get(name, "#444"),
                label=name,
            )
            # Phase-mean marker.
            if per_phase_y[name]:
                mean_rate = sum(per_phase_x[name]) / len(per_phase_x[name])
                mean_load = sum(per_phase_y[name]) / len(per_phase_y[name])
                self.rate_ax.scatter(
                    [mean_rate], [mean_load],
                    s=80, marker="x", color=phase_colors.get(name, "#444"),
                )

        title_bits = ["Load vs extrusion rate"]
        if cap.sample_count is not None:
            title_bits.append(f"{cap.sample_count} samples")
        self.rate_ax.set_title("  ·  ".join(title_bits))
        self.rate_ax.set_xlabel("filament feed rate [mm/s]")
        self.rate_ax.set_ylabel("load [g]")
        self.rate_ax.grid(True, alpha=0.3)
        self.rate_ax.legend(loc="upper left", fontsize=8)
        self.rate_canvas.draw_idle()

    # ---- PA test preview

    def _load_patest_dialog(self) -> None:
        path = filedialog.askopenfilename(
            title="Load Marlin PA test G-code",
            initialdir=str(PA_DEV_DIR / "tools"),
            filetypes=[("G-code", "*.gcode *.gco *.g"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
            segs = parse_patest_gcode(text)
            self._redraw_patest(Path(path).name, segs)
            self._log(f"[ui] PA test loaded {Path(path).name}: {len(segs)} extruding segments", "sys")
        except Exception as exc:
            self._log(f"[ui] PA test load failed: {exc}", "err")

    def _redraw_patest(
        self, name: str, segments: list[tuple[float, float, float, float, float]]
    ) -> None:
        self.pa_ax.clear()
        if not segments:
            self._render_empty_patest_plot()
            self.patest_var.set(f"{name}: no extruding segments")
            self.pa_canvas.draw_idle()
            return

        import matplotlib.cm as cm
        from matplotlib.collections import LineCollection
        from matplotlib.colors import Normalize

        ks = [s[4] for s in segments]
        kmin, kmax = min(ks), max(ks)
        norm = Normalize(vmin=kmin, vmax=max(kmax, kmin + 1e-9))
        cmap = cm.get_cmap("viridis")
        lines = [[(s[0], s[1]), (s[2], s[3])] for s in segments]
        colors = [cmap(norm(k)) for k in ks]
        lc = LineCollection(lines, colors=colors, linewidths=1.2)
        self.pa_ax.add_collection(lc)

        xs = [x for s in segments for x in (s[0], s[2])]
        ys = [y for s in segments for y in (s[1], s[3])]
        pad = 5.0
        self.pa_ax.set_xlim(min(xs) - pad, max(xs) + pad)
        self.pa_ax.set_ylim(min(ys) - pad, max(ys) + pad)
        self.pa_ax.set_aspect("equal", adjustable="datalim")
        self.pa_ax.set_xlabel("X [mm]")
        self.pa_ax.set_ylabel("Y [mm]")
        self.pa_ax.grid(True, alpha=0.3)

        sm = cm.ScalarMappable(norm=norm, cmap=cmap)
        sm.set_array([])
        # Reuse or create a colorbar. Matplotlib lacks a simple "update cbar";
        # easiest is to remove any existing one and make a new one.
        if getattr(self, "_pa_cbar", None) is not None:
            try:
                self._pa_cbar.remove()
            except Exception:
                pass
        self._pa_cbar = self.pa_fig.colorbar(sm, ax=self.pa_ax)
        self._pa_cbar.set_label("M900 K-factor")

        self.pa_ax.set_title(f"{name}  ·  {len(segments)} segments  ·  K ∈ [{kmin:g}, {kmax:g}]")
        self.patest_var.set(f"{name}: {len(segments)} segments, K ∈ [{kmin:g}, {kmax:g}]")
        self.pa_canvas.draw_idle()

    def _latest_capture_file(self) -> Optional[Path]:
        """Return the newest pa_capture_*.log in CAPTURE_DIR, or None."""
        if not CAPTURE_DIR.exists():
            return None
        candidates = sorted(CAPTURE_DIR.glob("pa_capture_*.log"))
        return candidates[-1] if candidates else None

    def _load_capture_dialog(self) -> None:
        path = filedialog.askopenfilename(
            title="Load PA capture",
            initialdir=str(CAPTURE_DIR),
            filetypes=[("PA capture log", "*.log"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            self._load_capture_from_file(Path(path))
            self._log(f"[ui] loaded {Path(path).name}", "sys")
        except Exception as exc:
            self._log(f"[ui] load failed: {exc}", "err")

    def _load_capture_from_file(self, path: Path) -> None:
        cap = Capture(started_at=datetime.fromtimestamp(path.stat().st_mtime))
        with open(path, "r", encoding="utf-8") as f:
            for raw in f:
                line = raw.rstrip("\r\n")
                if line.startswith("PA_SAMPLES="):
                    parts = dict(p.split("=", 1) for p in line.split() if "=" in p)
                    cap.sample_count = int(parts.get("PA_SAMPLES", 0))
                    cap.dropped_count = int(parts.get("PA_DROPPED", 0))
                elif m := PHASE_RE.match(line):
                    cap.phases.append((m.group("name"), int(m.group("ts"))))
                elif m := SAMPLE_RE.match(line):
                    cap.t_us.append(int(m.group("ts")))
                    cap.load_g.append(float(m.group("load")))
        self._last_capture = cap
        self.capture_var.set(f"capture: {path.name}  ({len(cap.t_us)} samples)")
        self._redraw(cap)

    def _save_png(self) -> None:
        if self._last_capture is None or not self._last_capture.t_us:
            self._log("[ui] nothing to save — no capture loaded", "err")
            return
        stamp = self._last_capture.started_at.strftime("%Y%m%d_%H%M%S")
        default = f"pa_capture_{stamp}.png"
        path = filedialog.asksaveasfilename(
            title="Save plot as PNG",
            initialdir=str(CAPTURE_DIR),
            initialfile=default,
            defaultextension=".png",
            filetypes=[("PNG", "*.png")],
        )
        if not path:
            return
        self.fig.savefig(path, dpi=140, bbox_inches="tight")
        self._log(f"[ui] saved plot: {Path(path).name}", "sys")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="COM5")
    p.add_argument("--baud", type=int, default=115200)
    args = p.parse_args()

    root = tk.Tk()
    try:
        app = App(root, port=args.port, baud=args.baud)
    except Exception as exc:
        print(f"app init failed: {exc}", file=sys.stderr)
        return 1

    def on_close() -> None:
        try:
            app.serial.close()
        except Exception:
            pass
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_close)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
