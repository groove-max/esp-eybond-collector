#!/usr/bin/env python3
"""Build web-installer firmware artifacts for ESP EyeBond Collector.

The public web installer intentionally ships only two generic presets:

- esp8266-d1-mini: one D1-mini-compatible pinout for TTL/RS232/RS485.
- esp32-devkit: one ESP32 DevKit pinout for TTL/RS232/RS485.

BLE and BK72xx stay YAML-only for the first release.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
GITHUB_SOURCE = """external_components:
  - source: github://groove-max/esp-eybond-collector
    components: [eybond_collector]
"""
LOCAL_SOURCE = f"""external_components:
  - source:
      type: local
      path: {REPO / "components"}
"""


@dataclass(frozen=True)
class Preset:
    slug: str
    label: str
    config: str
    chip_family: str
    tx: str
    rx: str
    de: str
    led: str
    build_name: str


PRESETS: tuple[Preset, ...] = (
    Preset(
        slug="esp8266-d1-mini",
        label="ESP8266 / D1 mini compatible",
        config="examples/esp8266-d1-mini.yaml",
        chip_family="ESP8266",
        tx="GPIO13 / D7",
        rx="GPIO12 / D6",
        de="GPIO14 / D5",
        led="GPIO2 / D4, inverted",
        build_name="eybond-esp8266",
    ),
    Preset(
        slug="esp32-devkit",
        label="ESP32 DevKit compatible",
        config="examples/esp32-devkit.yaml",
        chip_family="ESP32",
        tx="GPIO17",
        rx="GPIO16",
        de="GPIO4",
        led="GPIO2",
        build_name="eybond-esp32",
    ),
)


def _write_ci_config(preset: Preset, base: Path) -> Path:
    source = (REPO / preset.config).read_text()
    if GITHUB_SOURCE not in source:
        raise RuntimeError(f"{preset.config}: expected public GitHub external_components block")
    source = source.replace("  name: eybond-bridge\n", f"  name: {preset.build_name}\n", 1)
    base.mkdir(parents=True, exist_ok=True)
    (base / "secrets.yaml").write_text(
        'wifi_ssid: "ExampleNetwork"\nwifi_password: "ExamplePassword"\n'
    )
    config = base / f"{preset.slug}.yaml"
    config.write_text(source.replace(GITHUB_SOURCE, LOCAL_SOURCE))
    return config


def _run(cmd: list[str], cwd: Path) -> None:
    subprocess.run(cmd, cwd=cwd, check=True)


def _compile(preset: Preset, *, esphome: str, local_source: bool) -> Path:
    if local_source:
        config = _write_ci_config(preset, REPO / ".local" / "web_release_configs")
    else:
        config = REPO / preset.config
    _run([esphome, "compile", str(config)], cwd=REPO)
    build_name = preset.build_name if local_source else "eybond-bridge"
    build_dir = config.parent / ".esphome" / "build" / build_name
    factory = build_dir / ".pioenvs" / build_name / "firmware.factory.bin"
    if not factory.exists():
        raise FileNotFoundError(factory)
    return factory


def _manifest(preset: Preset, version: str) -> dict[str, object]:
    return {
        "name": f"ESP EyeBond Collector - {preset.label}",
        "version": version,
        "home_assistant_domain": "esphome",
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": preset.chip_family,
                "parts": [
                    {
                        "path": f"firmware/{preset.slug}.factory.bin",
                        "offset": 0,
                    }
                ],
            }
        ],
    }


def _write_web(output: Path, version: str) -> None:
    cards = []
    for preset in PRESETS:
        cards.append(
            f"""
      <section class="card">
        <h2>{preset.label}</h2>
        <p>Use this for TTL, RS232, or RS485 wiring with the release pinout.</p>
        <table>
          <tr><th>TX</th><td>{preset.tx}</td></tr>
          <tr><th>RX</th><td>{preset.rx}</td></tr>
          <tr><th>RS485 DE/RE</th><td>{preset.de} (leave unconnected for TTL/RS232)</td></tr>
          <tr><th>Status LED</th><td>{preset.led}</td></tr>
        </table>
        <esp-web-install-button manifest="manifest-{preset.slug}.json"></esp-web-install-button>
      </section>
"""
        )
    (output / "index.html").write_text(
        f"""<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP EyeBond Collector Installer</title>
    <script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
    <style>
      body {{ font-family: system-ui, sans-serif; max-width: 980px; margin: 2rem auto; padding: 0 1rem; line-height: 1.5; }}
      .grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1rem; }}
      .card {{ border: 1px solid #ddd; border-radius: 12px; padding: 1rem; }}
      table {{ border-collapse: collapse; width: 100%; margin: 1rem 0; }}
      th, td {{ border-top: 1px solid #eee; padding: .4rem; text-align: left; }}
      .warning {{ background: #fff7d6; border: 1px solid #ead27a; border-radius: 8px; padding: .75rem; }}
    </style>
  </head>
  <body>
    <h1>ESP EyeBond Collector Installer</h1>
    <p>Version: {version}</p>
    <p class="warning">Choose a preset only if your board and wiring match the listed pins. If your pins differ, use the ESPHome YAML examples instead.</p>
    <div class="grid">
{''.join(cards)}
    </div>
    <h2>After flashing</h2>
    <ol>
      <li>Connect the bridge to Wi-Fi using the setup portal if needed.</li>
      <li>Wire the inverter using the selected preset pins.</li>
      <li>Add the device through EyeBond Local in Home Assistant.</li>
    </ol>
  </body>
</html>
"""
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="dist/web-release")
    parser.add_argument("--version", default="dev")
    parser.add_argument("--esphome", default="esphome")
    parser.add_argument("--compile", action="store_true")
    parser.add_argument("--local-source", action="store_true")
    args = parser.parse_args()

    output = REPO / args.output
    web = output / "web"
    firmware = web / "firmware"
    firmware.mkdir(parents=True, exist_ok=True)

    for preset in PRESETS:
        if args.compile:
            factory = _compile(preset, esphome=args.esphome, local_source=args.local_source)
            shutil.copy2(factory, firmware / f"{preset.slug}.factory.bin")
        manifest_path = web / f"manifest-{preset.slug}.json"
        manifest_path.write_text(json.dumps(_manifest(preset, args.version), indent=2) + "\n")

    _write_web(web, args.version)
    print(f"web_release:{web}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
