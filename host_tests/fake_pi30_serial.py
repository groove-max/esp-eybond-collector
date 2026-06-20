#!/usr/bin/env python3
"""Fake PI30/Voltronic inverter on a serial port (2400 8N1).

On the bench the ESP8266's UART0 is reachable through the same USB cable used
for flashing, so this fake plays the inverter on /dev/ttyUSB0 while the bridge
firmware treats it as real hardware. Response values are the synthetic ones
from ha-eybond-local's PI30 driver tests, so the real integration can probe and
poll this fake successfully.

CRC-invalid lines (e.g. ESP8266 74880-baud boot noise seen at 2400) are
silently discarded. The QNOREPLY command is deliberately never answered — it
exercises the bridge's honest-timeout path.
"""

from __future__ import annotations

import sys
import threading
from pathlib import Path

import serial

INTEGRATION = Path(__file__).resolve().parent.parent.parent / "ha-eybond-local"
sys.path.insert(0, str(INTEGRATION))

from custom_components.eybond_local.payload.pi30 import crc16_xmodem  # noqa: E402

RESPONSES = {
    "QPI": "PI30",
    "QID": "553555355535552",
    # QPIRI + QMN pair matches the integration's catalog: VMII-NXPW5KW -> "PowMr 4.2kW"
    # (see test_probe_selects_vmii_model_overlay_when_model_number_matches)
    "QPIRI": "220.0 19.0 220.0 50.0 19.0 5000 5000 48.0 54.0 42.0 56.4 54.0 2 30 80 0 2 2 1 10 0 0 54.0 0 1",
    "QMN": "VMII-NXPW5KW",
    "QFLAG": "EadzDbjkuvxy",
    "QMOD": "L",
    "QPIWS": "00000100000000000000000000000000",
    "QPIGS": "239.5 49.9 239.5 49.9 0927 0924 015 396 26.60 000 100 0028 002.2 315.9 00.00 00000 00010000 00 00 00665 000",
    "Q1": "00001 16971 01 00 00 026 033 022 029 02 00 000 0036 0000 0000 49.95 10 0 060 030 100 030 58.40 000 120 0 0000",
    "QET": "12345",
    "QLT": "2345",
    "QT": "20260407113059",
}
SILENT_COMMANDS = {"QNOREPLY"}


def _escape_crc_byte(value: int) -> int:
    return value + 1 if value in (0x28, 0x0D, 0x0A) else value


def _crc_bytes(body: bytes) -> bytes:
    crc = crc16_xmodem(body)
    return bytes((_escape_crc_byte((crc >> 8) & 0xFF), _escape_crc_byte(crc & 0xFF)))


def build_pi30_response(value: str) -> bytes:
    body = f"({value}".encode("ascii")
    return body + _crc_bytes(body) + b"\r"


class FakePi30Serial:
    def __init__(self, port: str, baud: int = 2400) -> None:
        self.serial = serial.Serial(port, baud, timeout=0.05, dsrdtr=False, rtscts=False)
        # Keep DTR/RTS deasserted so the ESP auto-reset circuit stays idle.
        self.serial.dtr = False
        self.serial.rts = False
        self.requests: list[str] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)
        self.serial.close()

    def _run(self) -> None:
        buffer = b""
        while not self._stop.is_set():
            data = self.serial.read(64)
            if data:
                buffer += data
            while b"\r" in buffer:
                line, _, buffer = buffer.partition(b"\r")
                self._handle_line(line)
            if len(buffer) > 256:  # boot garbage without terminators
                buffer = b""

    def _handle_line(self, line: bytes) -> None:
        # Boot noise can glue itself to the front of the first real request
        # (it has no terminator of its own), so accept any CRC-valid suffix.
        body = None
        for start in range(0, max(1, len(line) - 2)):
            candidate = line[start:]
            if len(candidate) < 3:
                break
            if _crc_bytes(candidate[:-2]) == candidate[-2:]:
                body = candidate[:-2]
                break
        if body is None:
            return  # noise or boot log, not a PI30 request
        try:
            command = body.decode("ascii")
        except UnicodeDecodeError:
            return
        self.requests.append(command)
        if command in SILENT_COMMANDS:
            print(f"fake_pi30: {command} -> (silent)", flush=True)
            return
        value = RESPONSES.get(command, "NAK")
        self.serial.write(build_pi30_response(value))
        print(f"fake_pi30: {command} -> ({value[:40]}{'...' if len(value) > 40 else ''}", flush=True)


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
    fake = FakePi30Serial(port)
    fake.start()
    print(f"fake PI30 inverter on {port} @2400 8N1, Ctrl-C to stop", flush=True)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        fake.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
