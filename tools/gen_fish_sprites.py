#!/usr/bin/env python3
"""Generate the Pocket Fishing 8-bit pixel art as LVGL image descriptors.

Every sprite is drawn procedurally at native resolution from a fixed 16-color
palette, outlined in ink, and scaled by an integer factor with nearest-neighbour
sampling so pixels stay sharp. Output is uncompressed RGB565 (background) and
RGB565A8 (sprites): LVGL 9.5 draws both directly from Flash without decoding
into RAM, which matters on the PSRAM-less ESP32-C3.

The catch and bait order is read from main/fish_catalog.c, so the generated
arrays always match FISH_ENTRIES / FISH_BAITS; a catalog entry without art fails.

Usage:
  python3 tools/gen_fish_sprites.py --c-out build/fish_sprites.c     # used by CMake
  python3 tools/gen_fish_sprites.py --preview assets/images/fishing-sprites-preview.png
"""

from __future__ import annotations

import argparse
import math
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / "main" / "fish_catalog.c"

SCALE = 3
SCREEN_W, SCREEN_H = 240, 320
HORIZON = 50  # native rows of sky; 50 × 3 = 150 px, matches WATER_Y in main/fish_ui.c

# 16-color "pond at dusk" palette.
PALETTE = {
    "K": 0x1A1C2C,  # ink / outline
    "P": 0x5D275D,  # dusk purple
    "R": 0xB13E53,  # red
    "O": 0xEF7D57,  # orange
    "Y": 0xFFCD75,  # yellow
    "W": 0xFFF1E8,  # cream
    "E": 0xA7F070,  # light green
    "G": 0x38B764,  # green
    "T": 0x257179,  # teal
    "N": 0x29366F,  # navy
    "B": 0x3B5DC9,  # blue
    "L": 0x41A6F6,  # light blue
    "S": 0x94B0C2,  # grey
    "D": 0x566C86,  # slate
    "M": 0x8B5A2B,  # brown
    "A": 0xC98A3D,  # amber
}
SHADOW = 0x1E3A4F  # silhouette colour for undiscovered catches (C_SHADOW in fish_ui.c)

BAYER4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))


