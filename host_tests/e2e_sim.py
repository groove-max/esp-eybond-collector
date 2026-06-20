#!/usr/bin/env python3
"""End-to-end test: the host simulator (real firmware core) vs the integration's protocol code.

Spawns build/host_sim, plays the HA side over real UDP/TCP sockets and the
inverter side over the simulator's PTY, using ha-eybond-local's own frame
builders/parsers. Run with the integration venv python:
    ../.venv/bin/python3 e2e_sim.py
"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
INTEGRATION = HERE.parent.parent / "ha-eybond-local"
sys.path.insert(0, str(INTEGRATION))

from custom_components.eybond_local.collector.protocol import (  # noqa: E402
    FC_FORWARD_TO_DEVICE,
    FC_HEARTBEAT,
    HEADER_SIZE,
    build_collector_request,
    decode_header,
    parse_heartbeat_pn,
)
from custom_components.eybond_local.payload.modbus import (  # noqa: E402
    build_read_holding_request,
    crc16_modbus,
)

UDP_PORT = 58901
PN = "V00000200000000001"
FAILURES = 0


def check(condition: bool, label: str) -> None:
    global FAILURES
    if condition:
        print(f"  ok   {label}")
    else:
        FAILURES += 1
        print(f"  FAIL {label}")


def recv_frame(conn: socket.socket, timeout: float = 5.0):
    """Read one collector frame (header + payload) from the reverse TCP stream."""
    conn.settimeout(timeout)
    header_bytes = b""
    while len(header_bytes) < HEADER_SIZE:
        chunk = conn.recv(HEADER_SIZE - len(header_bytes))
        if not chunk:
            raise ConnectionError("peer closed")
        header_bytes += chunk
    header = decode_header(header_bytes)
    payload = b""
    while len(payload) < header.payload_len:
        chunk = conn.recv(header.payload_len - len(payload))
        if not chunk:
            raise ConnectionError("peer closed")
        payload += chunk
    return header, payload


def recv_line(conn: socket.socket, timeout: float = 5.0) -> bytes:
    conn.settimeout(timeout)
    line = b""
    while not line.endswith(b"\n"):
        chunk = conn.recv(1)
        if not chunk:
            raise ConnectionError("peer closed")
        line += chunk
    return line


def main() -> int:
    subprocess.run(["make", "build/host_sim"], cwd=HERE, check=True, capture_output=True)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    server_port = server.getsockname()[1]

    sim = subprocess.Popen(
        [
            HERE / "build" / "host_sim",
            "--udp-port", str(UDP_PORT),
            "--pn", PN,
            "--command-spacing", "50",
            "--uart-timeout", "1000",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    try:
        startup = {}
        for _ in range(3):
            key, _, value = sim.stdout.readline().strip().partition(" ")
            startup[key] = value
        pty_path = startup["PTY"]
        print(f"simulator up: pty={pty_path} udp={startup['UDP']} pn={startup['PN']}")
        inverter = os.open(pty_path, os.O_RDWR | os.O_NOCTTY)
        import termios
        import tty
        tty.setraw(inverter)

        # 1. Discovery: integration-style redirect must get the factory reply
        #    and trigger the reverse TCP connection.
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.settimeout(5.0)
        udp.sendto(f"set>server=127.0.0.1:{server_port};".encode(), ("127.0.0.1", UDP_PORT))
        reply, _ = udp.recvfrom(256)
        check(reply == b"rsp>server=2;", f"discovery reply {reply!r}")

        server.settimeout(5.0)
        conn, peer = server.accept()
        print(f"reverse TCP from {peer}")

        # 2. Unsolicited heartbeat on connect carries the PN.
        header, payload = recv_frame(conn)
        check(header.fcode == FC_HEARTBEAT, "connect heartbeat fcode")
        check(parse_heartbeat_pn(payload) == PN[:14], f"heartbeat pn {parse_heartbeat_pn(payload)!r}")

        # 3. Server-initiated heartbeat is answered with the same tid.
        conn.sendall(build_collector_request(0x0042, b"", devcode=0, collector_addr=1, fcode=FC_HEARTBEAT))
        header, payload = recv_frame(conn)
        check(header.fcode == FC_HEARTBEAT and header.tid == 0x0042, "server heartbeat echo tid")

        # 4. AT channel on the same stream.
        conn.sendall(b"AT+DTUPN?\r\n")
        check(recv_line(conn) == f"AT+DTUPN:{PN}\r\n".encode(), "AT+DTUPN reply")
        conn.sendall(b"AT+CLDSRVHOST1?\r\n")
        check(
            recv_line(conn) == f"AT+CLDSRVHOST1:127.0.0.1,{server_port},TCP\r\n".encode(),
            "AT+CLDSRVHOST1 tracks discovery endpoint",
        )

        # 5. FC=4 modbus round-trip through the PTY "inverter".
        request = build_read_holding_request(1, 100, 2)
        conn.sendall(build_collector_request(0x0301, request, devcode=0x0994, collector_addr=0x10,
                                             fcode=FC_FORWARD_TO_DEVICE))
        seen = os.read(inverter, 64)
        check(seen == request, f"inverter received request {seen.hex()}")
        response_body = bytes([1, 0x03, 4, 0, 1, 0, 2])
        response = response_body + crc16_modbus(response_body).to_bytes(2, "little")
        os.write(inverter, response)
        header, payload = recv_frame(conn)
        check(header.fcode == FC_FORWARD_TO_DEVICE and header.tid == 0x0301, "FC4 response tid")
        check(payload == response, f"FC4 payload verbatim {payload.hex()}")

        # 6. Inverter silence -> honest timeout: no frame at all.
        conn.sendall(build_collector_request(0x0302, request, devcode=0x0994, collector_addr=0x10,
                                             fcode=FC_FORWARD_TO_DEVICE))
        os.read(inverter, 64)  # request reaches the inverter, which stays silent
        try:
            header, _ = recv_frame(conn, timeout=1.6)  # uart-timeout 1.0s + margin
            check(False, f"unexpected frame after timeout fc={header.fcode}")
        except socket.timeout:
            check(True, "timeout produced no reply")

        # 7. Bridge is serialized: queued request goes out after the spacing window.
        conn.sendall(build_collector_request(0x0303, request, devcode=0x0994, collector_addr=0x10,
                                             fcode=FC_FORWARD_TO_DEVICE))
        seen = os.read(inverter, 64)
        check(seen == request, "queued request reached inverter after spacing")
        os.write(inverter, response)
        header, payload = recv_frame(conn)
        check(header.tid == 0x0303 and payload == response, "queued request answered")

        conn.close()
        server.close()
        udp.close()
        os.close(inverter)
    finally:
        sim.terminate()
        sim.wait(timeout=5)

    print(f"{FAILURES} failures")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    raise SystemExit(main())
