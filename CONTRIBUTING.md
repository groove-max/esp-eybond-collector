# Contributing to ESP EyeBond Collector

Thanks for your interest in improving the firmware. This document covers the architecture, the protocol contract it implements, and the validation workflow.

If you're a user looking to install or troubleshoot the bridge, the [README](README.md) is the right place to start.

---

## Project Philosophy

The firmware is split into two strictly separated layers:

```
platform-independent protocol core (plain C++)  →  thin ESPHome glue (Arduino)
```

The most important rule:

> **All protocol logic lives in the core; the glue does I/O only.**

The core (`namespace eybond`) includes **nothing** from Arduino or ESPHome — this is enforced by the host build. It is event-driven ("sans-IO"): the platform feeds it events (UDP datagram, TCP data, UART bytes, time ticks) and the core returns actions through an `Actions` interface. That is why it compiles with plain `g++` and is fully covered by host tests, and why a second platform (e.g. an ESP-IDF socket layer) would only need a new glue file, not protocol changes.

---

## Architecture

```
components/eybond_collector/     # flat layout: ESPHome does not copy a component's subdirectories
├── frame.{h,cpp}                # core: 8-byte header >HHHBB, frame assembly
├── stream_splitter.{h,cpp}      # core: TCP stream parsing — AT lines vs binary frames
├── at_handler.{h,cpp}           # core: AT dictionary (DTUPN/WFSS/FWVER/...), byte-exact replies
├── discovery.{h,cpp}            # core: set>server=IP:PORT; → rsp>server=2; + PN from MAC
├── profile.h                    # core: virtual collector identity
├── core.{h,cpp}                 # core: orchestrator — heartbeat, FC dispatch, UART bridge, reconnect
├── eybond_collector.{h,cpp}     # ESPHome glue (Arduino: WiFiUDP/WiFiClient + uart)
├── baud_select.h                # ESPHome glue: runtime baud-rate select entity
├── __init__.py                  # ESPHome config schema
└── select.py                    # ESPHome select platform schema
```

---

## Protocol Contract

The behavioral specification is the **EyeBond Local** integration's own reference
fake collector (`fake_collector.py` / its `fake_collector_lib.py` helper). The
firmware is a port of that logic. When those files change, re-run `cross_check.py`.

- **Discovery**: UDP `58899`, `set>server=IP:PORT;` → reply `rsp>server=2;` + reverse TCP to Home Assistant.
- **Heartbeat**: FC=1 on connect, then periodic (default 60 s); payload is the PN truncated to 14 bytes. A server FC=1 is answered with its own `tid`.
- **FC=2** (query collector): the core answers parameters it knows from its own state (PN, firmware, current endpoint, baud); the glue answers the rest (hardware, IP, SSID, RSSI, Wi-Fi scan list). Unknown parameters → `01 <param>` (refused), like the factory fake.
- **FC=3** (set collector): writes are *staged* and committed together. Param 41/43 stage the target Wi-Fi SSID/password; param 21 stages the server endpoint; param 29 (`system_operation`) **commits everything staged** — endpoint and/or Wi-Fi. If nothing is staged, param 29 is treated as **Restart Collector** and the reboot is delayed until after the response frame is sent. Param 34 (baud) applies immediately. Everything else is refused `01 <param>`. The committed endpoint is persisted to NVS (so the bridge reconnects to the same HA across reboots / can be moved to another HA); incoming discovery still overrides it live. `AT+CLDSRVHOST1=` applies an endpoint immediately (no staging) and retargets the link.
- **FC=4** (forward): payload → UART as-is; the inverter reply (end detected by a `response_gap` of silence) → frame with the same `tid`/`devcode`/`devaddr`. **Inverter silence → no reply at all** — the integration distinguishes a timeout from a zero-filled answer, so this is load-bearing.
- **AT lines** (`AT+...\n`) are interleaved with binary frames on the same TCP stream; they are told apart by the 3-byte `AT+` prefix.
- Inverter requests are serialized: one in flight, with a `command_spacing` pause (factory ~850 ms) between commands.

### Identity

The PN is synthetic: `V00` + the 15-digit decimal MAC → the PN18 format `^[A-Z]\d{17}$` the integration accepts. Override with `pn:` (the schema validator accepts synthetic values only: a letter + 13/17 digits). `devcode` defaults to `0x0000`, which the integration handles through its `unknown_0x0000` profile.

### AT+VDTU capability probe

The firmware answers `AT+VDTU?` with a capability string:

```
AT+VDTU:esp-collector,<semver>;features=local_only,no_cloud,wifi_params,endpoint_write,reboot;uart=<baud,data,stop,parity>;spacing_ms=<n>;queue=<n>
```

EyeBond Local probes this on link-up. The `esp-collector,` prefix identifies a virtual bridge so cloud-only flows can be hidden and an honest device name/version can be shown. Factory collectors do not return the prefix. Features advertise optional collector-side operations such as endpoint writes and restart. For every other command the bridge behavior remains compatible with the factory collector path.

---

