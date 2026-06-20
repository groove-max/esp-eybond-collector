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
    label_uk: str
    config: str
    chip_family: str
    tx: str
    rx: str
    de: str
    led: str
    led_uk: str
    build_name: str


PRESETS: tuple[Preset, ...] = (
    Preset(
        slug="esp8266-d1-mini",
        label="ESP8266 / D1 mini compatible",
        label_uk="ESP8266 / D1 mini сумісна плата",
        config="examples/esp8266-d1-mini.yaml",
        chip_family="ESP8266",
        tx="GPIO13 / D7",
        rx="GPIO12 / D6",
        de="GPIO14 / D5",
        led="GPIO2 / D4, inverted",
        led_uk="GPIO2 / D4, інверсний режим",
        build_name="eybond-esp8266",
    ),
    Preset(
        slug="esp32-devkit",
        label="ESP32 DevKit compatible",
        label_uk="ESP32 DevKit сумісна плата",
        config="examples/esp32-devkit.yaml",
        chip_family="ESP32",
        tx="GPIO17",
        rx="GPIO16",
        de="GPIO4",
        led="GPIO2",
        led_uk="GPIO2",
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
    cards_en = []
    cards_uk = []
    for preset in PRESETS:
        cards_en.append(
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
        <esp-web-install-button manifest="manifest-{preset.slug}.json">
          <button slot="activate">Install firmware</button>
          <span slot="unsupported">Use Google Chrome or Microsoft Edge on a secure HTTPS page to flash from the browser.</span>
          <span slot="not-allowed">Open this installer over HTTPS or from localhost.</span>
        </esp-web-install-button>
      </section>
"""
        )
        cards_uk.append(
            f"""
      <section class="card">
        <h2>{preset.label_uk}</h2>
        <p>Використовуйте цей варіант для TTL, RS232 або RS485 з наведеними нижче пінами.</p>
        <table>
          <tr><th>TX</th><td>{preset.tx}</td></tr>
          <tr><th>RX</th><td>{preset.rx}</td></tr>
          <tr><th>RS485 DE/RE</th><td>{preset.de} (не підключайте для TTL/RS232)</td></tr>
          <tr><th>Світлодіод стану</th><td>{preset.led_uk}</td></tr>
        </table>
        <esp-web-install-button manifest="manifest-{preset.slug}.json">
          <button slot="activate">Прошити плату</button>
          <span slot="unsupported">Для прошивки з браузера використовуйте Google Chrome або Microsoft Edge на захищеній HTTPS-сторінці.</span>
          <span slot="not-allowed">Відкрийте цю сторінку через HTTPS або з localhost.</span>
        </esp-web-install-button>
      </section>
"""
        )
    (output / "index.html").write_text(
        f"""<!doctype html>
<html lang="uk">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP EyeBond Collector Installer</title>
    <script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
    <style>
      body {{ font-family: system-ui, sans-serif; max-width: 980px; margin: 2rem auto; padding: 0 1rem; line-height: 1.5; color: #1f2933; }}
      h1, h2 {{ line-height: 1.2; }}
      a {{ color: #0969da; }}
      button {{ cursor: pointer; }}
      .topbar {{ display: flex; align-items: center; justify-content: space-between; gap: 1rem; flex-wrap: wrap; }}
      .lang-switch {{ display: flex; gap: .5rem; }}
      .lang-switch button {{ border: 1px solid #ccd6dd; border-radius: 999px; background: #fff; padding: .4rem .8rem; }}
      .lang-switch button[aria-pressed="true"] {{ background: #0969da; border-color: #0969da; color: #fff; }}
      [data-lang-panel] {{ display: none; }}
      [data-lang-panel].active {{ display: block; }}
      .grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1rem; }}
      .card {{ border: 1px solid #ddd; border-radius: 12px; padding: 1rem; }}
      .card h2 {{ margin-top: 0; }}
      table {{ border-collapse: collapse; width: 100%; margin: 1rem 0; }}
      th, td {{ border-top: 1px solid #eee; padding: .4rem; text-align: left; }}
      .warning {{ background: #fff7d6; border: 1px solid #ead27a; border-radius: 8px; padding: .75rem; }}
      .note {{ background: #eef6ff; border: 1px solid #b6d7ff; border-radius: 8px; padding: .75rem; }}
      .danger {{ background: #fff1f1; border: 1px solid #ffb4b4; border-radius: 8px; padding: .75rem; }}
      .reload {{ border: 1px solid #ccd6dd; border-radius: 8px; background: #fff; padding: .5rem .75rem; }}
      esp-web-install-button button {{ background: #0969da; border: 0; border-radius: 999px; color: #fff; font-weight: 600; padding: .65rem 1.1rem; }}
    </style>
  </head>
  <body>
    <div class="topbar">
      <h1>ESP EyeBond Collector Installer</h1>
      <div class="lang-switch" aria-label="Language">
        <button type="button" data-lang-button="uk" aria-pressed="true">Українська</button>
        <button type="button" data-lang-button="en" aria-pressed="false">English</button>
      </div>
    </div>

    <section data-lang-panel="uk" class="active" lang="uk">
      <p>Версія: {version}</p>
      <p class="warning">Обирайте готовий варіант тільки якщо ваша плата і підключення відповідають наведеним пінам. Якщо піни інші, використовуйте YAML-приклади ESPHome.</p>
      <p class="note">Для багатьох ESP32-плат перед прошивкою потрібно затиснути кнопку <strong>BOOT</strong>, натиснути <strong>Прошити плату</strong>, вибрати USB-порт і відпустити BOOT після початку підключення або стирання flash.</p>
      <p class="danger">Якщо з'явилась помилка <strong>Failed to initialize</strong>, затисніть BOOT і спробуйте ще раз. Якщо після кнопки <strong>Continue</strong> лишився нескінченний індикатор прогресу, перезавантажте цю сторінку.</p>
      <p><button class="reload" type="button" onclick="window.location.reload()">Перезавантажити сторінку прошивки</button></p>
      <div class="grid">
{''.join(cards_uk)}
      </div>
      <h2>Після прошивки</h2>
      <ol>
        <li>Якщо плата не підключилась до Wi-Fi, знайдіть її тимчасову мережу з назвою на кшталт <strong>V00000...</strong> і відкрийте сторінку налаштування.</li>
        <li>Підключіть інвертор до пінів обраного варіанта.</li>
        <li>Додайте пристрій через EyeBond Local у Home Assistant.</li>
      </ol>
    </section>

    <section data-lang-panel="en" lang="en">
      <p>Version: {version}</p>
      <p class="warning">Choose a preset only if your board and wiring match the listed pins. If your pins differ, use the ESPHome YAML examples instead.</p>
      <p class="note">Many ESP32 boards need the <strong>BOOT</strong> button for flashing. Hold BOOT, click <strong>Install firmware</strong>, select the USB port, and release BOOT after connecting or flash erase starts.</p>
      <p class="danger">If you see <strong>Failed to initialize</strong>, hold BOOT and try again. If clicking <strong>Continue</strong> leaves an endless progress bar, reload this page before retrying.</p>
      <p><button class="reload" type="button" onclick="window.location.reload()">Reload installer page</button></p>
      <div class="grid">
{''.join(cards_en)}
      </div>
      <h2>After flashing</h2>
      <ol>
        <li>If the board does not connect to Wi-Fi, find its temporary network named like <strong>V00000...</strong> and open the setup portal.</li>
        <li>Wire the inverter using the selected preset pins.</li>
        <li>Add the device through EyeBond Local in Home Assistant.</li>
      </ol>
    </section>

    <script>
      const panels = [...document.querySelectorAll("[data-lang-panel]")];
      const buttons = [...document.querySelectorAll("[data-lang-button]")];
      function setLanguage(lang) {{
        document.documentElement.lang = lang;
        panels.forEach((panel) => panel.classList.toggle("active", panel.dataset.langPanel === lang));
        buttons.forEach((button) => button.setAttribute("aria-pressed", button.dataset.langButton === lang ? "true" : "false"));
        try {{ localStorage.setItem("installer-language", lang); }} catch (_err) {{}}
      }}
      buttons.forEach((button) => button.addEventListener("click", () => setLanguage(button.dataset.langButton)));
      let saved = null;
      try {{ saved = localStorage.getItem("installer-language"); }} catch (_err) {{}}
      setLanguage(saved || (navigator.language && navigator.language.toLowerCase().startsWith("uk") ? "uk" : "en"));
    </script>
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
