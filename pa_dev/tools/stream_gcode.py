"""Stream a G-code file to a Prusa (Marlin / Buddy) printer over serial.

The flow-control model is the classic Marlin one: send one command, wait
for the printer to reply with `ok`, then send the next. We treat `Error:`
as fatal (print stops, port is closed, non-zero exit). Temperature reports
(`T:...`) and busy pings (`echo:busy`) are logged but do not count as acks.

Usage:
    python pa_dev/tools/stream_gcode.py <port> <gcode-file> [--baud 115200] \
        [--log pa_dev/runs/stream_<ts>.log] [--resume-at N]

Output (stdout, one event per line, prefix-tagged so you can tail -f easily):

    STATE port=COM5 file=... total_lines=897
    TX 1/897   G90
    RX        << ok
    TX 2/897   M83
    ...
    PROGRESS 50/897 (5.6%)  ETA 18m12s  T:214.3/215.0  B:59.8/60.0
    DONE sent=897 time=0:18:33
"""

from __future__ import annotations

import argparse
import re
import sys
import time

# M573 echoes °C as UTF-8 (0xC2 0xB0). Reconfigure stdout to UTF-8 so
# those characters survive the print() call on Windows (default cp1252).
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from datetime import datetime, timedelta
from pathlib import Path
from typing import Optional

import serial  # pyserial

RE_OK = re.compile(r"^\s*ok\b", re.IGNORECASE)
RE_ERR = re.compile(r"^\s*(Error|!!)", re.IGNORECASE)
RE_BUSY = re.compile(r"^\s*echo:\s*busy", re.IGNORECASE)
RE_TEMP = re.compile(
    r"T:(?P<t>-?\d+\.\d+)/(?P<t_tgt>-?\d+\.\d+)\s+B:(?P<b>-?\d+\.\d+)/(?P<b_tgt>-?\d+\.\d+)"
)


def should_send(line: str) -> bool:
    """Skip blanks and pure-comment lines — saves serial round-trips."""
    stripped = line.strip()
    if not stripped:
        return False
    if stripped.startswith(";"):
        return False
    return True


def read_line_blocking(ser: serial.Serial, timeout_s: float) -> Optional[str]:
    """Read one CR-LF-terminated line with a wall-clock timeout.

    Returns None if timeout expires without a newline. Returns empty string
    on EOF-like conditions (port still open, just quiet).
    """
    buf = bytearray()
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        ch = ser.read(1)
        if not ch:
            if buf:
                # Incomplete line — keep waiting.
                continue
            continue
        if ch in (b"\n", b"\r"):
            if buf:
                return buf.decode("utf-8", errors="replace")
            continue
        buf += ch
    if buf:
        return buf.decode("utf-8", errors="replace")
    return None


