#!/usr/bin/env python3
"""Print lines from the board's serial port.

Exists because `arduino-cli monitor` prints nothing and exits immediately when
stdin is not a real terminal -- which is the case whenever it is driven from a
script or an agent rather than typed at a prompt. See CLAUDE.md **Board
gotchas**. This is the substitute: same job, no TTY required.

Usage:
    python tools/read_serial.py COM5                 # until Ctrl+C
    python tools/read_serial.py COM5 --seconds 30    # then stop
    python tools/read_serial.py COM5 --send r        # send a char, then read
"""

import argparse
import sys
import time

import serial


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=None,
                    help="stop after this long (default: until Ctrl+C)")
    ap.add_argument("--send", default=None,
                    help="write these characters once the port is open")
    args = ap.parse_args()

    # Right after an upload the board re-enumerates and the port is briefly
    # gone; opening then fails, and every press in that window is lost.
    # Wait for it instead of failing.
    wait_until = time.time() + 15
    while True:
        try:
            ser = serial.Serial(args.port, args.baud, timeout=0.5)
            break
        except serial.SerialException:
            if time.time() > wait_until:
                raise
            time.sleep(0.5)
    print(f"# listening on {args.port}", file=sys.stderr, flush=True)
    # Opening the port is what releases a sketch blocked on `while (!Serial)`,
    # so give the board a moment to notice before expecting output.
    time.sleep(0.3)
    if args.send:
        ser.write(args.send.encode())

    deadline = time.time() + args.seconds if args.seconds else None
    try:
        while deadline is None or time.time() < deadline:
            line = ser.readline()
            if line:
                sys.stdout.write(line.decode(errors="replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


if __name__ == "__main__":
    main()
