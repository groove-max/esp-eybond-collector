#!/usr/bin/env python3
"""Hardware-in-the-loop E2E: real ESP8266 bridge + fake PI30 inverter on /dev/ttyUSB0.

Laptop plays both sides: the HA integration (UDP discovery + reverse TCP server,
using ha-eybond-local's own builders/parsers) and the inverter (fake_pi30_serial
on the USB UART). Run with the esp-collector venv:
    .venv/bin/python3 host_tests/bench_e2e.py [--address eybond-bench.local] [--serial /dev/ttyUSB0]

If a live Home Assistant also owns the bench device, its periodic discovery can
steal the bridge mid-test (last redirect wins). Each step therefore retries once
after re-acquiring the device.
"""

from __future__ import annotations

import argparse
import socket
import sys
import time
from pathlib import Path

INTEGRATION = Path(__file__).resolve().parent.parent.parent / "ha-eybond-local"
sys.path.insert(0, str(INTEGRATION))

from custom_components.eybond_local.collector.protocol import (  # noqa: E402
    FC_FORWARD_TO_DEVICE,
    FC_HEARTBEAT,
    FC_QUERY_COLLECTOR,
    FC_SET_COLLECTOR,
    HEADER_SIZE,
    build_collector_request,
    decode_header,
    parse_heartbeat_pn,
)
from custom_components.eybond_local.collector.smartess_local import (  # noqa: E402
    build_query_collector_payload,
    build_set_collector_payload,
    parse_query_collector_response,
    parse_set_collector_response,
)
from custom_components.eybond_local.payload.pi30 import build_request, parse_response  # noqa: E402

from fake_pi30_serial import RESPONSES, FakePi30Serial  # noqa: E402

PN = "V00000200000000001"
FAILURES = 0


def check(condition: bool, label: str) -> None:
    global FAILURES
    if condition:
        print(f"  ok   {label}")
    else:
        FAILURES += 1
        print(f"  FAIL {label}")


