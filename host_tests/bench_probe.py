#!/usr/bin/env python3
"""Run the integration's REAL Pi30Driver against the live bench device.

This is the detection step the HA config flow performs ("model in catalog"),
executed standalone: discovery -> reverse TCP -> Pi30Driver.async_probe() over
FC=4 through the ESP bridge and the fake PI30 inverter on /dev/ttyUSB0.
Expected outcome: model "PowMr 4.2kW" (catalog binding via QMN=VMII-NXPW5KW)
plus a live async_read_values() pass.

Run with the integration venv (has pyserial installed):
    ../../.venv/bin/python3 bench_probe.py [--address eybond-bench.local]
"""

from __future__ import annotations

import argparse
import asyncio
import socket
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
INTEGRATION = HERE.parent.parent / "ha-eybond-local"
sys.path.insert(0, str(INTEGRATION))
sys.path.insert(0, str(HERE))

from custom_components.eybond_local.collector.protocol import (  # noqa: E402
    FC_FORWARD_TO_DEVICE,
    FC_HEARTBEAT,
    HEADER_SIZE,
    build_collector_request,
    decode_header,
)
from custom_components.eybond_local.drivers.pi30 import Pi30Driver  # noqa: E402
from custom_components.eybond_local.models import CollectorInfo, ProbeTarget  # noqa: E402

from fake_pi30_serial import FakePi30Serial  # noqa: E402

REQUEST_TIMEOUT = 6.0


class LiveBenchTransport:
    """Legacy-contract transport (async_send_forward) over the live reverse TCP link."""

    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter, remote_ip: str) -> None:
        self._reader = reader
        self._writer = writer
        self._tid = 0
        self.collector_info = CollectorInfo(remote_ip=remote_ip)
        self.connected = True
        self.commands: list[str] = []

    async def wait_until_connected(self, timeout: float) -> bool:
        return True

    async def wait_until_heartbeat(self, timeout: float) -> bool:
        return True

    async def _read_frame(self):
        header_bytes = await self._reader.readexactly(HEADER_SIZE)
        header = decode_header(header_bytes)
        payload = await self._reader.readexactly(header.payload_len)
        return header, payload

    async def async_send_forward(self, payload: bytes, *, devcode: int, collector_addr: int) -> bytes:
        self._tid = (self._tid + 1) & 0x7FFF
        tid = self._tid
        self.commands.append(payload[:-3].decode("ascii", errors="replace"))
        frame = build_collector_request(tid, payload, devcode=devcode, collector_addr=collector_addr,
                                        fcode=FC_FORWARD_TO_DEVICE)
        self._writer.write(frame)
        await self._writer.drain()

        deadline = asyncio.get_event_loop().time() + REQUEST_TIMEOUT
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                raise asyncio.TimeoutError()
            try:
                header, response = await asyncio.wait_for(self._read_frame(), timeout=remaining)
            except asyncio.TimeoutError:
                raise
            if header.fcode == FC_HEARTBEAT:
                continue  # unsolicited keepalive
            if header.fcode == FC_FORWARD_TO_DEVICE and header.tid == tid:
                return response
            # stale/unrelated frame: keep waiting


async def open_reverse_link(device_ip: str, laptop_ip: str, udp_port: int):
    accepted: asyncio.Future = asyncio.get_event_loop().create_future()

    async def on_connect(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        if not accepted.done():
            accepted.set_result((reader, writer))

    server = await asyncio.start_server(on_connect, laptop_ip, 0)
    server_port = server.sockets[0].getsockname()[1]

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setblocking(False)
    loop = asyncio.get_event_loop()
    for attempt in range(20):
        udp.sendto(f"set>server={laptop_ip}:{server_port};".encode(), (device_ip, udp_port))
        try:
            reply = await asyncio.wait_for(loop.sock_recv(udp, 256), timeout=2.0)
            print(f"discovery reply: {reply!r}")
            break
        except asyncio.TimeoutError:
            print(f"  ...waiting for device (attempt {attempt + 1})")
    udp.close()

    reader, writer = await asyncio.wait_for(accepted, timeout=10.0)
    peer = writer.get_extra_info("peername")
    print(f"reverse TCP from {peer}")

    # consume the connect heartbeat
    header_bytes = await reader.readexactly(HEADER_SIZE)
    header = decode_header(header_bytes)
    await reader.readexactly(header.payload_len)
    print(f"heartbeat: fc={header.fcode} tid={header.tid:#x}")
    return server, reader, writer


async def run(address: str, serial_port: str, udp_port: int, no_fake: bool) -> int:
    device_ip = socket.gethostbyname(address)
    probe_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe_sock.connect((device_ip, 9))
    laptop_ip = probe_sock.getsockname()[0]
    probe_sock.close()
    print(f"device={device_ip} laptop={laptop_ip} mode={'real-inverter' if no_fake else 'fake-inverter'}")

    # --no-fake: the bridge's UART is wired to a REAL inverter, so the laptop only
    # plays the HA side over Wi-Fi and does not open the serial port at all.
    fake = None if no_fake else FakePi30Serial(serial_port)
    if fake is not None:
        fake.start()
    try:
        server, reader, writer = await open_reverse_link(device_ip, laptop_ip, udp_port)
        transport = LiveBenchTransport(reader, writer, device_ip)
        # Identity advertised by the bridge: devcode 0x0000, collector_addr 0x01.
        target = ProbeTarget(devcode=0x0000, collector_addr=0x01, device_addr=0)

        driver = Pi30Driver()
        started = time.monotonic()
        inverter = await driver.async_probe(transport, target)
        print(f"probe took {time.monotonic() - started:.1f}s, commands: {transport.commands}")

        if inverter is None:
            print("PROBE FAILED: the bridge answered but the inverter was not recognized.")
            if no_fake:
                print("  Check: inverter powered, UART TX/RX (and common ground), 2400 8N1, "
                      "and that nothing else is polling the bus.")
            return 1

        print(f"model_name:           {inverter.model_name}")
        print(f"variant/profile:      {getattr(inverter, 'variant_key', '?')} / {inverter.profile_name}")
        print(f"register_schema_name: {inverter.register_schema_name}")
        print(f"serial_number:        {inverter.details.get('serial_number', '?')}")

        values = await driver.async_read_values(transport, inverter)
        interesting = {
            key: values[key]
            for key in ("operating_mode", "output_active_power", "battery_voltage",
                        "pv_input_power", "grid_voltage", "ac_output_voltage")
            if key in values
        }
        print(f"read_values ({len(values)} keys): {interesting}")

        writer.close()
        server.close()

        if no_fake:
            # Real inverter: we cannot assert specific values; success = recognized
            # model + a non-empty live read.
            ok = bool(inverter.model_name) and len(values) > 0
            print("COLLECTOR + INVERTER OK (real hardware)" if ok else "READ FAILED")
            return 0 if ok else 1

        ok = inverter.model_name == "PowMr 4.2kW" and values.get("output_active_power") == 924
        print("DETECTION OK" if ok else "DETECTION MISMATCH")
        return 0 if ok else 1
    finally:
        if fake is not None:
            fake.stop()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", default="eybond-bench.local")
    parser.add_argument("--serial", default="/dev/ttyUSB0")
    parser.add_argument("--udp-port", type=int, default=58899)
    parser.add_argument("--no-fake", action="store_true",
                        help="bridge is wired to a REAL inverter; do not start the fake on the serial port")
    args = parser.parse_args()
    return asyncio.run(run(args.address, args.serial, args.udp_port, args.no_fake))


if __name__ == "__main__":
    raise SystemExit(main())
