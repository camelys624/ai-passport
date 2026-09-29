#!/usr/bin/env python3
"""Generate the Pocket Fishing LVGL font subsets and the UI character inventory.

Reproducible pipeline (see assets/README.md):

  1. Collect every code point used in C string literals of main/fish_*.c/.h
     (comments are ignored), plus printable ASCII and a fixed punctuation set.
  2. 16 px (Regular) and 24 px (Medium) cover that inventory. 40 px (Medium)
     covers only FISH_LARGE_TEXT from main/fish_fonts.h (reel prompts and the
     bite headline), which keeps the large font tiny.
  3. Write main/fish_ui_charset.h (used by the on-device glyph self-check)
     and run lv_font_conv 1.5.3 for each size.

Usage:
  python3 tools/gen_fish_fonts.py --font-dir ~/.cache/noto-sans-sc
  python3 tools/gen_fish_fonts.py --charset-only   # header only, no npx
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAIN_DIR = ROOT / "main"
CHARSET_HEADER = MAIN_DIR / "fish_ui_charset.h"
FONTS_HEADER = MAIN_DIR / "fish_fonts.h"
FONT_DIR = ROOT / "assets" / "fonts"
LV_FONT_CONV = "lv_font_conv@1.5.3"

# Noto Sans SC 2.004 (tag Sans2.004, commit 523d033d6cb47f4a80c58a35753646f5c3608a78),
# SubsetOTF/SC. SHA-256 recorded in assets/README.md.
SOURCES = {
    "Regular": "NotoSansSC-Regular.otf",
    "Medium": "NotoSansSC-Medium.otf",
}

# (symbol name, pixel size, source weight, large-text-only)
FONTS = (
    ("fish_font_16", 16, "Regular", False),
    ("fish_font_24", 24, "Medium", False),
    ("fish_font_40", 40, "Medium", True),
)

PUNCTUATION = "，。、：；？！“”（）…·—–\u3000"

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))


def c_string_literals(source: str) -> list[str]:
    """Return decoded C string literals, skipping comments and char literals."""
    out: list[str] = []
    i = 0
    n = len(source)
    while i < n:
        c = source[i]
        if source.startswith("//", i):
            j = source.find("\n", i)
            i = n if j < 0 else j
        elif source.startswith("/*", i):
            j = source.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == "'":
            i += 1
            while i < n and source[i] != "'":
                i += 2 if source[i] == "\\" else 1
            i += 1
        elif c == '"':
            i += 1
            raw = bytearray()
            while i < n and source[i] != '"':
                ch = source[i]
                if ch == "\\":
                    i, value = _escape(source, i + 1)
                    raw += value
                else:
                    raw += ch.encode("utf-8")
                    i += 1
            i += 1
            out.append(raw.decode("utf-8", errors="strict"))
        else:
            i += 1
    return out


def _escape(source: str, i: int) -> tuple[int, bytes]:
    simple = {"n": b"\n", "t": b"\t", "r": b"\r", "0": b"\0", "\\": b"\\", '"': b'"', "'": b"'",
              "a": b"\a", "b": b"\b", "f": b"\f", "v": b"\v", "?": b"?"}
    c = source[i]
    if c == "x":
        m = re.match(r"[0-9A-Fa-f]+", source[i + 1:])
        assert m, "invalid \\x escape"
        return i + 1 + len(m.group(0)), bytes([int(m.group(0), 16) & 0xFF])
    if c == "u":
        return i + 5, chr(int(source[i + 1:i + 5], 16)).encode("utf-8")
    if c == "U":
        return i + 9, chr(int(source[i + 1:i + 9], 16)).encode("utf-8")
    if c in "01234567":
        m = re.match(r"[0-7]{1,3}", source[i:])
        return i + len(m.group(0)), bytes([int(m.group(0), 8) & 0xFF])
    return i + 1, simple[c]


def ui_source_files() -> list[Path]:
    return sorted(p for p in MAIN_DIR.glob("fish_*.[ch]") if p != CHARSET_HEADER)


def printable(text: str) -> set[int]:
    # LV_SYMBOL_* live in the private-use area and come from the Montserrat fallback.
    return {ord(ch) for ch in text
            if ord(ch) >= 0x20 and ord(ch) != 0x7F and not 0xE000 <= ord(ch) <= 0xF8FF}


def ui_codepoints() -> set[int]:
    points: set[int] = set()
    for path in ui_source_files():
        for literal in c_string_literals(path.read_text(encoding="utf-8")):
            points |= printable(literal)
    return points


def large_text() -> str:
    match = re.search(r'#define FISH_LARGE_TEXT "([^"]*)"', FONTS_HEADER.read_text(encoding="utf-8"))
    if match is None:
        raise SystemExit("FISH_LARGE_TEXT not found in main/fish_fonts.h")
    return match.group(1)


def large_codepoints() -> set[int]:
    return printable(large_text())


def base_codepoints() -> set[int]:
    return {ord(c) for c in ASCII + PUNCTUATION}


def font_codepoints(large_only: bool) -> set[int]:
    return large_codepoints() if large_only else base_codepoints() | ui_codepoints()


def c_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def write_charset_header() -> None:
    chars = "".join(chr(cp) for cp in sorted(ui_codepoints() | base_codepoints()))
    lines = [chars[i:i + 32] for i in range(0, len(chars), 32)]
    body = "\n".join(f'    "{c_escape(line)}"' for line in lines)
    CHARSET_HEADER.write_text(
        "// main/fish_ui_charset.h — GENERATED by tools/gen_fish_fonts.py; do not edit.\n"
        "// 界面固定文字、ASCII 与常用标点的全部码点（UTF-8），供开机字形自检使用。\n"
        "#pragma once\n\n"
        "#define FISH_UI_CHARSET \\\n" + body.replace("\n", " \\\n") + "\n",
        encoding="utf-8",
    )


def convert(font_dir: Path) -> None:
    for name, size, weight, large_only in FONTS:
        points = sorted(font_codepoints(large_only))
        command = ["npx", "-y", LV_FONT_CONV, "--font", str(font_dir / SOURCES[weight])]
        if large_only:
            command += ["--symbols", "".join(chr(cp) for cp in points)]
        else:
            command += ["-r", "0x20-0x7E", "--symbols", "".join(chr(cp) for cp in points if cp > 0x7E)]
        command += [
            "--size", str(size), "--bpp", "4", "--format", "lvgl", "--no-compress",
            "--lv-font-name", name, "--lv-include", "lvgl.h",
            "-o", f"assets/fonts/{name}.c",
        ]
        print(f"lv_font_conv {name}: {len(points)} code points", flush=True)
        subprocess.run(command, check=True, cwd=ROOT)
        # The converter records its options in a comment; keep machine paths out of Git.
        output = FONT_DIR / f"{name}.c"
        text = output.read_text(encoding="utf-8")
        output.write_text(text.replace(str(font_dir), "<noto-sans-sc-dir>"), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--font-dir", type=Path, help="directory with the Noto Sans SC OTF files")
    parser.add_argument("--charset-only", action="store_true")
    args = parser.parse_args()
    write_charset_header()
    if args.charset_only:
        return 0
    if args.font_dir is None:
        parser.error("--font-dir is required unless --charset-only")
    FONT_DIR.mkdir(parents=True, exist_ok=True)
    convert(args.font_dir.expanduser())
    return 0


if __name__ == "__main__":
    sys.exit(main())
