#!/usr/bin/env python3
"""Generate wiring diagrams (SVG) for the docs.

Pure-stdlib generator — no Fritzing, no binary assets. Emits clean, scalable,
diff-friendly SVGs into docs/images/. Re-run after editing to regenerate:
    python3 tools/gen_wiring_svg.py
"""

from __future__ import annotations

import html
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "docs" / "images"

# Palette
BG = "#ffffff"
BOX = "#f4f6f8"
BOX_STROKE = "#5b6b7b"
TEXT = "#1d2b36"
DATA = "#1f6feb"      # data lines
GND = "#3a3a3a"       # ground
POWER = "#c9302c"     # optional power
NOTE = "#6a7682"

W, H = 820, 380
COL_ESP_X = 40
COL_MID_X = 330
COL_INV_X = 620
BOX_W = 160
BOX_W_MID = 150


def esc(text: str) -> str:
    return html.escape(str(text))


def box(x, y, w, h, title, pins, *, subtitle=""):
    """A labelled node box with pins listed top-to-bottom; returns (svg, pin_y)."""
    parts = [
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" '
        f'fill="{BOX}" stroke="{BOX_STROKE}" stroke-width="2"/>',
        f'<text x="{x + w/2}" y="{y + 22}" text-anchor="middle" '
        f'font-weight="700" font-size="15" fill="{TEXT}">{esc(title)}</text>',
    ]
    if subtitle:
        parts.append(
            f'<text x="{x + w/2}" y="{y + 39}" text-anchor="middle" '
            f'font-size="11" fill="{NOTE}">{esc(subtitle)}</text>'
        )
    pin_y = {}
    top = y + (58 if subtitle else 46)
    step = 30
    for i, (name, side) in enumerate(pins):
        py = top + i * step
        pin_y[name] = py
        anchor_x = x + w if side == "right" else x
        tx = (x + w - 12) if side == "right" else (x + 12)
        ta = "end" if side == "right" else "start"
        parts.append(
            f'<circle cx="{anchor_x}" cy="{py}" r="4" fill="{TEXT}"/>'
        )
        parts.append(
            f'<text x="{tx}" y="{py + 4}" text-anchor="{ta}" '
            f'font-size="13" fill="{TEXT}" font-family="monospace">{esc(name)}</text>'
        )
        pin_y[(name, "x")] = anchor_x
    return "\n".join(parts), pin_y


def wire(x1, y1, x2, y2, color, label="", *, arrow=True, dashed=False):
    mid_x = (x1 + x2) / 2
    dash = ' stroke-dasharray="6 4"' if dashed else ""
    marker = ' marker-end="url(#arrow)"' if arrow else ""
    path = (
        f'<path d="M {x1} {y1} H {mid_x} V {y2} H {x2}" fill="none" '
        f'stroke="{color}" stroke-width="2.5"{dash}{marker}/>'
    )
    lbl = ""
    if label:
        ly = min(y1, y2) - 7 if y1 != y2 else y1 - 7
        lbl = (
            f'<text x="{mid_x}" y="{ly}" text-anchor="middle" font-size="11" '
            f'fill="{color}">{esc(label)}</text>'
        )
    return path + lbl


def svg_doc(title, caption, body):
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI, Arial, sans-serif">
  <defs>
    <marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M 0 0 L 10 5 L 0 10 z" fill="context-stroke"/>
    </marker>
  </defs>
  <rect width="{W}" height="{H}" fill="{BG}"/>
  <text x="{W/2}" y="34" text-anchor="middle" font-size="19" font-weight="700" fill="{TEXT}">{esc(title)}</text>
{body}
  <text x="{W/2}" y="{H-18}" text-anchor="middle" font-size="12" fill="{NOTE}">{esc(caption)}</text>
