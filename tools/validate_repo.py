#!/usr/bin/env python3
"""Validate public YAML files and local Markdown links."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

import yaml


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
        *(REPO / "docs").glob("*.md"),
    ]
    for path in paths:
        for match in link_re.finditer(path.read_text()):
            target = match.group(1).split("#", 1)[0]
            if not target or "://" in target:
                continue
            if not (path.parent / target).exists():
                raise RuntimeError(f"{path.relative_to(REPO)}: broken link {target}")


def main() -> int:
    _validate_yaml()
    _validate_markdown_links()
    print("repository_validation:ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
