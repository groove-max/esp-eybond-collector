# Releasing

Releases are tag-driven.

## CI checks

Every pull request and push runs:

- host-side C++ tests;
- example YAML parsing;
- markdown link checks;
- ESPHome compile for the two release presets:
  - `esp8266-d1-mini`
  - `esp32-devkit`

## Release artifacts

Pushing a tag like `v0.1.0` builds:

- `esp8266-d1-mini.factory.bin`
- `esp32-devkit.factory.bin`
- `manifest-esp8266-d1-mini.json`
- `manifest-esp32-devkit.json`
- GitHub Pages web installer content

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

1. Bump the version: `BRIDGE_VERSION` in `components/eybond_collector/profile.h` is the
   single source of truth. It is embedded in the FC=2 param-6 hardware-version marker
   (`esp-collector/<version>/<platform>`) that EyeBond Local keys the bridge on; no host
   test hard-codes it, so nothing else needs to move with it.
2. Add the new version's entry to [`CHANGELOG.md`](../CHANGELOG.md).
3. Confirm the release examples still match the documented pinout.
4. Confirm `examples/secrets.yaml` is not committed.
5. Run host tests (`make -C host_tests test`) and `cross_check.py`.
6. Run a local web release build if ESPHome dependencies are available.
7. Create and push the release tag (`git tag vX.Y.Z && git push origin vX.Y.Z`) — the
   release workflow then builds the binaries and deploys the web installer.
