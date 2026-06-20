#!/usr/bin/env bash
# Full autonomous bench iteration: host tests -> compile -> OTA flash -> hardware E2E.
# Usage: ./bench_cycle.sh [device-address]   (default eybond-bench.local)
# First-ever flash must be done over USB instead:
#   .venv/bin/esphome run examples/bench-esp8266.yaml --device /dev/ttyUSB0
set -euo pipefail
cd "$(dirname "$0")"

ADDRESS="${1:-eybond-bench.local}"

echo "=== 1/4 host unit tests ==="
make -C host_tests test

echo "=== 2/4 cross-check vs integration python ==="
(cd host_tests && ../../.venv/bin/python3 cross_check.py)

echo "=== 3/4 compile + OTA flash ==="
.venv/bin/esphome run examples/bench-esp8266.yaml --device "$ADDRESS" --no-logs

echo "=== 4/4 hardware E2E (fake PI30 on /dev/ttyUSB0) ==="
.venv/bin/python3 host_tests/bench_e2e.py --address "$ADDRESS"

echo "=== bench cycle PASSED ==="
