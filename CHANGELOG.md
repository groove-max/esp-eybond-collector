# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and this project adheres to
[Semantic Versioning](https://semver.org/).

## [0.1.5] - 2026-06-23

### Added
- Fallback setup access point: when the YAML leaves `wifi: ap:` empty (`ap: {}`),
  the bridge now names the setup network after its synthetic collector PN
  (for example `V00…`) instead of a fixed string. A user-provided `ap: ssid:` is
  still respected.

### Fixed
- **ESP32 BLE example (`examples/esp32-ble.yaml`) no longer overflows the flash
  partition and boot-loops.** BLE (Bluedroid) + Wi-Fi plus `api:`, `captive_portal:`
  and the baud `select:` exceeded the default 4 MB app slot (the real image is
  ~12 KB larger than ESPHome's reported "Flash %"), so the board silently looped on
  boot. Those three are now omitted (BLE provisioning replaces the captive portal;
  the integration uses its own protocol, not the ESPHome API); OTA still works.
- The `ble_provisioning` config-validation error pointed at a non-existent example
  file (`esp32-c3-ble.yaml`); it now references `examples/esp32-ble.yaml`.

### Changed
- `BRIDGE_VERSION` bumped to `0.1.5` (advertised in the `AT+VDTU` capability string).
- Documentation: clarified the BLE-profile flash-size constraints in `FLASHING.md`
  and `FLASHING.uk.md`; removed maintainer-local paths from `CONTRIBUTING.md` and
  `docs/RELEASING.md`; added this changelog.
- CI / release workflows: updated GitHub Actions to current major versions.

## [0.1.0] – [0.1.4] - 2026-06-21 … 2026-06-23

Initial public releases of the virtual eybond/SmartESS Wi-Fi collector as an
ESPHome external component for **ESP8266, ESP32 (incl. C3) and BK72xx/LibreTiny**,
fully compatible with the [EyeBond Local](https://github.com/groove-max/ha-eybond-local)
Home Assistant integration. Highlights:

- UDP `58899` discovery + reverse TCP `8899`, periodic heartbeats, and the AT
  command dictionary, with a byte-for-byte cross-check against the integration.
- FC=2 / FC=3 / FC=4 with a transparent inverter UART bridge and honest timeouts
  (inverter silence → no reply, never a synthesized answer).
- Synthetic, neutral collector identity (`V00…` PN derived from the MAC).
- Runtime inverter-UART baud/format change (select entity, `AT+UART=`, FC=3 param 34).
- Writable, NVS-persisted reverse-TCP server endpoint.
- ESP32 BLE Wi-Fi provisioning compatible with the SmartESS Bluetooth pairing.
- Status LED indication and the `AT+VDTU` virtual-bridge capability probe.
- Web installer (GitHub Pages) for the ESP8266 and ESP32 presets.

[0.1.5]: https://github.com/groove-max/esp-eybond-collector/compare/v0.1.4...v0.1.5
[0.1.0]: https://github.com/groove-max/esp-eybond-collector/releases/tag/v0.1.0
[0.1.4]: https://github.com/groove-max/esp-eybond-collector/releases/tag/v0.1.4
