#!/usr/bin/env python3
"""Scan the bridge's UART for a Modbus RTU inverter (SMG family) over Wi-Fi.

Plays the HA side and forwards Modbus read-holding probes while sweeping baud
rate and slave id. A live Modbus device replies (data OR an exception frame) to
its OWN slave id and the right baud; a wrong slave id stays silent, a wrong baud
yields garbage. So: any returned bytes = we found it.

Usage: ../.venv/bin/python3 host_tests/modbus_scan.py --address eybond-pi30.local
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
from custom_components.eybond_local.payload.modbus import build_read_holding_request  # noqa: E402

# Registers that exist on the SMG family (from the integration's SMG register map),
# so a correctly-addressed device returns DATA rather than only an exception.
PROBE_REGISTERS = (100, 201, 0)


def open_link(device_ip, laptop_ip, udp_port):
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
        conn.settimeout(max(0.05, deadline - time.monotonic()))
        hb = b""
        while len(hb) < HEADER_SIZE:
            c = conn.recv(HEADER_SIZE - len(hb))
            if not c:
                raise ConnectionError("closed")
            hb += c
        hdr = decode_header(hb)
        payload = b""
        while len(payload) < hdr.payload_len:
            c = conn.recv(hdr.payload_len - len(payload))
            if not c:
                raise ConnectionError("closed")
            payload += c
        if hdr.fcode == FC_HEARTBEAT and hdr.tid >= 0x8000:
            continue
        return hdr, payload


def forward(conn, tid, payload, timeout):
    conn.sendall(build_collector_request(tid, payload, devcode=0x0994, collector_addr=0x01,
                                         fcode=FC_FORWARD_TO_DEVICE))
    try:
        return recv_frame(conn, timeout)[1]
    except socket.timeout:
        return None


def at_write(conn, line):
    conn.sendall(line)
    conn.settimeout(3.0)
    out = b""
    try:
        while not out.endswith(b"\n"):
            out += conn.recv(1)
    except socket.timeout:
        pass
    return out.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--address", default="eybond-pi30.local")
    ap.add_argument("--udp-port", type=int, default=58899)
    ap.add_argument("--bauds", default="9600,2400,4800,19200,38400")
    ap.add_argument("--slaves", default="1,2,3,4,5,6,7,8")
    ap.add_argument("--timeout", type=float, default=1.6)
    args = ap.parse_args()

    bauds = [int(b) for b in args.bauds.split(",")]
    slaves = [int(s) for s in args.slaves.split(",")]

    device_ip = socket.gethostbyname(args.address)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((device_ip, 9))
    laptop_ip = s.getsockname()[0]
    s.close()
    print(f"device={device_ip} laptop={laptop_ip}")

    srv, conn = open_link(device_ip, laptop_ip, args.udp_port)
    print("reverse TCP up; scanning (any returned bytes = a live Modbus device)\n")
    try:
        recv_frame(conn, 0.3)  # drain first heartbeat
    except socket.timeout:
        pass

    tid = 0x0200
    found = []
    for baud in bauds:
        ack = at_write(conn, f"AT+UART={baud},8,1,NONE\r\n".encode())
        if not ack.endswith(b"W000"):
            print(f"[baud {baud}] AT+UART not acked ({ack!r}); skipping")
            continue
        time.sleep(0.4)
        any_hit = False
        for slave in slaves:
            reg = PROBE_REGISTERS[0]
            tid = (tid + 1) & 0x7FFF
            resp = forward(conn, tid, build_read_holding_request(slave, reg, 1), args.timeout)
            if resp:
                any_hit = True
                tag = "EXCEPTION" if len(resp) >= 2 and resp[1] & 0x80 else "DATA"
                print(f"[baud {baud}] slave {slave:>2} reg {reg} -> {tag} {resp.hex()}")
                found.append((baud, slave, resp.hex()))
                # confirm with the other registers
                for reg2 in PROBE_REGISTERS[1:]:
                    tid = (tid + 1) & 0x7FFF
                    r2 = forward(conn, tid, build_read_holding_request(slave, reg2, 1), args.timeout)
                    print(f"           slave {slave:>2} reg {reg2} -> {r2.hex() if r2 else 'timeout'}")
        if not any_hit:
            print(f"[baud {baud}] slaves {slaves[0]}-{slaves[-1]}: all silent")

    at_write(conn, b"AT+UART=2400,8,1,NONE\r\n")
    conn.close()
    srv.close()
    print()
    if found:
        print(f"FOUND a Modbus device: {found}")
    else:
        print("No Modbus response on any baud/slave. The inverter is not answering at all")
        print("-> physical link (TX/RX swap, no common ground, RS232-vs-TTL levels) or the")
        print("   inverter's comms port/protocol, not a baud/slave-id mismatch.")


if __name__ == "__main__":
    main()
