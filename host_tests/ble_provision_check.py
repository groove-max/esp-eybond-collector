#!/usr/bin/env python3
"""Drive the bridge's BLE provisioning server with the integration's own client.

This is the BLE analogue of cross_check.py: instead of reimplementing the
protocol, it loads the ha-eybond-local integration's smartess_ble.py and runs
the exact SmartEssBleProvisioner flow the HA config flow uses, against the real
firmware over the laptop's Bluetooth adapter. A green run means the integration's
BLE pairing works end to end with our firmware.

Usage:
    .venv/bin/python host_tests/ble_provision_check.py "ExampleSSID" "ExamplePassword"
    .venv/bin/python host_tests/ble_provision_check.py --scan-only
"""

from __future__ import annotations

import argparse
import asyncio
import importlib.util
import logging
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SMARTESS_BLE = os.path.normpath(
    os.path.join(
        HERE,
        "..",
        "..",
        "ha-eybond-local",
        "custom_components",
        "eybond_local",
        "collector",
        "smartess_ble.py",
    )
)


def _load_smartess_ble():
    spec = importlib.util.spec_from_file_location("smartess_ble", SMARTESS_BLE)
    module = importlib.util.module_from_spec(spec)
    # Register before exec: @dataclass(slots=True) resolves cls.__module__ via
    # sys.modules during class creation, which fails for an unregistered module.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


async def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ssid", nargs="?", help="Wi-Fi SSID to provision")
    parser.add_argument("password", nargs="?", help="Wi-Fi password")
    parser.add_argument("--scan-only", action="store_true", help="discover and exit")
    parser.add_argument("--timeout", type=float, default=8.0, help="BLE scan timeout")
    parser.add_argument("--verbose", "-v", action="store_true")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
    )

    sb = _load_smartess_ble()

    print(f"== Discovering SmartESS BLE collectors ({args.timeout:.0f}s) ==")
    scanner = sb.BleakSmartEssBleScanner()
    candidates = await scanner.discover_candidates(timeout=args.timeout)
    if not candidates:
        print("FAIL: no SmartESS BLE candidates found (is the bridge advertising?)")
        return 2
    for c in candidates:
        print(f"  candidate: pn={c.local_pn} name={c.preferred_name!r} addr={c.address}")

    # SAFETY: only ever talk to our synthetic V00 bridge. Real eybond collectors
    # advertise E5… PNs; provisioning one would change real hardware's Wi-Fi, so
    # we refuse to target anything but a V00 bridge (scan-only may list all).
    v00 = [c for c in candidates if c.local_pn.startswith("V00")]
    if args.scan_only:
        return 0
    if not v00:
        print("FAIL: no V00 bridge found. Refusing to provision a non-V00 device "
              "(could be a real collector). Wait for the bridge to boot, then retry.")
        return 2
    target = v00[0]
    print(f"== Selected: pn={target.local_pn} addr={target.address} ==")

    link = sb.BleakSmartEssBleLink(target.address, device=target.device)
    session = sb.SmartEssBleSession(link)
    layout = await session.connect()
    print(f"== Connected. GATT layout: {layout.name} (service {layout.service_uuid}) ==")

    try:
        prov = sb.SmartEssBleProvisioner(session)

        info = await prov.query_device_info()
        print(
            f"== Device info: fw={info.fw_version} at={info.at_version} "
            f"branch={info.branch.value} requires_restart={info.requires_restart} =="
        )

        try:
            nets = await prov.scan_wifi_networks()
            print(f"== Wi-Fi scan: {len(nets)} network(s) ==")
            for n in nets[:10]:
                print(f"  [{n.ssid}, {n.signal}]")
        except sb.SmartEssBleError as exc:
            print(f"  (wi-fi scan: {exc})")

        if not args.ssid or not args.password:
            print("No SSID/password given; skipping provisioning (scan complete).")
            return 0

        print(f"== Provisioning onto SSID={args.ssid!r} ==")
        result = await prov.provision_wifi(
            ssid=args.ssid, password=args.password, info=info
        )
        print(
            f"== Result: branch={result.branch.value} outcome={result.outcome.value} "
            f"status={result.status_code} =="
        )
        if result.raw_response:
            print(f"   raw: {result.raw_response!r}")

        ok = result.outcome.value in ("success", "degraded")
        print("RESULT:", "PASS" if ok else "FAIL", f"({result.outcome.value})")
        return 0 if ok else 1
    finally:
        await session.disconnect()
        print("== Disconnected ==")


if __name__ == "__main__":
    try:
        sys.exit(asyncio.run(main()))
    except KeyboardInterrupt:
        sys.exit(130)
