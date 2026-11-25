#!/usr/bin/env python3
"""Validate public YAML files and local Markdown links."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

import yaml

from build_web_release import PRESETS, _manifest


REPO = Path(__file__).resolve().parents[1]


class Loader(yaml.SafeLoader):
    """YAML loader that accepts ESPHome's !secret scalar."""


Loader.add_constructor("!secret", lambda loader, node: loader.construct_scalar(node))


def _validate_yaml() -> None:
    tracked_secret = subprocess.run(
        ["git", "ls-files", "--error-unmatch", "examples/secrets.yaml"],
        cwd=REPO,
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if tracked_secret.returncode == 0:
        raise RuntimeError("examples/secrets.yaml must not be committed")
    paths = [
        *(REPO / "examples").glob("*.yaml"),
        *(REPO / "packages").glob("*.yaml"),
    ]
    for path in sorted(paths):
        if path.name == "secrets.yaml":
            continue
        yaml.load(path.read_text(), Loader=Loader)


def _validate_markdown_links() -> None:
    link_re = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
    paths = [
        REPO / "README.md",
        REPO / "README.uk.md",
        *(REPO / "docs").rglob("*.md"),
    ]
    for path in paths:
        for match in link_re.finditer(path.read_text()):
            target = match.group(1).split("#", 1)[0]
            if not target or "://" in target:
                continue
            if not (path.parent / target).exists():
                raise RuntimeError(f"{path.relative_to(REPO)}: broken link {target}")


def _validate_supermini() -> None:
    # The dashboard import must not silently move the two UART wires, change
    # power-on behaviour, or require credentials baked into the public image.
    c3_presets = [p for p in PRESETS if p.chip_family == "ESP32-C3"]
    if len(c3_presets) != 1 or c3_presets[0].config != "examples/esp32-c3.yaml":
        raise ValueError("Expected one C3 installer preset using examples/esp32-c3.yaml")
    preset = c3_presets[0]
    example = yaml.safe_load((REPO / preset.config).read_text())
    if example["dashboard_import"]["package_import_url"] != (
        f"github://groove-max/esp-eybond-collector/{preset.config}@main"
    ):
        raise ValueError("Dashboard adoption and factory build must use the same C3 YAML")
    uart = example["uart"]
    if not (uart["tx_pin"] == preset.tx == "GPIO21" and uart["rx_pin"] == preset.rx == "GPIO20"
            and (uart["baud_rate"], uart["data_bits"], uart["parity"], uart["stop_bits"])
            == (9600, 8, "NONE", 1)):
        raise ValueError("SuperMini UART differs from the tested pinout/format")
    collector = example["eybond_collector"]
    if (collector["status_led_pin"] != {"number": "GPIO8", "inverted": True}
            or preset.de is not None or "flow_control_pin" in collector
            or {"pn", "server_host"} & collector.keys() or example["wifi"] != {"ap": {}}):
        raise ValueError("SuperMini board or provisioning contract violated")
    builds = _manifest(preset, "ci")["builds"]
    if builds != [{"chipFamily": "ESP32-C3", "parts": [
        {"path": "firmware/esp32-c3.factory.bin", "offset": 0}]}]:
        raise ValueError("SuperMini installer must use a C3 factory image at offset zero")


def main() -> int:
    _validate_yaml()
    _validate_supermini()
    _validate_markdown_links()
    print("repository_validation:ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
