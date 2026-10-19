# Releasing

Releases are tag-driven.

## CI checks

Every pull request and push runs:

- host-side C++ tests;
- platform IO contract checks (offline Wi-Fi apply and partial TCP writes);
- example YAML parsing;
- markdown link checks;
- ESPHome compile for the two release presets:
  - `esp8266-d1-mini`
  - `esp32-devkit`
- ESPHome compile checks for the YAML-only BK72xx and ESP32 BLE profiles.

## Release artifacts

Pushing a tag like `v0.1.0` builds:

- `esp8266-d1-mini.factory.bin`
- `esp32-devkit.factory.bin`
- `manifest-esp8266-d1-mini.json`
- `manifest-esp32-devkit.json`
- GitHub Pages web installer content
- a `SHA256SUMS` file for the published binaries and manifests.

The GitHub release description is rendered from the matching version section in
`CHANGELOG.md`. Keep that section useful to people upgrading the firmware; the
release workflow rejects a missing or empty section.

BLE and BK72xx are not shipped as web binaries in the first release. They remain YAML-based profiles.

## Local release build

Run this with `esphome` on `PATH` (e.g. an activated virtualenv); the script finds
it by default. Pass `--esphome /path/to/esphome` to override.

```bash
python3 tools/build_web_release.py \
  --compile \
  --local-source \
  --version v0.1.5
```

Output:

- `dist/web-release/web/index.html`
- `dist/web-release/web/manifest-*.json`
- `dist/web-release/web/firmware/*.bin`

## Before tagging

1. Bump `BRIDGE_VERSION` in `components/eybond_collector/profile.h`. It is embedded in
   the FC=2 param-6 hardware-version marker (`esp-collector/<version>/<platform>`).
   Mirror the same project version in the three release examples and three files under
   `packages/`; `tools/build_web_release.py` rejects a metadata or release-tag
   version mismatch before building.
2. Add the new version's entry to [`CHANGELOG.md`](../CHANGELOG.md).
3. Confirm the release examples still match the documented pinout.
4. Confirm `examples/secrets.yaml` is not committed.
5. Run host tests (`make -C host_tests test`) and `cross_check.py`.
6. Run a local web release build if ESPHome dependencies are available.
7. Create and push the release tag (`git tag vX.Y.Z && git push origin vX.Y.Z`) — the
   release workflow then builds the binaries and deploys the web installer.

## Runtime qualification for 0.1.11

In addition to the compile matrix, verify on an own test device: OTA reconnect,
unchanged Wi-Fi/PN/UART settings, fresh inverter values, staged endpoint readback,
one real framed restart, new-session identity qualification, and restoration of
the original endpoint. Keep hardware claims separate: an ESP8266 run does not
qualify ESP32 BLE radio behavior, and a compile pass is not a hardware test.

The UART bridge has no inverter transaction identifiers. After an uncertain
exchange it discards queued commands and requires a full `response_timeout`
of UART silence (3 seconds by default) before accepting new requests. New bytes
extend that interval; TCP reconnect does not clear it. Choose a timeout covering
the actual inverter response latency. This bounds recovery under that timing
contract; it cannot identify arbitrarily delayed bytes or make raw UART writes
safe to replay. No empty response or invented inverter ACK is sent.

AT endpoint writes now stage settings; `AT+INTPARA=29,1` applies them. Both AT
and framed callers can read the accepted endpoint before Apply. `AT+RESET=S`
is a real soft restart and refuses pending settings. FC3 parameter29 remains
the compatible apply-or-reboot operation, so clients must inspect parameter30
before offering a standalone restart. ACK means acceptance, not verified return.
