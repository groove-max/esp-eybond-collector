#!/usr/bin/env python3
"""Compile selected local-source examples without publishing release artifacts.

Works with both PlatformIO and native ESP-IDF ESPHome builds. Generated YAML
and synthetic secrets stay under .local, separate from real device configs.
"""

import argparse
from pathlib import Path
import subprocess

from build_web_release import REPO, _write_local_source_config

PROFILES = (
    "esp32-c3", "esp32-devkit", "esp32-ble", "esp8266-d1-mini", "bk72xx"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--esphome", default="esphome")
    parser.add_argument("--profile", choices=PROFILES, action="append", required=True)
    parser.add_argument("--only-generate", action="store_true")
    parser.add_argument("--build-root", type=Path, default=REPO / ".local" / "compat_build")
    args = parser.parse_args()
    for profile in args.profile:
        config = _write_local_source_config(
            f"examples/{profile}.yaml", args.build_root / profile
        )
        # Bound compiler memory usage on laptops and small CI runners alike.
        source = config.read_text()
        config.write_text(source.replace("esphome:\n", "esphome:\n  compile_process_limit: 2\n", 1))
        command = [args.esphome, "compile"]
        if args.only_generate:
            command.append("--only-generate")
        subprocess.run([*command, str(config)], cwd=REPO, check=True)


if __name__ == "__main__":
    main()