## Validation Pipeline

Run the checks below from fastest to slowest. The first three are hardware-free
and are the normal validation path for public contributions.

> Test 1 needs only `g++`. Tests 2–3 import the **EyeBond Local** integration, so
> check out [`ha-eybond-local`](https://github.com/groove-max/ha-eybond-local) as a
> sibling directory of this repo. Run the Python/ESPHome commands in an environment
> with `esphome` installed and on `PATH` (an activated virtualenv is easiest).

```bash
# 1. Core host unit tests (plain g++, no hardware)
cd host_tests && make test

# 2. Byte-for-byte cross-check against the integration's own builders
python3 cross_check.py

# 3. Linux end-to-end: the real core (host_sim: POSIX sockets + a PTY in place of
#    the UART) against the integration's protocol code — discovery, heartbeat, AT,
#    the FC4 bridge, and timeouts
python3 e2e_sim.py

# host_sim can also run standalone — the real EyeBond Local integration (in Home
# Assistant) can talk to it, with a fake inverter on the PTY:
make build/host_sim && ./build/host_sim --udp-port 58899

# 4. Optional: compile the release presets
esphome compile examples/esp8266-d1-mini.yaml  # ESP8266 release preset
esphome compile examples/esp32-devkit.yaml     # ESP32 release preset
```

### Optional maintainer hardware checks

The repository still contains a few low-level scripts that can help maintainers
test a live board, but they are not part of the public validation contract and
they expect a local hardware setup.

One useful setup is an ESP board connected to the host by USB only. The same USB
bridge that flashes the board exposes UART0, so the host can play the inverter:
`host_tests/fake_pi30_serial.py` answers PI30 commands on `/dev/ttyUSB0`
(`2400 8N1`) with values from the integration's PI30 driver tests.
`host_tests/bench_e2e.py` can then play the Home Assistant side (discovery,
reverse TCP, AT, FC4, collector parameters, baud switching) using the
integration's own builders.

`host_tests/bench_probe.py` runs the integration's real `Pi30Driver.async_probe()` against the live device — the same detection step the Home Assistant config flow performs.

Hardware-check gotchas worth knowing:

- The first flash is over USB; every later flash is OTA, leaving the USB port free for the fake inverter.
- Opening the serial port pulses DTR/RTS and reboots the board, so `bench_e2e` retries discovery until Wi-Fi is back (~10–15 s).
- The ESP8266 boot ROM prints at 74880 baud, which looks like garbage at 2400 and can prepend itself to the first PI30 request — the fake inverter searches for a CRC-valid suffix.
- If a live Home Assistant also owns the bench device, its periodic discovery can steal the bridge mid-test (last redirect wins); test steps re-acquire the link and retry.

---

## Safety Invariants

These are inherited from the parent project and must not be weakened:

- **One master per bus.** While the bridge is active, no other Modbus/serial master may poll the inverter in parallel.
- **The bridge never writes to the inverter on its own** — it only forwards what the integration sent.
- **No real identifiers in tracked files** — no real PNs/SNs/passwords/SSIDs in code, tests, or example configs. Synthetic only (`V00000200000000001`-style). Secrets live in `secrets.yaml`, which is git-ignored.
- **Honest timeouts** — never synthesize a reply on the inverter's behalf.

---

## Current Limitations

- Arduino framework only — ESP8266, ESP32-arduino, and **BK72xx/RTL87xx via LibreTiny**
  (the same glue compiles on all three; LibreTiny defines `USE_ARDUINO`). ESP-IDF would be
  a separate adapter (the core is platform-independent; only a socket glue layer is needed).
- **LibreTiny (BK72xx) caveats:** its `UARTComponent` has no `load_settings()`, so runtime
  baud/format change (select / `AT+UART=` / FC=3 param 34) is unavailable — baud/framing are
  fixed by YAML there. Software serial is chip-dependent, so the inverter must be on a
  hardware UART. `WiFiClient::setNoDelay` and `IPAddress::toString` are absent and are guarded
  out (`#ifndef USE_LIBRETINY` / manual octet formatting).
- `AT+SYST` returns an empty value (no time source); attach a `time:` component if you want a real timestamp.
- `WiFiClient::connect` is blocking (a rare event, but it can briefly stall the loop on connect).
- The SMG/Modbus path is implemented through the same transparent bridge as PI30 and passes simulation, but has not yet been verified on real SMG hardware.

---

## Roadmap

- **Integration-side support:** EyeBond Local detects the `esp-collector,` VDTU prefix,
  gates cloud-only flows, forces the collector operation mode to "HA only" for a bridge,
  and shows an honest device name.
- **ESP32 + ESP-IDF adapter** — a socket glue layer over `CollectorCore::Actions`.
- **BLE provisioning:** ESP8266 has no BLE radio. ESP32 builds can expose the bridge's BLE Wi-Fi provisioning path when `esp32_ble_server:` and `ble_provisioning: true` are enabled; keep an eye on firmware image size because BLE + Wi-Fi can exceed small default partitions.
