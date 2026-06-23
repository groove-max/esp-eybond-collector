# Flashing & First Setup

Українською: [Прошивка та перше налаштування](FLASHING.uk.md).

[![Flash from browser](https://img.shields.io/badge/Flash%20from%20browser-ESP%20Web%20Tools-03a9f4)](https://groove-max.github.io/esp-eybond-collector/)

This guide takes you from an empty ESP board to a working ESP EyeBond Collector bridge in Home Assistant.

You do not need a factory SmartESS / EyeBond collector.

## Before you start

You need:

- an ESP8266, ESP32, or supported BK72xx board;
- a suitable adapter for the inverter port;
- a USB cable for the first flash;
- the [EyeBond Local](https://github.com/groove-max/ha-eybond-local) Home Assistant integration.

Pick the adapter by inverter port:

| Inverter port | Adapter |
|---|---|
| TTL UART | Direct UART wiring. Use a level shifter/divider on ESP RX for 5 V TTL. |
| RS232 | MAX3232 RS232↔TTL converter. |
| RS485 | MAX485 / MAX3485 RS485 transceiver. |

If your inverter already worked with another ESP firmware or cable, use the same inverter port and the same type of adapter.

## Option A — Flash from the browser

[Open the web installer](https://groove-max.github.io/esp-eybond-collector/)

Use this option when your board and wiring match one of the release presets.
It does not require installing ESPHome or creating `examples/secrets.yaml`.

Open the project web installer, plug the ESP board into USB, and select the matching preset.

| Preset | Board | TX | RX | RS485 DE/RE | Default UART speed |
|---|---|---|---|---|---|
| `esp8266-d1-mini` | ESP8266 D1 mini compatible | GPIO13 / D7 | GPIO12 / D6 | GPIO14 / D5 | `9600` |
| `esp32-devkit` | ESP32 DevKit compatible | GPIO17 | GPIO16 | GPIO4 | `9600` |

Both presets can be used for TTL, RS232, or RS485:

- TTL: connect TX/RX/GND and leave DE/RE unconnected.
- RS232: connect TX/RX/GND through a MAX3232 and leave DE/RE unconnected.
- RS485: connect TX/RX to the RS485 module and connect DE+RE to the preset DE/RE pin.

After flashing, the board opens a setup access point if it cannot join Wi-Fi. Connect to that access point from a phone or laptop and enter your Wi-Fi credentials.

If your inverter needs another UART speed, add the bridge in EyeBond Local, open the collector **Configure** menu, choose **Change inverter UART speed**, select the speed, and confirm. On ESP8266/ESP32, the selected speed is saved and restored after reboot or power loss. The inverter connection may drop briefly while the speed changes.

If your board or pins are different, use the ESPHome YAML option below.

## Option B — Flash with ESPHome YAML

Use this option when you need custom pins, a different board, BK72xx, BLE provisioning, or local development.

### Step 1 — Install ESPHome

Use one of these options:

- **Home Assistant add-on:** install **ESPHome Device Builder** from Home Assistant add-ons.
- **ESPHome CLI:** install ESPHome on your computer.

CLI example:

```bash
python3 -m venv esphome-venv
esphome-venv/bin/pip install esphome
esphome-venv/bin/esphome version
```

If your computer does not see the ESP board over USB, install the driver for your USB-serial chip. Common chips are CH340 and CP210x.

### Step 2 — Choose an example config

Copy the example that matches your board and wiring type:

| File | Board | Link |
|---|---|---|
| [`examples/esp8266-d1-mini.yaml`](../examples/esp8266-d1-mini.yaml) | ESP8266 D1 mini compatible | TTL, RS232, or RS485 |
| [`examples/esp32-devkit.yaml`](../examples/esp32-devkit.yaml) | ESP32 DevKit compatible | TTL, RS232, or RS485 |
| [`examples/esp32-ble.yaml`](../examples/esp32-ble.yaml) | ESP32 with BLE and enough flash | advanced BLE provisioning profile |
| [`examples/bk72xx.yaml`](../examples/bk72xx.yaml) | BK72xx | any |

Then copy `examples/secrets.example.yaml` to `examples/secrets.yaml` and put your Wi-Fi name and password there:

```yaml
wifi_ssid: "YourNetwork"
wifi_password: "YourPassword"
```

ESP8266 and BK72xx boards use 2.4 GHz Wi-Fi only.

Do not commit `examples/secrets.yaml`. It is a local ESPHome file and may contain real Wi-Fi credentials.

### Step 3 — Check board and UART settings

Open the YAML and check:

- `board:` matches your ESP board;
- `tx_pin` and `rx_pin` match your wiring;
- `flow_control_pin` is set when you use a classic RS485 module with DE/RE;
- `status_led_pin` matches the board's built-in LED if you want status indication;
- `baud_rate` matches the inverter family.

The ready-made browser firmware starts at `9600`. Typical baud rates:

- PI30 / Voltronic-style ASCII: `2400`
- SMG / Modbus-style RS485: `9600`

Wrong UART pins or wrong baud rate are the most common reason the bridge is found but the inverter is not detected.

### Step 4 — Flash over USB

Plug the ESP board into your computer and flash it.

CLI example:

```bash
esphome-venv/bin/esphome run examples/esp8266-d1-mini.yaml --device /dev/ttyUSB0
```

In ESPHome Device Builder, use **Install → Plug into this computer**.

After the first USB flash, future updates can usually be installed over Wi-Fi.

If the board does not join Wi-Fi after flashing, power-cycle it.

### Step 5 — Confirm Wi-Fi

The board should join your Wi-Fi within about a minute.

You can check:

- ESPHome logs;
- your router client list;
- `ping eybond-bridge.local`, if mDNS works on your network.

If Wi-Fi credentials are wrong or the network is out of range, the board opens a setup access point. Connect to it from a phone or laptop and enter the correct Wi-Fi credentials.

## Step 6 — Wire the inverter

Power down before changing wiring.

### TTL UART

- inverter TX → ESP RX
- inverter RX → ESP TX
- common ground
- level shifting if the inverter uses 5 V TTL

[TTL wiring diagram](images/wiring-ttl.svg)

### RS232

Use a MAX3232 converter. Do not wire ESP pins directly to RS232.

[RS232 wiring diagram](images/wiring-rs232.svg)

### RS485

- A → A
- B → B
- DE/RE → `flow_control_pin`, unless your module has automatic direction control
- common ground where required by your module

If the inverter does not answer, try swapping A/B.

[RS485 wiring diagram](images/wiring-rs485.svg)

> Avoid powering the ESP from a laptop while it is connected to a mains-powered inverter. Use a safe isolated supply or the intended inverter-side supply.

## Step 7 — Add it to Home Assistant

1. Install **EyeBond Local** in Home Assistant.
2. Go to **Settings → Devices & Services → Add Integration**.
3. Search for **EyeBond Local**.
4. Use the normal collector network setup.
5. Run quick scan.
6. Select the bridge when it appears.

EyeBond Local should detect the bridge as a collector and then probe the inverter through it.

Keep Home Assistant and the bridge on the same network for easiest discovery.

## Optional — add the ESPHome device too

EyeBond Local creates the collector/inverter devices.

EyeBond Local can change the inverter UART speed for the custom ESP bridge from the collector **Configure** menu. Use **Change inverter UART speed**, choose the speed, and confirm. On ESP8266/ESP32, the selected speed is saved and restored after reboot or power loss. The option is shown only for this ESP bridge, not for factory collectors.

The ESPHome integration can also add the ESP board as a separate device. This gives you:

- ESPHome logs;
- OTA updates;
- optional **Inverter UART baud rate** select on ESP8266/ESP32, when it is included in your YAML.

The ESPHome baud-rate select belongs to the ESPHome device. For normal use, prefer the EyeBond Local **Change inverter UART speed** action on the collector.

On BK72xx / LibreTiny, runtime baud-rate switching is not available. Change `baud_rate:` in YAML and reflash. A reboot alone is not enough because the board initializes UART from the compiled YAML value.

The ESP32 BLE profile is YAML-only in the first release. BLE + Wi-Fi is large, so to fit the default 4 MB app partition the profile omits `api:`, `captive_portal:` and the baud `select:` (BLE provisioning replaces the captive portal; the EyeBond Local integration uses its own protocol, not the ESPHome API). OTA still works. To add those back, use a board with more flash and a larger app-partition layout — otherwise the image silently overflows and the board boot-loops.

## Status LED

The release presets use the board's built-in LED:

- Wemos D1 mini: GPIO2 / D4, inverted.
- ESP-WROOM-32 DevKit: GPIO2.

On ESP32, GPIO2 is a boot strapping pin. The built-in LED is fine on typical DevKit boards, but do not add external pull-up/down resistors or heavy loads to GPIO2.

Indication:

| LED | State |
|---|---|
| Fast blink | The board is not connected to Wi-Fi yet. |
| Slow blink | Wi-Fi is connected, but EyeBond Local has not connected to the bridge yet. |
| Solid on | The bridge is connected and idle. |
| Short blink during operation | Data is moving between Home Assistant, the bridge, and the inverter. |

If your board uses another LED pin or inverted logic, adjust `status_led_pin` in YAML.

## Troubleshooting

| Symptom | Most likely cause |
|---|---|
| EyeBond Local does not find the bridge | ESP board is offline, on another network, or blocked by network isolation. |
| Bridge is found, but inverter is not detected | Wrong UART pins, swapped TX/RX, no common ground, wrong baud rate, wrong adapter, or another master on the same bus. |
| ESPHome logs show TX but no RX | Inverter reply path is broken: RX pin, adapter, RS485 A/B, level shifting, or baud rate. |
| Data looks like garbage | Usually wrong baud rate or wrong voltage levels. |
| Board does not join Wi-Fi after flash | Power-cycle it and check Wi-Fi credentials. |
| Captive portal appears after Wi-Fi change | The new Wi-Fi credentials did not work. Connect to the portal and enter them again. |
| Need another UART speed | On ESP8266/ESP32, open the ESP bridge collector **Configure** menu in EyeBond Local and use **Change inverter UART speed**. On BK72xx, change `baud_rate:` in YAML and reflash. |

If the bridge is online but EyeBond Local still cannot detect the inverter, create a Support Archive from EyeBond Local and attach it to an issue.
