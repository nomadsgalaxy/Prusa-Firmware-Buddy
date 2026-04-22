"""Interactive serial bridge for Prusa printers.

Opens a serial port, forwards stdin lines to the printer, and tees everything
received back to stdout + a log file. Designed to be driven interactively.

Usage:
    python utils/serial_bridge.py COM5 [log_file.txt]

Meta-commands (start with '@', processed locally, not sent to the printer):
    @log <path>   switch the tee file
    @quit         close the port and exit
    @status       print port + log state
"""

from __future__ import annotations

import sys
import threading
import time
from pathlib import Path

import serial  # pyserial


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: serial_bridge.py <port> [logfile]", flush=True)
        return 2

    port = sys.argv[1]
    log_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("serial_capture.log")

    # Open log file in line-buffered text mode so we see output live.
    log = open(log_path, "w", buffering=1, encoding="utf-8", newline="\n")
    log_lock = threading.Lock()

    try:
        s = serial.Serial(port, 115200, timeout=0.2)
    except serial.SerialException as exc:
        print(f"OPEN_FAIL: {exc}", flush=True)
        return 1

    print(f"OPEN_OK port={port} log={log_path}", flush=True)

    stop = threading.Event()

    def reader() -> None:
        # Read bytes, split on newlines. Prusa firmware terminates with \n.
        buf = b""
        while not stop.is_set():
            chunk = s.read(512)
            if not chunk:
                continue
            buf += chunk
            while True:
                nl = buf.find(b"\n")
                if nl < 0:
                    break
                line = buf[:nl].rstrip(b"\r").decode("ascii", errors="replace")
                buf = buf[nl + 1:]
                ts = time.strftime("%H:%M:%S")
                out = f"<< {line}"
                print(out, flush=True)
                with log_lock:
                    log.write(f"{ts} {line}\n")

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    print("READY", flush=True)

    try:
        for raw in sys.stdin:
            cmd = raw.rstrip("\r\n")
            if not cmd:
                continue
            if cmd.startswith("@"):
                parts = cmd.split(maxsplit=1)
                op = parts[0]
                if op == "@quit":
                    break
                if op == "@status":
                    print(f"STATUS port={port} open={s.is_open} log={log_path}", flush=True)
                    continue
                if op == "@log" and len(parts) == 2:
                    new_path = Path(parts[1])
                    with log_lock:
                        log.close()
                        log_path = new_path
                        log = open(log_path, "w", buffering=1, encoding="utf-8", newline="\n")
                    print(f"LOG_SWITCHED log={log_path}", flush=True)
                    continue
                print(f"UNKNOWN_META {cmd}", flush=True)
                continue
            # Send to printer. Marlin accepts LF line termination.
            s.write((cmd + "\n").encode("ascii", errors="replace"))
            s.flush()
            ts = time.strftime("%H:%M:%S")
            print(f">> {cmd}", flush=True)
            with log_lock:
                log.write(f"{ts} >> {cmd}\n")
    finally:
        stop.set()
        try:
            s.close()
        except Exception:
            pass
        with log_lock:
            log.close()
        print("CLOSED", flush=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
