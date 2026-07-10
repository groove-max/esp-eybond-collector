# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and this project adheres to
[Semantic Versioning](https://semver.org/).

## [0.1.10] - 2026-07-10

### Added
- **Unified local configuration page.** `/` now combines Wi-Fi provisioning,
  Home Assistant endpoint settings, complete inverter UART framing and web OTA.
  It is available on both the normal LAN address and `192.168.4.1` in setup mode.
- **Complete persisted UART framing.** ESP8266 and ESP32 now persist data bits,
  parity and stop bits together with baud rate. Existing baud-only preferences
  migrate automatically. On BK72xx, UART settings remain build-time-only and can
  be changed only by rebuilding and reflashing the firmware.
- **Hardware identity overview.** The page shows the compiled TX/RX and RS485
  direction/status/communication LED pins, collector PN and ESPHome hostname.
  Build-time fields remain read-only until a platform-safe boot-time
  hardware-profile mechanism is available.
- **Interactive Wi-Fi scan.** Nearby networks retain the stock captive-portal
  behavior: SSID, a signal-strength indicator and lock state are displayed, and
  selecting a row copies its SSID into the Wi-Fi form. Exact RSSI is available
  from the indicator tooltip. The configured network remains visible and is
  clearly marked as connected or currently unavailable.
- **Configurable collector PN.** The default PN remains protected by a checked
  toggle. Disabling it allows a validated custom PN; re-enabling it restores the
  default. Identity changes are persisted and applied after a safe restart.
- **Local page translations.** English, Ukrainian and Russian can be selected on
  the device page. The browser remembers the selected language.
- **ESPHome Dashboard adoption.** Ready-made ESP8266, ESP32 and BK72xx builds
  publish project metadata and a board-specific `dashboard_import` package over
  mDNS. The Dashboard can create a local YAML for a device flashed from a
  prebuilt binary; no server-specific ownership key is required.
- **UART / RS485 diagnostics.** The local page shows passive live counters for
  transmitted bytes and requests, total physical receive bytes and the age of
  the last TX/RX activity. Developer-only response/stale counters remain in the
  diagnostics JSON. Counters reset at boot and never influence transport behavior.
- **Dark and light themes.** The local page uses the dark theme by default and
  remembers the user's theme toggle alongside the language preference.

### Fixed
- The always-on local web server is initialized only after the network stack is
  ready. Starting AsyncTCP from the component's pre-Wi-Fi setup priority caused
  an ESP32 boot loop; captive-portal registration remains available before Wi-Fi.

### Changed
- Collector identity, Home Assistant endpoint and UART framing now have separate
  save actions. PN changes require explicit browser confirmation and explain the
  Home Assistant re-add requirement.
- UART persistence wording now explicitly states that settings are stored in
  non-volatile device memory and used after every restart.
- `BRIDGE_VERSION` bumped to `0.1.10`.

## [0.1.8] - 2026-07-04

### Fixed
- **Changing the server endpoint over FC=3 no longer drops the acknowledgement.**
  A staged endpoint (param 21) committed with param 29 used to be applied
  synchronously, closing the TCP session before the FC=3 acknowledgement for that
  very request was flushed — Home Assistant saw the write as failed even though it
  had worked. The apply is now deferred (~750 ms) until the acknowledgement is on
  the wire, mirroring the existing deferred Wi-Fi apply.
- **A retried commit during the deferred-apply window no longer reboots the bridge.**
  Param 29 clears the staged values immediately but applies them a moment later; a
  retry arriving inside that window found nothing staged and was misread as a bare
  restart request. An in-flight deferred apply now acknowledges the retry as
  success instead.

### Changed
- `BRIDGE_VERSION` bumped to `0.1.8`.

## [0.1.7] - 2026-07-02

### Added
- **Separate STATUS and COM LEDs, like a factory collector.** A new `com_led_pin`
  drives an inverter-communication activity LED alongside the existing `status_led_pin`
  connection-state LED. With both pins, each LED has one job. With only `status_led_pin`
  (single-LED dev boards) that LED shows both — solid when the bridge is connected and
  idle, flickering while the inverter is talked to. With only `com_led_pin` it is a pure
  activity light. Each pin takes the full pin schema, so per-pin `inverted:` is available.

### Changed
- **The activity (COM) indicator now flickers instead of holding lit, and reflects real
  inverter transactions only.** It is armed by inverter UART traffic — a forward and the
  reply *while that forward is awaiting a response* — never by HA↔bridge TCP chatter, and
  it *toggles* (capped at ~10 Hz) rather than staying lit for an activity window, so a
  heavy read/write load reads as a fast flicker and never a solid glow.
- **Idle-line RX noise no longer lights the COM LED.** Bytes arriving on the inverter UART
  when no forward is pending (electrical noise on a floating/unwired RX, or an idle bus)
  are ignored, so a bridge connected to HA before its inverter is wired or powered on
  stays solid instead of flickering constantly.
- `BRIDGE_VERSION` bumped to `0.1.7`.

## [0.1.6] - 2026-07-01

### Changed
- **Bridge detection no longer uses an AT probe.** EyeBond Local now recognizes a
  virtual bridge from the FC=2 hardware-version parameter (registry id 6), answered as
  `esp-collector/<version>/<platform>`. This is PN-independent (a user may set a custom
  PN, and a real collector's PN could collide) and timeout-free (factory collectors
  reject unknown AT commands by timing out). The `<platform>` suffix (`ESP8266` /
  `ESP32` / `BK72xx/RTL87xx`) also tells the integration whether runtime UART
  reconfiguration is available.
- **The connected-state status LED is now an activity light.** While the bridge is
  connected the LED stays dark when idle and flickers in time with data moving between
  Home Assistant, the bridge, and the inverter, instead of being solid on. The
  not-connected states are unchanged (fast blink = no Wi-Fi, slow blink = no bridge
  link). LED inversion remains config-driven through the pin's `inverted:` property.
- **The default reported firmware version is now the bridge's own version, not the
  factory logger's.** `AT+FWVER` / FC=2 param 5 previously answered with a hardcoded
  original-collector firmware string; the bridge has no relation to that logger, so it
  now reports `BRIDGE_VERSION` by default. A future release will let a reflashed unit
  set the original value via YAML for the original-cloud path.
- `BRIDGE_VERSION` bumped to `0.1.6`.

### Removed
- The `AT+VDTU` capability probe. Real collectors do not implement it (they time out on
  it) and no public integration relied on it, so the firmware now treats `AT+VDTU?` as
  an unknown command. Detection uses the FC=2 hardware-version marker described above.

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

[0.1.10]: https://github.com/groove-max/esp-eybond-collector/compare/v0.1.8...v0.1.10
[0.1.8]: https://github.com/groove-max/esp-eybond-collector/compare/v0.1.7...v0.1.8
[0.1.7]: https://github.com/groove-max/esp-eybond-collector/compare/v0.1.6...v0.1.7
[0.1.6]: https://github.com/groove-max/esp-eybond-collector/compare/v0.1.5...v0.1.6
[0.1.5]: https://github.com/groove-max/esp-eybond-collector/compare/v0.1.4...v0.1.5
[0.1.0]: https://github.com/groove-max/esp-eybond-collector/releases/tag/v0.1.0
[0.1.4]: https://github.com/groove-max/esp-eybond-collector/releases/tag/v0.1.4