</svg>
"""


def legend(x, y):
    items = [
        (DATA, "data (TX/RX)"),
        (GND, "ground (common)"),
    ]
    parts = []
    for i, (c, t) in enumerate(items):
        ly = y + i * 20
        parts.append(f'<line x1="{x}" y1="{ly}" x2="{x+26}" y2="{ly}" stroke="{c}" stroke-width="3"/>')
        parts.append(f'<text x="{x+34}" y="{ly+4}" font-size="12" fill="{NOTE}">{esc(t)}</text>')
    return "\n".join(parts)


def diagram_ttl():
    esp, ep = box(COL_ESP_X, 70, BOX_W, 200, "ESP (3.3 V)",
                  [("TX", "right"), ("RX", "right"), ("GND", "right")],
                  subtitle="ESP8266 / ESP32 / BK72xx")
    inv, ip = box(COL_INV_X, 70, BOX_W, 200, "Inverter",
                  [("RX", "left"), ("TX", "left"), ("GND", "left")],
                  subtitle="TTL UART port")
    body = [esp, inv]
    body.append(wire(ep[("TX", "x")], ep["TX"], ip[("RX", "x")], ip["RX"], DATA, "ESP TX → INV RX", arrow=False))
    body.append(wire(ip[("TX", "x")], ip["TX"], ep[("RX", "x")], ep["RX"], DATA, "INV TX → ESP RX", arrow=False))
    body.append(wire(ep[("GND", "x")], ep["GND"], ip[("GND", "x")], ip["GND"], GND, "common ground", arrow=False))
    body.append(legend(COL_ESP_X, 312))
    warn_cx = (COL_ESP_X + BOX_W + COL_INV_X) / 2
    body.append(
        f'<text x="{warn_cx}" y="244" text-anchor="middle" font-size="12" fill="{POWER}">'
        f'⚠ 5 V TTL inverter: add a level shifter / divider</text>'
        f'<text x="{warn_cx}" y="261" text-anchor="middle" font-size="12" fill="{POWER}">'
        f'on the ESP RX (3.3 V) line</text>'
    )
    return svg_doc("Wiring — direct TTL UART",
                   "Cross over TX/RX. Share a common ground. PI30/Voltronic TTL is usually 2400 8N1.",
                   "\n".join(body))


def diagram_converter(kind):
    is485 = kind == "rs485"
    chip = "MAX485 (RS485)" if is485 else "MAX3232 (RS232)"
    esp_pins = [("TX", "right"), ("RX", "right"), ("GND", "right")]
    if is485:
        esp_pins.insert(2, ("DE/RE", "right"))
    esp, ep = box(COL_ESP_X, 60, BOX_W, 230, "ESP (3.3 V)", esp_pins,
                  subtitle="ESP8266 / ESP32 / BK72xx")
    mid_pins_ttl = [("DI/TXD", "left"), ("RO/RXD", "left"), ("GND", "left")]
    if is485:
        mid_pins_ttl.insert(2, ("DE+RE", "left"))
    # transceiver: TTL side (left pins) + line side (right pins)
    line_pins = [("A", "right"), ("B", "right"), ("GND", "right")] if is485 \
        else [("TX", "right"), ("RX", "right"), ("GND", "right")]
    mid_h = 230
    midx = COL_MID_X
    tparts = [
        f'<rect x="{midx}" y="60" width="{BOX_W_MID}" height="{mid_h}" rx="10" '
        f'fill="{BOX}" stroke="{BOX_STROKE}" stroke-width="2"/>',
        f'<text x="{midx + BOX_W_MID/2}" y="82" text-anchor="middle" font-weight="700" '
        f'font-size="14" fill="{TEXT}">{esc(chip)}</text>',
        f'<text x="{midx + BOX_W_MID/2}" y="99" text-anchor="middle" font-size="10" '
        f'fill="{NOTE}">TTL  ↔  line levels</text>',
    ]
    mp = {}
    top = 120
    for i, (name, _side) in enumerate(mid_pins_ttl):
        py = top + i * 30
        mp[("L", name)] = py
        tparts.append(f'<circle cx="{midx}" cy="{py}" r="4" fill="{TEXT}"/>')
        tparts.append(f'<text x="{midx+12}" y="{py+4}" font-size="12" fill="{TEXT}" font-family="monospace">{esc(name)}</text>')
    for i, (name, _side) in enumerate(line_pins):
        py = top + i * 30
        mp[("R", name)] = py
        tparts.append(f'<circle cx="{midx+BOX_W_MID}" cy="{py}" r="4" fill="{TEXT}"/>')
        tparts.append(f'<text x="{midx+BOX_W_MID-12}" y="{py+4}" text-anchor="end" font-size="12" fill="{TEXT}" font-family="monospace">{esc(name)}</text>')

    inv_pins = [("A", "left"), ("B", "left"), ("GND", "left")] if is485 \
        else [("RX", "left"), ("TX", "left"), ("GND", "left")]
    inv, ip = box(COL_INV_X, 60, BOX_W, 230, "Inverter",
                  inv_pins, subtitle=("RS485 / Modbus port" if is485 else "RS232 port (DB9 / RJ45)"))

    body = ["\n".join(tparts), esp, inv]
    # ESP <-> transceiver TTL side
    body.append(wire(ep[("TX", "x")], ep["TX"], midx, mp[("L", "DI/TXD")], DATA, "TX", arrow=False))
    body.append(wire(ep[("RX", "x")], ep["RX"], midx, mp[("L", "RO/RXD")], DATA, "RX", arrow=False))
    if is485:
        body.append(wire(ep[("DE/RE", "x")], ep["DE/RE"], midx, mp[("L", "DE+RE")], DATA, "flow_control_pin", arrow=False))
    body.append(wire(ep[("GND", "x")], ep["GND"], midx, mp[("L", "GND")], GND, "", arrow=False))
    # transceiver line side <-> inverter
    if is485:
        body.append(wire(midx+BOX_W_MID, mp[("R", "A")], ip[("A", "x")], ip["A"], DATA, "A", arrow=False))
        body.append(wire(midx+BOX_W_MID, mp[("R", "B")], ip[("B", "x")], ip["B"], DATA, "B", arrow=False))
    else:
        body.append(wire(midx+BOX_W_MID, mp[("R", "TX")], ip[("RX", "x")], ip["RX"], DATA, "TX→RX", arrow=False))
        body.append(wire(midx+BOX_W_MID, mp[("R", "RX")], ip[("TX", "x")], ip["TX"], DATA, "RX←TX", arrow=False))
    body.append(wire(midx+BOX_W_MID, mp[("R", "GND")], ip[("GND", "x")], ip["GND"], GND, "", arrow=False))
    body.append(legend(COL_ESP_X, 312))

    if is485:
        title = "Wiring — RS485 (MAX485)"
        cap = ("RS485 is half-duplex: drive DE/RE from flow_control_pin. A/B are the differential "
               "pair. SMG/Modbus is usually 9600 8N1.")
    else:
        title = "Wiring — RS232 (MAX3232)"
        cap = ("RS232 is ±12 V — never wire ESP TTL pins straight to it. Full-duplex: no DE/RE. "
               "Cross TX/RX, common ground.")
    return svg_doc(title, cap, "\n".join(body))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    files = {
        "wiring-ttl.svg": diagram_ttl(),
        "wiring-rs232.svg": diagram_converter("rs232"),
        "wiring-rs485.svg": diagram_converter("rs485"),
    }
    for name, content in files.items():
        (OUT / name).write_text(content)
        print("wrote", OUT / name)


if __name__ == "__main__":
    main()