class BenchLink:
    """Reverse-TCP link to the bridge that can re-acquire it after HA steals it."""

    def __init__(self, device_ip: str, laptop_ip: str, udp_port: int) -> None:
        self.device_ip = device_ip
        self.laptop_ip = laptop_ip
        self.udp_port = udp_port
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((laptop_ip, 0))
        self.server.listen(1)
        self.server_port = self.server.getsockname()[1]
        self.conn: socket.socket | None = None
        self.first_heartbeat = None  # (header, payload) captured on (re)connect

    def acquire(self, attempts: int = 20) -> None:
        if self.conn is not None:
            try:
                self.conn.close()
            except OSError:
                pass
            self.conn = None
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.settimeout(2.0)
        redirect = f"set>server={self.laptop_ip}:{self.server_port};".encode()
        reply = b""
        for attempt in range(attempts):
            udp.sendto(redirect, (self.device_ip, self.udp_port))
            try:
                reply, sender = udp.recvfrom(256)
                if reply == b"rsp>server=2;" and sender[0] == self.device_ip:
                    break
            except socket.timeout:
                print(f"  ...waiting for device (attempt {attempt + 1})")
        udp.close()
        if reply != b"rsp>server=2;":
            raise ConnectionError("device did not answer discovery")

        self.server.settimeout(10.0)
        conn, peer = self.server.accept()
        if peer[0] != self.device_ip:
            conn.close()
            raise ConnectionError(f"unexpected peer {peer}")
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.conn = conn
        self.first_heartbeat = self.recv_frame(skip_heartbeats=False)

    def close(self) -> None:
        if self.conn is not None:
            self.conn.close()
        self.server.close()

    def _recv_exact(self, count: int) -> bytes:
        data = b""
        while len(data) < count:
            chunk = self.conn.recv(count - len(data))
            if not chunk:
                raise ConnectionError("peer closed")
            data += chunk
        return data

    def recv_frame(self, timeout: float = 8.0, skip_heartbeats: bool = True):
        deadline = time.monotonic() + timeout
        while True:
            self.conn.settimeout(max(0.1, deadline - time.monotonic()))
            header = decode_header(self._recv_exact(HEADER_SIZE))
            payload = self._recv_exact(header.payload_len)
            if skip_heartbeats and header.fcode == FC_HEARTBEAT and header.tid >= 0x8000:
                continue
            return header, payload

    def recv_line(self, timeout: float = 8.0) -> bytes:
        self.conn.settimeout(timeout)
        line = b""
        while not line.endswith(b"\n"):
            line += self._recv_exact(1)
        return line

    def send(self, data: bytes) -> None:
        self.conn.sendall(data)

    def forward(self, tid: int, command: str) -> None:
        self.send(build_collector_request(tid, build_request(command), devcode=0x0994,
                                          collector_addr=0x01, fcode=FC_FORWARD_TO_DEVICE))

    def run_step(self, label: str, step, attempts: int = 4) -> None:
        """Run one test step; on a stolen/dropped link re-acquire and retry."""
        for attempt in range(1, attempts + 1):
            try:
                step()
                return
            except ConnectionError as exc:
                if attempt == attempts:
                    raise
                print(f"  ...link lost during '{label}' ({exc}), re-acquiring")
                self.acquire()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", default="eybond-bench.local")
    parser.add_argument("--serial", default="/dev/ttyUSB0")
    parser.add_argument("--udp-port", type=int, default=58899)
    args = parser.parse_args()

    device_ip = socket.gethostbyname(args.address)
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((device_ip, 9))
    laptop_ip = probe.getsockname()[0]
    probe.close()
    print(f"device={device_ip} laptop={laptop_ip}")

    fake = FakePi30Serial(args.serial)
    fake.start()
    link = BenchLink(device_ip, laptop_ip, args.udp_port)

    try:
        # 1+2. Discovery (retried internally: serial open may have rebooted the
        # board via DTR/RTS) and the connect heartbeat identity.
        link.acquire()
        check(True, "discovery reply + reverse TCP")
        header, payload = link.first_heartbeat
        check(header.fcode == FC_HEARTBEAT, "connect heartbeat fcode")
        check(parse_heartbeat_pn(payload) == PN[:14], f"heartbeat pn {parse_heartbeat_pn(payload)!r}")

        # 3. Server-initiated heartbeat is answered with the same tid.
        def step_heartbeat_echo():
            link.send(build_collector_request(0x0042, b"", devcode=0, collector_addr=1, fcode=FC_HEARTBEAT))
            header, _ = link.recv_frame(skip_heartbeats=False)
            check(header.fcode == FC_HEARTBEAT and header.tid == 0x0042, "server heartbeat echo tid")
        link.run_step("heartbeat echo", step_heartbeat_echo)

        # 4. AT channel.
        def step_at():
            link.send(b"AT+DTUPN?\r\n")
            check(link.recv_line() == f"AT+DTUPN:{PN}\r\n".encode(), "AT+DTUPN")
            link.send(b"AT+WFSS?\r\n")
            rssi_line = link.recv_line()
            check(rssi_line.startswith(b"AT+WFSS:-") and rssi_line.endswith(b"\r\n"), f"AT+WFSS {rssi_line!r}")
            link.send(b"AT+VDTU?\r\n")
            vdtu_line = link.recv_line()
            check(
                vdtu_line.startswith(b"AT+VDTU:esp-collector,")
                and b";features=local_only,no_cloud,wifi_params,endpoint_write;" in vdtu_line
                and b";uart=2400,8,1,NONE;" in vdtu_line,
                f"AT+VDTU {vdtu_line!r}",
            )
        link.run_step("AT channel", step_at)

        # 5. FC4 PI30 round-trips through the real UART.
        def make_fc4_step(tid: int, command: str):
            def step():
                link.forward(tid, command)
                header, payload = link.recv_frame()
                decoded = parse_response(payload)
                check(
                    header.fcode == FC_FORWARD_TO_DEVICE and header.tid == tid and decoded == RESPONSES[command],
                    f"FC4 {command} -> {decoded[:32]!r}",
                )
            return step

        for tid, command in ((0x0101, "QPI"), (0x0102, "QPIGS"), (0x0103, "QMOD")):
            link.run_step(f"FC4 {command}", make_fc4_step(tid, command))
        check(fake.requests[-3:] == ["QPI", "QPIGS", "QMOD"], f"inverter saw {fake.requests[-3:]}")

        # 6. Honest timeout: silent command produces no frame at all.
        def step_timeout():
            link.forward(0x0104, "QNOREPLY")
            try:
                header, _ = link.recv_frame(timeout=1.8)  # bench response_timeout 1s + margin
                check(False, f"unexpected frame after timeout fc={header.fcode} tid={header.tid:#x}")
            except socket.timeout:
                check(True, "QNOREPLY produced no reply (timeout)")
        link.run_step("timeout", step_timeout)

        # 7. Queueing: back-to-back requests are serialized and both answered.
        def step_queue():
            link.forward(0x0105, "QPI")
            link.forward(0x0106, "QMOD")
            header_a, payload_a = link.recv_frame()
            header_b, payload_b = link.recv_frame()
            check(
                (header_a.tid, header_b.tid) == (0x0105, 0x0106)
                and parse_response(payload_a) == RESPONSES["QPI"]
                and parse_response(payload_b) == RESPONSES["QMOD"],
                "queued requests answered in order",
            )
        link.run_step("queueing", step_queue)

        # 8. Collector parameters (FC=2/FC=3) — the integration's WiFi options
        # flow uses exactly these: query 41/48/49, set 41 -> 43 -> 29.
        def query_param(tid: int, parameter: int):
            link.send(build_collector_request(tid, build_query_collector_payload(parameter),
                                              devcode=0x0994, collector_addr=0x01,
                                              fcode=FC_QUERY_COLLECTOR))
            header, payload = link.recv_frame()
            return parse_query_collector_response(payload)

        def step_params():
            response = query_param(0x0201, 2)  # collector_pn
            check(response.code == 0 and response.text == PN, f"FC2 param 2 pn {response.text!r}")
            response = query_param(0x0202, 34)  # serial_baudrate
            check(response.code == 0 and response.text == "2400", f"FC2 param 34 baud {response.text!r}")
            response = query_param(0x0203, 41)  # router_ssid (real value stays off the repo)
            check(response.code == 0 and len(response.text) > 0, "FC2 param 41 ssid non-empty")
            response = query_param(0x0204, 48)  # network diagnostics / rssi
            check(response.code == 0 and response.text.startswith("-"), f"FC2 param 48 rssi {response.text!r}")
            response = query_param(0x0205, 55)  # gprs_csq: unsupported -> refused
            check(response.code == 1, "FC2 param 55 refused")
        link.run_step("collector params", step_params)

        def step_scan_list():
            first = query_param(0x0206, 49)
            check(first.code == 0 and first.text.startswith("["), f"FC2 param 49 immediate {first.text[:40]!r}")
            time.sleep(4.0)  # async scan finishes in the background
            second = query_param(0x0207, 49)
            check(
                second.code == 0 and second.text.count("[") >= 1,
                f"FC2 param 49 scan list {second.text[:60]!r}",
            )
        link.run_step("wifi scan list", step_scan_list)

        def set_param(tid: int, parameter: int, value: str):
            link.send(build_collector_request(tid, build_set_collector_payload(parameter, value),
                                              devcode=0x0994, collector_addr=0x01, fcode=FC_SET_COLLECTOR))
            header, payload = link.recv_frame()
            return parse_set_collector_response(payload)

        def step_param_writes():
            # Commit with nothing staged -> refused (the param-29 guard). We avoid
            # staging Wi-Fi credentials here so the bench network is never touched.
            response = set_param(0x0208, 29, "1")
            check(response.status == 1 and response.parameter == 29, "FC3 apply with nothing staged refused")

            # Endpoint write (Item 5) is now ACCEPTED and committed. We write back
            # the bridge's CURRENT endpoint so the commit is a no-op retarget that
            # cannot break the live link; the real move-to-another-host retarget is
            # covered by the host test. Format matches the param-21 read.
            current = query_param(0x0209, 21)
            check(current.code == 0 and "," in current.text, f"FC2 param 21 current endpoint {current.text!r}")
            staged = set_param(0x020A, 21, current.text)
            check(staged.status == 0 and staged.parameter == 21, "FC3 param 21 endpoint accepted")
            applied = set_param(0x020B, 29, "1")
            check(applied.status == 0 and applied.parameter == 29, "FC3 param 29 commits staged endpoint")

            # Link survived the (no-op) endpoint commit.
            link.send(b"AT+DTUPN?\r\n")
            check(link.recv_line() == f"AT+DTUPN:{PN}\r\n".encode(), "link alive after endpoint commit")
        link.run_step("param writes", step_param_writes)

        # 9. Runtime UART re-baud: switch to 9600 (no FC4 traffic while there!)
        # and back to 2400, then prove the bridge still talks to the inverter.
        def step_rebaud():
            link.send(b"AT+UART=9600,8,1,NONE\r\n")
            check(link.recv_line() == b"AT+UART:W000\r\n", "AT+UART=9600 acked")
            response = query_param(0x0301, 34)
            check(response.code == 0 and response.text == "9600", f"param 34 now {response.text!r}")
            link.send(b"AT+UART?\r\n")
            check(link.recv_line() == b"AT+UART:9600,8,1,NONE\r\n", "AT+UART? reflects 9600")

            link.send(b"AT+UART=2400,8,1,NONE\r\n")
            check(link.recv_line() == b"AT+UART:W000\r\n", "AT+UART=2400 acked")
            response = query_param(0x0302, 34)
            check(response.code == 0 and response.text == "2400", f"param 34 back to {response.text!r}")

            link.forward(0x0303, "QPI")
            header, payload = link.recv_frame()
            check(header.tid == 0x0303 and parse_response(payload) == RESPONSES["QPI"],
                  "FC4 works after re-baud round trip")
        link.run_step("uart rebaud", step_rebaud)
    finally:
        link.close()
        fake.stop()

    print(f"{FAILURES} failures")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    raise SystemExit(main())
