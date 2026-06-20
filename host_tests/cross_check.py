#!/usr/bin/env python3
"""Byte-for-byte cross-check: C++ core output vs the integration's own builders.

Builds host_tests/vector_dump (via make) and compares every emitted vector with
the same bytes produced by ha-eybond-local's protocol.py / fake_collector_lib.py.
Run with the integration venv python:
    ../.venv/bin/python3 cross_check.py
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
INTEGRATION = HERE.parent.parent / "ha-eybond-local"
sys.path.insert(0, str(INTEGRATION))
sys.path.insert(0, str(INTEGRATION / ".local" / "tools"))

from custom_components.eybond_local.collector.protocol import (  # noqa: E402
    build_collector_request,
    encode_header,
)
from fake_collector_lib import (  # noqa: E402
    CollectorProfile,
    build_at_reply,
    build_unsolicited_heartbeat,
)

PN = "V00000200000000001"
PROFILE = CollectorProfile(pn=PN, uart="2400,8,1,NONE")
CLOUD_ENDPOINT = "192.0.2.10,8899,TCP"


def expected_vectors() -> dict[str, bytes]:
    vectors: dict[str, bytes] = {
        "encode_header": encode_header(0x1234, 0x0994, 10, 0x10, 4),
        "heartbeat": build_unsolicited_heartbeat(tid=0x8001, pn=PN, devcode=0x0000, collector_addr=1),
        "fc2_param5": build_collector_request(
            7,
            bytes((0, 5)) + PROFILE.firmware_version.encode("ascii"),
            devcode=0x0994,
            collector_addr=0x10,
            fcode=2,
        ),
        "at_write_ack": build_at_reply("CLDSRVHOST1", profile=PROFILE, cloud_endpoint=CLOUD_ENDPOINT, write_ack=True),
        "pn_synth": b"V00" + str(0xDEADBEEF0042).zfill(15).encode("ascii"),
    }
    for command in (
        "DTUPN", "ATVER", "ENUPMODE", "WFSS", "UART", "DTUTYPE",
        "FWVER", "CLDSRVHOST1", "HTBT", "LINK", "INTPARA49", "UNKNOWNCMD",
    ):
        vectors[f"at_{command}"] = build_at_reply(command, profile=PROFILE, cloud_endpoint=CLOUD_ENDPOINT)
    # SYST depends on wall clock; the dump uses a fixed string, so compare statically.
    vectors["at_SYST"] = b"AT+SYST:20260613120000\r\n"
    return vectors


def main() -> int:
    subprocess.run(["make", "build/vector_dump"], cwd=HERE, check=True, capture_output=True)
    output = subprocess.run([HERE / "build" / "vector_dump"], check=True, capture_output=True, text=True).stdout

    actual: dict[str, bytes] = {}
    for line in output.splitlines():
        name, _, hex_text = line.partition("\t")
        actual[name] = bytes.fromhex(hex_text)

    expected = expected_vectors()
    failures = 0
    for name, expected_bytes in sorted(expected.items()):
        got = actual.get(name)
        if got != expected_bytes:
            failures += 1
            print(f"MISMATCH {name}\n  cpp    {got.hex() if got is not None else '<missing>'}\n  python {expected_bytes.hex()}")
    extra = set(actual) - set(expected)
    if extra:
        failures += 1
        print(f"vectors without python reference: {sorted(extra)}")

    print(f"{len(expected)} vectors, {failures} mismatches")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
