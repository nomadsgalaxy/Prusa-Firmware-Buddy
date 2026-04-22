"""One-shot M573 capture over USB-serial for a Prusa printer.

Sends a short preflight sequence, then M573, and records every line the
printer emits until we either see `END PA_CAPTURE` or hit the timeout.

Usage:
    python utils/pa_capture.py COM5 pa_dev/captures/pa_capture_001.log
    python utils/pa_capture.py COM5 pa_dev/captures/pa_capture_001.log --preflight=none
    python utils/pa_capture.py COM5 pa_dev/captures/pa_capture_001.log --gcode=M115

All PA dev artefacts (captures, preflights, notes) live under pa_dev/.

The preflight default just asks for firmware identification (M115) so we can
confirm we're actually talking to the printer. It does *not* heat — you must
preheat via the printer's LCD before running M573 (M573 aborts with 'hotend
too cold to extrude' otherwise).
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import serial  # pyserial

BAUD = 115200
# Default total/idle limits. Override per-run via CLI. M573 itself runs fast
# (~5 s mechanical + ~0.5 s CSV dump), but anything that blocks on heating
# (M109/M190) or homing needs more headroom.
DEFAULT_TOTAL_TIMEOUT_S = 45.0
DEFAULT_IDLE_TIMEOUT_S = 8.0
# Phrase that signals the CSV block is complete.
END_MARKER = "END PA_CAPTURE"


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("port", help="COM port, e.g. COM5")
    p.add_argument("output", help="output log file path")
    p.add_argument("--gcode", default="M573", help="G-code to run (default: M573)")
    p.add_argument(
        "--preflight",
        default="m115",
        help="comma-separated preflight commands, or 'none' (default: M115)",
    )
    p.add_argument(
        "--preflight-file",
        default=None,
        help="path to a file with one G-code command per line (overrides --preflight); blank/# lines ignored",
    )
    p.add_argument(
        "--baud", type=int, default=BAUD, help=f"baud rate (default {BAUD})"
    )
    p.add_argument(
        "--total-timeout",
        type=float,
        default=DEFAULT_TOTAL_TIMEOUT_S,
        help=f"max seconds to wait overall (default {DEFAULT_TOTAL_TIMEOUT_S})",
    )
    p.add_argument(
        "--idle-timeout",
        type=float,
        default=DEFAULT_IDLE_TIMEOUT_S,
        help=f"max seconds of silence before giving up (default {DEFAULT_IDLE_TIMEOUT_S})",
    )
    args = p.parse_args()

    out_path = Path(args.output)

    preflight: list[str]
    if args.preflight_file:
        with open(args.preflight_file, "r", encoding="utf-8") as pf:
            preflight = [
                line.strip()
                for line in pf
                if line.strip() and not line.strip().startswith("#")
            ]
    elif args.preflight.lower() == "none":
        preflight = []
    else:
        preflight = [c.strip() for c in args.preflight.split(",") if c.strip()]

    commands = preflight + [args.gcode]

    print(f"[pa_capture] port={args.port} baud={args.baud} output={out_path}")
    print(f"[pa_capture] commands: {commands}")

    try:
        s = serial.Serial(args.port, args.baud, timeout=0.2)
    except serial.SerialException as exc:
        print(f"[pa_capture] ERROR opening {args.port}: {exc}")
        return 1

    with s, open(out_path, "w", encoding="utf-8", newline="\n") as f:
        # A short settle/drain so we flush any boot chatter already buffered
        # in the USB CDC endpoint.
        time.sleep(0.3)
        try:
            s.reset_input_buffer()
        except Exception:
            pass

        deadline = time.time() + args.total_timeout
        last_rx = time.time()
        buf = b""
        saw_end = False

        for cmd in commands:
            line = (cmd + "\n").encode("ascii", errors="replace")
            s.write(line)
            s.flush()
            note = f"[pa_capture] >> {cmd}"
            print(note)
            f.write(note + "\n")
            # Pause briefly between commands so responses don't interleave.
            time.sleep(0.1)

        # Now drain until END_MARKER or timeout/idle.
        while time.time() < deadline:
            chunk = s.read(512)
            now = time.time()
            if chunk:
                last_rx = now
                buf += chunk
                while True:
                    nl = buf.find(b"\n")
                    if nl < 0:
                        break
                    raw = buf[:nl].rstrip(b"\r")
                    buf = buf[nl + 1:]
                    text = raw.decode("ascii", errors="replace")
                    print(f"<< {text}")
                    f.write(text + "\n")
                    if END_MARKER in text:
                        saw_end = True
                        break
                if saw_end:
                    break
            else:
                # Idle. If we went too long with no traffic after the last
                # command was sent, give up - the printer probably rejected
                # the command or is still heating.
                if now - last_rx > args.idle_timeout:
                    print("[pa_capture] idle timeout; stopping")
                    break
        else:
            print("[pa_capture] total-timeout; stopping")

        if not saw_end:
            f.write("[pa_capture] NOTE: did not observe END PA_CAPTURE marker\n")

        f.flush()

    print(f"[pa_capture] done. saw_end={saw_end} output={out_path}")
    return 0 if saw_end else 2


if __name__ == "__main__":
    sys.exit(main())