def fmt_eta(sec: float) -> str:
    if sec < 0 or sec != sec:  # nan guard
        return "?"
    d = timedelta(seconds=int(sec))
    return str(d)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("gcode")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--log", default=None)
    ap.add_argument("--resume-at", type=int, default=1,
                    help="Start streaming at 1-based line index (skip prior lines)")
    ap.add_argument("--ack-timeout", type=float, default=120.0,
                    help="Seconds to wait for 'ok' after each line (long moves + M190/M109)")
    ap.add_argument("--quiet-rx", action="store_true",
                    help="Don't echo every RX line to stdout (still logged to --log)")
    args = ap.parse_args()

    gpath = Path(args.gcode)
    if not gpath.is_file():
        print(f"STREAM_ERR file not found: {gpath}", flush=True)
        return 2

    # Load lines up front — the file is small (<< 1 MB) and this lets us
    # report a total and progress percentage cleanly.
    raw_lines = gpath.read_text(encoding="utf-8", errors="replace").splitlines()
    sendable_idx = [i for i, l in enumerate(raw_lines) if should_send(l)]
    total = len(sendable_idx)

    log_path = Path(args.log) if args.log else (
        Path(__file__).resolve().parents[1] / "runs" /
        f"stream_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    logf = open(log_path, "w", buffering=1, encoding="utf-8", newline="\n")

    def logln(s: str, echo: bool = True) -> None:
        logf.write(s + "\n")
        if echo:
            print(s, flush=True)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except serial.SerialException as exc:
        logln(f"STREAM_ERR open {args.port}: {exc}")
        return 1

    logln(f"STATE port={args.port} file={gpath.name} total_lines={total} log={log_path}")
    # Small settle — some USB CDC stacks drop the first bytes after open.
    time.sleep(0.5)
    ser.reset_input_buffer()

    # Handshake: Marlin will emit 'ok' or temp lines on its own cadence. We
    # send M115 (firmware info) to force a reliable 'ok'.
    logln("TX 0/0    M115  (handshake)")
    ser.write(b"M115\n")
    ser.flush()
    deadline = time.monotonic() + 10.0
    hs_partial_ok = False
    while time.monotonic() < deadline:
        ln = read_line_blocking(ser, 1.0)
        if ln is None:
            continue
        logln(f"RX        << {ln}", echo=not args.quiet_rx)
        ln_s = ln.strip()
        if ln_s == "o":
            hs_partial_ok = True
            continue
        if ln_s == "k" and hs_partial_ok:
            ln = "ok"
        hs_partial_ok = False
        if RE_OK.match(ln):
            break
        if RE_ERR.match(ln):
            logln(f"STREAM_ERR handshake error: {ln}")
            ser.close()
            return 1
    else:
        logln("STREAM_ERR handshake timed out")
        ser.close()
        return 1

    start_t = time.monotonic()
    last_progress_report_t = 0.0
    last_temp = ""

    try:
        for sent_idx, raw_idx in enumerate(sendable_idx, start=1):
            if sent_idx < args.resume_at:
                continue
            line = raw_lines[raw_idx].strip()
            # Strip trailing inline comments — they still send fine but we
            # reduce noise in the log.
            cmd = line.split(";", 1)[0].rstrip()
            if not cmd:
                continue

            logln(f"TX {sent_idx}/{total}   {cmd}")
            ser.write((cmd + "\n").encode("ascii", errors="replace"))
            ser.flush()

            # Wait for 'ok'. M190/M109 can take minutes — the ack-timeout
            # applies to each line individually, so keep it generous.
            ack_deadline = time.monotonic() + args.ack_timeout
            got_ok = False
            pending_partial_ok = False  # Prusa sometimes sends "o\r\n" then "k\r\n"
            while time.monotonic() < ack_deadline:
                rx = read_line_blocking(ser, 2.0)
                if rx is None:
                    # Periodic progress heartbeat while waiting on slow cmds.
                    now = time.monotonic()
                    if now - last_progress_report_t >= 5.0:
                        elapsed = now - start_t
                        rate = (sent_idx - args.resume_at + 1) / max(elapsed, 1e-6)
                        eta = (total - sent_idx) / max(rate, 1e-6)
                        logln(
                            f"PROGRESS {sent_idx}/{total} "
                            f"({100.0 * sent_idx / max(total, 1):.1f}%) "
                            f"ETA {fmt_eta(eta)}  {last_temp}"
                        )
                        last_progress_report_t = now
                    continue
                logln(f"RX        << {rx}", echo=not args.quiet_rx)
                m = RE_TEMP.search(rx)
                if m:
                    last_temp = f"T:{m.group('t')}/{m.group('t_tgt')}  B:{m.group('b')}/{m.group('b_tgt')}"
                # Handle split ok: firmware sometimes sends "o\r\n" then "k\r\n"
                # as two separate serial lines instead of a single "ok\r\n".
                rx_stripped = rx.strip()
                if rx_stripped == "o":
                    pending_partial_ok = True
                    continue
                if rx_stripped == "k" and pending_partial_ok:
                    rx = "ok"
                pending_partial_ok = False
                if RE_OK.match(rx):
                    got_ok = True
                    break
                if RE_ERR.match(rx):
                    logln(f"STREAM_ERR printer error: {rx}")
                    return 1
                if RE_BUSY.match(rx):
                    # Extend deadline a bit — busy means it's still alive.
                    ack_deadline = time.monotonic() + args.ack_timeout
            if not got_ok:
                logln(f"STREAM_ERR ack timeout at line {sent_idx}: {cmd}")
                return 1

            # Throttled progress report every 25 lines.
            if sent_idx % 25 == 0:
                now = time.monotonic()
                elapsed = now - start_t
                rate = (sent_idx - args.resume_at + 1) / max(elapsed, 1e-6)
                eta = (total - sent_idx) / max(rate, 1e-6)
                logln(
                    f"PROGRESS {sent_idx}/{total} "
                    f"({100.0 * sent_idx / max(total, 1):.1f}%) "
                    f"ETA {fmt_eta(eta)}  {last_temp}"
                )
                last_progress_report_t = now

        elapsed = time.monotonic() - start_t
        logln(f"DONE sent={total} time={fmt_eta(elapsed)}")
        return 0
    finally:
        try:
            ser.close()
        except Exception:
            pass
        logf.close()


if __name__ == "__main__":
    sys.exit(main())
