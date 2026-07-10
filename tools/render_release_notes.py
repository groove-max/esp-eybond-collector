#!/usr/bin/env python3
"""Render one version section from CHANGELOG.md for a GitHub release."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]


def render(version: str) -> str:
    normalized = version.removeprefix("v")
    changelog = (REPO / "CHANGELOG.md").read_text()
    pattern = re.compile(
        rf"^## \[{re.escape(normalized)}\][^\n]*\n(?P<body>.*?)(?=^## \[|\Z)",
        re.MULTILINE | re.DOTALL,
    )
    match = pattern.search(changelog)
    if match is None:
        raise RuntimeError(f"CHANGELOG.md has no section for {normalized}")
    body = match.group("body").strip()
    if not body:
        raise RuntimeError(f"CHANGELOG.md section for {normalized} is empty")
    return body + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("version")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render(args.version))
    print(f"release_notes:{output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
