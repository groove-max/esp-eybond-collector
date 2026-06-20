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

```bash
python3 tools/build_web_release.py \
  --compile \
  --local-source \
  --esphome .venv/bin/esphome \
  --version v0.1.0
```

Output:

- `dist/web-release/web/index.html`
- `dist/web-release/web/manifest-*.json`
- `dist/web-release/web/firmware/*.bin`

## Before tagging

1. Confirm the release examples still match the documented pinout.
2. Confirm `examples/secrets.yaml` is not committed.
3. Run host tests.
4. Run a local web release build if ESPHome dependencies are available.
5. Create and push the release tag.