class Canvas:
    def __init__(self, w: int, h: int) -> None:
        self.w, self.h = w, h
        self.px: list[list[str | None]] = [[None] * w for _ in range(h)]

    def get(self, x: int, y: int) -> str | None:
        return self.px[y][x] if 0 <= x < self.w and 0 <= y < self.h else None

    def set(self, x: int, y: int, c: str | None) -> None:
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def rect(self, x0: int, y0: int, x1: int, y1: int, c: str | None) -> None:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def ellipse(self, cx: float, cy: float, a: float, b: float, c: str) -> None:
        for y in range(self.h):
            for x in range(self.w):
                if ((x + 0.5 - cx) / a) ** 2 + ((y + 0.5 - cy) / b) ** 2 <= 1.0:
                    self.px[y][x] = c

    def triangle(self, p0, p1, p2, c: str | None) -> None:
        def edge(a, b, x, y):
            return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0])

        for y in range(self.h):
            for x in range(self.w):
                px, py = x + 0.5, y + 0.5
                d0, d1, d2 = edge(p0, p1, px, py), edge(p1, p2, px, py), edge(p2, p0, px, py)
                if (d0 >= 0 and d1 >= 0 and d2 >= 0) or (d0 <= 0 and d1 <= 0 and d2 <= 0):
                    self.px[y][x] = c

    def cells(self):
        for y in range(self.h):
            for x in range(self.w):
                yield x, y, self.px[y][x]

    def outline(self, c: str = "K") -> None:
        """Ink every empty pixel that touches the shape (4-neighbourhood)."""
        edge = [
            (x, y) for x, y, v in self.cells()
            if v is None and any(self.get(x + dx, y + dy) not in (None,)
                                 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        ]
        for x, y in edge:
            self.px[y][x] = c


# ---------------------------------------------------------------------------
# Catches
# ---------------------------------------------------------------------------

def fish(body: str, top: str, belly: str, fin: str, *, fat: float = 1.0,
         pattern: str | None = None, mark: str = "K", barbel: bool = False,
         sparkle: bool = False) -> Canvas:
    c = Canvas(32, 20)
    cx, cy, a, b = 12.0, 10.0, 10.5, 5.0 * fat
    tail_x = cx + a + 7
    c.triangle((cx + a - 2, cy), (tail_x, cy - b - 1), (tail_x, cy + b + 1), fin)
    c.triangle((tail_x + 0.5, cy - 2.5), (cx + a + 3.5, cy), (tail_x + 0.5, cy + 2.5), None)
    c.triangle((cx - 4, cy - b + 1), (cx - 1, cy - b - 3.5), (cx + 5, cy - b + 1), fin)
    c.triangle((cx - 1, cy + b - 1), (cx + 1, cy + b + 2.5), (cx + 4, cy + b - 1), fin)
    c.ellipse(cx, cy, a, b, body)

    def in_body(x: int, y: int) -> bool:
        return ((x + 0.5 - cx) / a) ** 2 + ((y + 0.5 - cy) / b) ** 2 <= 1.0

    for x, y, _ in list(c.cells()):
        if not in_body(x, y):
            continue
        dy = y + 0.5 - cy
        if dy < -b * 0.45:
            c.set(x, y, top)
        elif dy > b * 0.3 and x < cx + a * 0.55:
            c.set(x, y, belly)
        if pattern == "stripes" and x % 4 == 1 and cx - a + 6 <= x <= cx + a - 3 and dy < b * 0.4:
            c.set(x, y, mark)
        elif pattern == "scales" and (x + y) % 3 == 0 and x % 2 == 0 and x > cx - a + 5 and dy < b * 0.3:
            c.set(x, y, mark)
        elif pattern == "spots" and (x * 7 + y * 3) % 11 == 0 and x > cx - a + 5 and dy < b * 0.5:
            c.set(x, y, mark)
            c.set(x + 1, y, mark if in_body(x + 1, y) else c.get(x + 1, y))
    if pattern == "koi":
        for (px, py, ra, rb) in ((10.5, 7.5, 3.2, 2.0), (17.5, 9.5, 2.6, 2.4)):
            for x, y, _ in list(c.cells()):
                if in_body(x, y) and ((x + 0.5 - px) / ra) ** 2 + ((y + 0.5 - py) / rb) ** 2 <= 1:
                    c.set(x, y, mark)

    ex, ey = int(cx - a) + 3, int(cy) - 2
    c.set(ex, ey, "W"); c.set(ex + 1, ey, "W"); c.set(ex, ey + 1, "K"); c.set(ex + 1, ey + 1, "W")
    for y in range(ey - 1, ey + 4):
        if in_body(ex + 4, y):
            c.set(ex + 4, y, top)
    c.outline()
    c.set(int(cx - a) + 1, int(cy) + 1, "K")  # mouth
    if barbel:
        c.set(int(cx - a), int(cy) + 3, "K")
        c.set(int(cx - a) - 1, int(cy) + 4, "K")
    if sparkle:
        for sx, sy in ((2, 2), (29, 1), (27, 18)):
            for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
                if c.get(sx + dx, sy + dy) is None:
                    c.set(sx + dx, sy + dy, "W" if (dx, dy) == (0, 0) else "Y")
    return c


def weed() -> Canvas:
    c = Canvas(32, 20)
    for i, (x0, top, phase, col) in enumerate(((9, 3, 0.0, "T"), (14, 0, 1.3, "G"),
                                               (19, 2, 2.4, "T"), (23, 5, 0.7, "G"))):
        for y in range(19, top - 1, -1):
            x = x0 + round(1.6 * math.sin((19 - y) / 3.0 + phase))
            c.set(x, y, col)
            c.set(x + 1, y, col)
            if y % 3 == i % 3:
                c.set(x, y, "E")
    c.outline()
    return c


def boot() -> Canvas:
    c = Canvas(32, 20)
    c.rect(14, 2, 23, 14, "M")
    c.rect(13, 1, 24, 3, "A")
    c.rect(15, 4, 15, 12, "A")
    for x, y in ((17, 5), (20, 5), (18, 6), (19, 6), (17, 7), (20, 7), (18, 8), (19, 8), (17, 9), (20, 9)):
        c.set(x, y, "W")
    c.rect(5, 11, 23, 16, "M")
    c.ellipse(7.5, 14.0, 4.5, 3.2, "M")
    c.rect(9, 12, 11, 13, "A")
    c.rect(3, 17, 24, 17, "K")
    c.rect(20, 16, 24, 17, "K")
    c.outline()
    return c


def bottle() -> Canvas:
    c = Canvas(32, 20)
    c.rect(14, 0, 17, 2, "M"); c.set(14, 0, "A")
    c.rect(14, 3, 17, 5, "L")
    c.rect(12, 6, 19, 7, "L")
    c.rect(11, 7, 20, 18, "L")
    c.rect(19, 7, 20, 18, "B")
    c.rect(12, 18, 19, 18, "B")
    c.rect(12, 8, 12, 15, "W")
    c.rect(14, 9, 17, 15, "W")
    for x, y in ((15, 11), (16, 11), (15, 13), (16, 13)):
        c.set(x, y, "D")
    c.rect(14, 9, 17, 9, "Y"); c.rect(14, 15, 17, 15, "Y")
    c.outline()
    return c


def chest() -> Canvas:
    c = Canvas(32, 20)
    c.rect(3, 3, 28, 8, "M")
    c.rect(4, 3, 27, 3, "A")
    c.set(3, 3, None); c.set(28, 3, None)
    c.rect(3, 9, 28, 18, "M")
    c.rect(3, 9, 28, 9, "K")
    c.rect(3, 17, 28, 18, "P")
    c.rect(7, 3, 8, 18, "Y")
    c.rect(23, 3, 24, 18, "Y")
    c.rect(14, 7, 17, 12, "Y")
    c.rect(15, 9, 16, 10, "K")
    for x in (11, 12, 19, 20):  # coins peeking under the lid
        c.set(x, 2, "Y")
    c.outline()
    return c


CATCH_ART = {
    "鲫鱼": lambda: fish("S", "D", "W", "D"),
    "鲤鱼": lambda: fish("A", "M", "Y", "O", fat=1.15, pattern="scales", mark="M", barbel=True),
    "草鱼": lambda: fish("G", "T", "E", "T", fat=0.85),
    "鲈鱼": lambda: fish("D", "N", "S", "R", pattern="stripes", mark="N"),
    "鳜鱼": lambda: fish("A", "M", "Y", "M", fat=1.1, pattern="spots", mark="K"),
    "金色锦鲤": lambda: fish("Y", "O", "W", "O", pattern="koi", mark="R", sparkle=True),
    "水草": weed,
    "旧靴子": boot,
    "漂流瓶": bottle,
    "宝箱": chest,
}


# ---------------------------------------------------------------------------
# Baits
# ---------------------------------------------------------------------------

def worm() -> Canvas:
    c = Canvas(16, 16)
    for i in range(60):
        t = i / 59
        x = round(2 + t * 11)
        y = round(7 + 3 * math.sin(t * 2 * math.pi * 1.1))
        col = "R" if int(t * 8) % 2 == 0 else "O"
        c.set(x, y, col)
        c.set(x, y + 1, col)
    c.set(2, 7, "K")  # eye on the head end
    c.outline()
    return c


def dough() -> Canvas:
    c = Canvas(16, 16)
    c.ellipse(8.0, 8.5, 6.0, 5.5, "Y")
    for x, y, v in list(c.cells()):
        if v and (x - 8) + (y - 8.5) > 4.5:
            c.set(x, y, "A")
    for x, y in ((5, 5), (6, 5), (5, 6)):
        c.set(x, y, "W")
    c.outline()
    c.set(1, 14, "Y"); c.set(14, 2, "Y")
    return c


def lure() -> Canvas:
    c = Canvas(16, 16)
    c.ellipse(8.0, 6.5, 3.5, 4.5, "S")
    c.triangle((5.0, 8.0), (11.0, 8.0), (8.0, 12.5), "S")
    c.set(6, 4, "W"); c.set(6, 5, "W"); c.set(7, 3, "W")
    c.rect(8, 7, 9, 8, "R")
    c.outline()
    c.set(8, 0, "Y")  # split ring
    for x, y in ((8, 13), (8, 14), (8, 15), (7, 15), (6, 15), (5, 14), (5, 13), (6, 13)):
        c.set(x, y, "K")  # hook
    return c


BAIT_ART = {"蚯蚓": worm, "面团": dough, "亮片": lure}


# ---------------------------------------------------------------------------
# Background: dusk sky, setting sun, two hill ridges, pond
# ---------------------------------------------------------------------------

def dither(x: int, y: int, c0: str, c1: str, f: float) -> str:
    return c1 if BAYER4[y % 4][x % 4] < f * 16 else c0


def background() -> Canvas:
    w, h = SCREEN_W // SCALE, -(-SCREEN_H // SCALE)
    c = Canvas(w, h)
    for y in range(HORIZON):
        for x in range(w):
            if y < 14:
                c.set(x, y, dither(x, y, "O", "Y", y / 14))
            elif y < 36:
                c.set(x, y, "Y")
            else:
                c.set(x, y, dither(x, y, "Y", "W", (y - 36) / 12))
    sun = Canvas(w, h)
    sun.ellipse(64.5, 41.5, 7.0, 7.0, "O")
    for x, y, v in sun.cells():
        if v:
            c.set(x, y, "W" if (x - 64) ** 2 + (y - 38) ** 2 < 10 else ("R" if y > 44 and y % 2 else "O"))
    for x in range(w):
        far = 41 + 3 * math.sin(x / 7.0) + 2 * math.sin(x / 3.1 + 1)
        near = 45 + 2 * math.sin(x / 5.0 + 2) + 1.5 * math.sin(x / 2.3)
        for y in range(int(far), HORIZON):
            c.set(x, y, "P")
        for y in range(int(near), HORIZON):
            c.set(x, y, "G" if y == int(near) else "T")
    for tx in (12, 24, 70):  # pines on the far ridge
        base = int(41 + 3 * math.sin(tx / 7.0) + 2 * math.sin(tx / 3.1 + 1))
        for i in range(5):
            for dx in range(-(i // 2), i // 2 + 1):
                c.set(tx + dx, base - 5 + i, "T")
    for y in range(HORIZON, h):
        for x in range(w):
            if y == HORIZON:
                col = "W" if x % 4 == 0 else "L"
            elif y < HORIZON + 8:
                col = dither(x, y, "L", "B", (y - HORIZON) / 8)
            elif y < 80:
                col = "B"
            elif y < 92:
                col = dither(x, y, "B", "N", (y - 80) / 12)
            else:
                col = "N"
            c.set(x, y, col)
    for i, y in enumerate(range(HORIZON + 1, HORIZON + 15, 2)):  # sun glitter on the water
        half = max(1, 4 - i // 2)
        for x in range(64 - half, 64 + half + 1):
            c.set(x, y, "Y" if i < 3 else "L")
    for base_x, sign in ((2, 1), (77, -1)):  # reeds in the bottom corners
        for k, top in enumerate((86, 90, 93)):
            x = base_x + sign * 2 * k
            for y in range(top, h):
                c.set(x, y, "G" if (y + k) % 4 else "E")
            c.set(x, top - 1, "M")
            c.set(x, top - 2, "M")
    return c


# ---------------------------------------------------------------------------
# Animated layers: drifting clouds and scrolling water waves (moved by main/fish_ui.c)
# ---------------------------------------------------------------------------

def rows(*lines: str, **colors: str) -> Canvas:
    """Small sprite from text rows; '.' is transparent, letters map through `colors`."""
    c = Canvas(max(len(r) for r in lines), len(lines))
    for y, line in enumerate(lines):
        for x, ch in enumerate(line):
            if ch != ".":
                c.set(x, y, colors.get(ch, ch))
    return c


# Order matches CLOUDS[] in main/fish_ui.c, which places and moves them.
CLOUD_ART = (
    lambda: rows("....WWW.........",
                 "..WWWWWW..WWW...",
                 ".WWWWWWWWWWWWWW.",
                 "WWWWWWWWWWWWWWWW",
                 ".OOOOOOOOOOOOOO."),
    lambda: rows("..WWW......",
                 ".WWWWW.WW..",
                 "WWWWWWWWWWW",
                 ".OOOOOOOOO."),
    lambda: rows(".WW.WW.",
                 "OOOOOOO"),
)

# Wave crests that scroll right to left across the pond. Order matches WAVES[] in
# main/fish_ui.c; the tone follows the water behind each row (cream foam near the
# horizon, light blue on open water, blue on the deep navy band).
WAVE_SHORT = ("..cccc..",
              "cc....cc")
WAVE_MEDIUM = ("..ccc....cc",
               "cc...cccc..")
WAVE_LONG = ("..ccc.....ccc.",
             "cc...ccccc...c")
WAVE_ART = (
    (WAVE_SHORT, "W"), (WAVE_SHORT, "W"),    # y = 165, foam just below the horizon
    (WAVE_LONG, "L"),                        # y = 228
    (WAVE_LONG, "L"),                        # y = 252
    (WAVE_MEDIUM, "B"), (WAVE_MEDIUM, "B"),  # y = 312, deep water
)


def wave_art() -> list[Canvas]:
    return [rows(*shape, c=tone) for shape, tone in WAVE_ART]


# ---------------------------------------------------------------------------
# Encoding
# ---------------------------------------------------------------------------

def rgb565(rgb: int) -> int:
    r, g, b = rgb >> 16 & 0xFF, rgb >> 8 & 0xFF, rgb & 0xFF
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def scaled(c: Canvas, crop_h: int | None = None) -> tuple[int, int, list[str | None]]:
    w, h = c.w * SCALE, crop_h if crop_h is not None else c.h * SCALE
    return w, h, [c.px[y // SCALE][x // SCALE] for y in range(h) for x in range(w)]


def encode_rgb565(pixels, color_of) -> bytes:
    return b"".join(struct.pack("<H", rgb565(color_of(p)) if p else 0) for p in pixels)


def encode_rgb565a8(pixels, color_of) -> bytes:
    return encode_rgb565(pixels, color_of) + bytes(255 if p else 0 for p in pixels)


def catalog_names(array: str) -> list[str]:
    source = CATALOG.read_text(encoding="utf-8")
    block = re.search(rf"{array}\[[A-Z_]+\] = \{{(.*?)\n\}};", source, re.S)
    if block is None:
        raise SystemExit(f"{array} not found in {CATALOG}")
    return re.findall(r'\.name = "([^"]+)"', block.group(1))


def art_for(names: list[str], table: dict) -> list[Canvas]:
    missing = [n for n in names if n not in table]
    if missing:
        raise SystemExit(f"no pixel art for: {', '.join(missing)} (add it to tools/gen_fish_sprites.py)")
    return [table[n]() for n in names]


def c_bytes(name: str, data: bytes) -> str:
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 24]) for i in range(0, len(data), 24)]
    return (f"static const uint8_t {name}[{len(data)}] __attribute__((aligned(4))) = {{\n    "
            + ",\n    ".join(rows) + "\n};\n")


def descriptor(data_name: str, cf: str, w: int, h: int, size: int) -> str:
    return (f"    {{.header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_{cf}, "
            f".w = {w}, .h = {h}, .stride = {w * 2}}}, .data_size = {size}, .data = {data_name}}}")


def write_c(path: Path) -> None:
    catches = art_for(catalog_names("FISH_ENTRIES"), CATCH_ART)
    baits = art_for(catalog_names("FISH_BAITS"), BAIT_ART)
    out = ["// GENERATED at build time by tools/gen_fish_sprites.py — do not edit.",
           '#include "fish_sprites.h"', ""]
    w, h, px = scaled(background(), SCREEN_H)
    bg = encode_rgb565(px, lambda p: PALETTE[p])
    out.append(c_bytes("bg_data", bg))
    out.append("const lv_image_dsc_t fish_img_background =\n" + descriptor("bg_data", "RGB565", w, h, len(bg)).strip() + ";\n")

    def emit_set(prefix: str, arts: list[Canvas], color_of) -> list[str]:
        descs = []
        for i, art in enumerate(arts):
            w, h, px = scaled(art)
            data = encode_rgb565a8(px, color_of)
            out.append(c_bytes(f"{prefix}_{i}", data))
            descs.append(descriptor(f"{prefix}_{i}", "RGB565A8", w, h, len(data)))
        return descs

    for array, prefix, arts, color_of in (
        ("fish_img_entries", "entry", catches, lambda p: PALETTE[p]),
        ("fish_img_shadows", "shadow", catches, lambda p: SHADOW),
        ("fish_img_baits", "bait", baits, lambda p: PALETTE[p]),
        ("fish_img_clouds", "cloud", [make() for make in CLOUD_ART], lambda p: PALETTE[p]),
        ("fish_img_waves", "wave", wave_art(), lambda p: PALETTE[p]),
    ):
        descs = emit_set(prefix, arts, color_of)
        out.append(f"const lv_image_dsc_t {array}[{len(descs)}] = {{\n" + ",\n".join(descs) + "\n};\n")
    path.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(out)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


def write_png(path: Path, w: int, h: int, rgb: list[int]) -> None:
    raw = b"".join(b"\0" + b"".join(struct.pack(">I", v)[1:] for v in rgb[y * w:(y + 1) * w]) for y in range(h))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_preview(path: Path) -> None:
    """Contact sheet: background, then catches, silhouettes, baits, clouds and waves."""
    catches = art_for(catalog_names("FISH_ENTRIES"), CATCH_ART)
    baits = art_for(catalog_names("FISH_BAITS"), BAIT_ART)
    gap, bgcol = 8, 0x2B2B2B
    tiles: list[tuple[int, int, list, object]] = []
    for art in catches:
        tiles.append((*scaled(art), lambda p: PALETTE[p]))
    for art in catches:
        tiles.append((*scaled(art), lambda p: SHADOW))
    for art in baits:
        tiles.append((*scaled(art), lambda p: PALETTE[p]))
    for art in [make() for make in CLOUD_ART] + wave_art():
        tiles.append((*scaled(art), lambda p: PALETTE[p]))
    cols, tw, th = 5, 96, 60
    n_rows = -(-len(tiles) // cols)
    sheet_w = SCREEN_W + gap * 3 + cols * (tw + gap)
    sheet_h = max(SCREEN_H, n_rows * (th + gap)) + gap * 2
    rgb = [bgcol] * (sheet_w * sheet_h)
    w, h, px = scaled(background(), SCREEN_H)
    for y in range(h):
        for x in range(w):
            rgb[(gap + y) * sheet_w + gap + x] = PALETTE[px[y * w + x]]
    for k, (w, h, px, color_of) in enumerate(tiles):
        ox = SCREEN_W + gap * 2 + (k % cols) * (tw + gap)
        oy = gap + (k // cols) * (th + gap)
        for y in range(h):
            for x in range(w):
                p = px[y * w + x]
                if p:
                    rgb[(oy + y) * sheet_w + ox + x] = color_of(p)
    write_png(path, sheet_w, sheet_h, rgb)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--c-out", type=Path, help="write the LVGL image C source here")
    parser.add_argument("--preview", type=Path, help="write a PNG contact sheet here")
    args = parser.parse_args()
    if args.c_out is None and args.preview is None:
        parser.error("nothing to do: pass --c-out and/or --preview")
    if args.c_out:
        write_c(args.c_out)
    if args.preview:
        write_preview(args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
