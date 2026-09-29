#!/usr/bin/env python3
"""Glyph coverage checks for the generated Codeloom fonts.

The committed lv_font_conv sources are parsed exactly like LVGL's fmt_txt cmap
lookup (format0/sparse "tiny" tables) and compared with the UI inventory that
tools/gen_codeloom_fonts.py extracts from main/codeloom_*.c/.h string literals.
A new UI string without regenerated fonts fails here, before any firmware build.
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import gen_codeloom_fonts as gen  # noqa: E402

CMAP_RE = re.compile(
    r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = \d+,\s*"
    r"\.unicode_list = (\w+), \.glyph_id_ofs_list = (\w+), \.list_length = (\d+), "
    r"\.type = LV_FONT_FMT_TXT_CMAP_(\w+)"
)


def font_codepoints(name: str) -> set[int]:
    source = (gen.FONT_DIR / f"{name}.c").read_text(encoding="utf-8")
    points: set[int] = set()
    cmaps = CMAP_RE.findall(source)
    if not cmaps:
        raise AssertionError(f"{name}: no cmaps found")
    for start, length, unicode_list, ofs_list, list_length, kind in cmaps:
        start, length, list_length = int(start), int(length), int(list_length)
        if kind == "FORMAT0_TINY":
            points.update(range(start, start + length))
        elif kind == "SPARSE_TINY":
            match = re.search(
                rf"static const uint16_t {unicode_list}\[\] = \{{(.*?)\}};", source, re.S
            )
            if match is None:
                raise AssertionError(f"{name}: {unicode_list} not found")
            offsets = [int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\d+", match.group(1))]
            if len(offsets) != list_length:
                raise AssertionError(f"{name}: {unicode_list} length mismatch")
            points.update(start + offset for offset in offsets)
        else:
            raise AssertionError(f"{name}: unsupported cmap type {kind} (ofs {ofs_list})")
    return points


def missing(required: set[int], available: set[int]) -> str:
    return " ".join(f"U+{cp:04X}" for cp in sorted(required - available))


class CodeloomFontCoverageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.fonts = {name: font_codepoints(name) for name, *_ in gen.FONTS}
        cls.ui = gen.ui_codepoints()
        cls.base = gen.base_codepoints()

    def test_ui_strings_are_covered_by_every_size(self) -> None:
        required = self.ui | self.base
        for name, points in self.fonts.items():
            with self.subTest(font=name):
                self.assertEqual(missing(required, points), "",
                                 "regenerate with tools/gen_codeloom_fonts.py")

    def test_body_font_covers_gb2312_level1(self) -> None:
        level1 = gen.gb2312_level1()
        self.assertEqual(len(level1), 3755)
        self.assertEqual(missing(level1, self.fonts["codeloom_font_16"]), "")

    def test_known_missing_character_is_reported(self) -> None:
        # U+9F98 is outside GB2312 level 1; if it were "covered" the parser would be broken.
        self.assertNotIn(0x9F98, self.fonts["codeloom_font_16"])
        self.assertEqual(missing({0x9F98, 0x4E2D}, self.fonts["codeloom_font_16"]), "U+9F98")

    def test_charset_header_matches_sources(self) -> None:
        header = gen.CHARSET_HEADER.read_text(encoding="utf-8")
        literal = "".join(gen.c_string_literals(header))
        self.assertEqual({ord(c) for c in literal}, self.ui | self.base,
                         "main/codeloom_ui_charset.h is stale; rerun the generator")

    def test_generated_sources_contain_no_machine_paths(self) -> None:
        for name, *_ in gen.FONTS:
            head = (gen.FONT_DIR / f"{name}.c").read_text(encoding="utf-8")[:40000]
            with self.subTest(font=name):
                self.assertNotRegex(head, r"/home/|/Users/|[A-Za-z]:\\\\")

    def test_literal_extraction_ignores_comments_and_decodes_escapes(self) -> None:
        source = '/* "注释" */ a("中\\xE2\\x80\\xA6"); // "忽略"\nb(\'"\'); c("x\\"y");'
        self.assertEqual(gen.c_string_literals(source), ["中…", 'x"y'])


if __name__ == "__main__":
    unittest.main()
