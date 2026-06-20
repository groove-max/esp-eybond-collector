#!/usr/bin/env python3
"""Low-level bridge<->inverter UART diagnostic over Wi-Fi (no HA, no fake).

Plays the HA side, then forwards raw probes to whatever is on the bridge's UART
at several baud rates and prints ANY bytes that come back. This separates:
  - timeout on every probe  -> no bytes at all (wiring/ground/TX-RX swap, or dead bus)
  - bytes that parse as PI30 -> PI30 inverter, right baud
  - bytes only at 9600 / modbus-shaped -> SMG/Modbus inverter, wrong default baud
  - garbage bytes           -> wrong baud / level / framing

Usage: ../.venv/bin/python3 host_tests/uart_diag.py --address eybond-pi30.local
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
    HEADER_SIZE,
    build_collector_request,
    decode_header,
)
from custom_components.eybond_local.payload.pi30 import build_request  # noqa: E402
from custom_components.eybond_local.payload.modbus import build_read_holding_request  # noqa: E402


def open_link(device_ip: str, laptop_ip: str, udp_port: int):
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((laptop_ip, 0))
    srv.listen(1)
    port = srv.getsockname()[1]
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.settimeout(2.0)
    for _ in range(15):
        udp.sendto(f"set>server={laptop_ip}:{port};".encode(), (device_ip, udp_port))
        try:
            if udp.recvfrom(64)[0] == b"rsp>server=2;":
                break
        except socket.timeout:
            pass
    udp.close()
    srv.settimeout(10.0)
    conn, _ = srv.accept()
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return srv, conn


def recv_frame(conn, timeout):
    deadline = time.monotonic() + timeout
    while True:
        conn.settimeout(max(0.1, deadline - time.monotonic()))
        hdr_bytes = b""
        while len(hdr_bytes) < HEADER_SIZE:
            chunk = conn.recv(HEADER_SIZE - len(hdr_bytes))
            if not chunk:
                raise ConnectionError("closed")
            hdr_bytes += chunk
        hdr = decode_header(hdr_bytes)
        payload = b""
        while len(payload) < hdr.payload_len:
            chunk = conn.recv(hdr.payload_len - len(payload))
            if not chunk:
                raise ConnectionError("closed")
            payload += chunk
        if hdr.fcode == FC_HEARTBEAT and hdr.tid >= 0x8000:
            continue
        return hdr, payload


def drain_heartbeats(conn):
    try:
        recv_frame(conn, 0.3)
    except socket.timeout:
        pass


def forward(conn, tid, payload, timeout=4.0):
    conn.sendall(build_collector_request(tid, payload, devcode=0x0994, collector_addr=0x01,
                                         fcode=FC_FORWARD_TO_DEVICE))
    try:
        hdr, resp = recv_frame(conn, timeout)
        return resp
    except socket.timeout:
        return None


def at_write(conn, line):
    conn.sendall(line)
    # consume the W000/value ack line
    conn.settimeout(3.0)
    out = b""
    try:
        while not out.endswith(b"\n"):
            out += conn.recv(1)
    except socket.timeout:
        pass
    return out.strip()


def describe(resp):
    if resp is None:
        return "TIMEOUT (no bytes from inverter)"
    if not resp:
        return "empty frame"
    text = resp.decode("ascii", errors="replace")
    printable = sum(1 for b in resp if 32 <= b < 127)
    kind = "looks ASCII/PI30" if printable >= len(resp) * 0.8 else "binary (modbus-like?)"
    return f"{len(resp)} bytes [{kind}] hex={resp.hex()} ascii={text!r}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--address", default="eybond-pi30.local")
    ap.add_argument("--udp-port", type=int, default=58899)
    args = ap.parse_args()

    device_ip = socket.gethostbyname(args.address)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((device_ip, 9))
    laptop_ip = s.getsockname()[0]
    s.close()
    print(f"device={device_ip} laptop={laptop_ip}")

    srv, conn = open_link(device_ip, laptop_ip, args.udp_port)
    print("reverse TCP up")
    drain_heartbeats(conn)

    tid = 0x0100
    probes = [
        ("2400", "PI30 QPI", build_request("QPI")),
        ("2400", "PI30 QPIGS", build_request("QPIGS")),
        ("2400", "modbus rd 0x0000", build_read_holding_request(1, 0, 1)),
    ]
    # Try at the current (2400) baud first.
    for baud, name, payload in probes:
        tid += 1
        resp = forward(conn, tid, payload)
        print(f"[{baud}] {name:18s} -> {describe(resp)}")

    # Switch to 9600 and retry (SMG/Modbus family).
    print("--- switching bridge UART to 9600 ---")
    print("AT+UART:", at_write(conn, b"AT+UART=9600,8,1,NONE\r\n").decode(errors="replace"))
    time.sleep(0.5)
    for name, payload in (("modbus rd 0x0000", build_read_holding_request(1, 0, 1)),
                          ("modbus rd 0x00ab", build_read_holding_request(1, 0xAB, 1)),
                          ("PI30 QPI", build_request("QPI"))):
        tid += 1
        resp = forward(conn, tid, payload)
        print(f"[9600] {name:18s} -> {describe(resp)}")

    # Restore 2400.
    at_write(conn, b"AT+UART=2400,8,1,NONE\r\n")
    print("--- restored 2400 ---")
    conn.close()
    srv.close()


if __name__ == "__main__":
    main()
