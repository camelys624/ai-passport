<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

### Codeloom Noto Sans SC subsets

Used by the [Codeloom Approver](../docs/reference/camelys624/codeloom/README.md) firmware
(`main/CMakeLists.txt` compiles them; `main/codeloom_fonts.c` selects them with a
Montserrat fallback for `LV_SYMBOL_*` icons).

| File | Size / bpp | Source weight | Coverage | Flash (.rodata) |
| --- | --- | --- | --- | ---: |
| [`fonts/codeloom_font_14.c`](fonts/codeloom_font_14.c) | 14 px / 4 | Regular | ASCII, punctuation, every UI string | 35,940 B |
| [`fonts/codeloom_font_16.c`](fonts/codeloom_font_16.c) | 16 px / 4 | Regular | ASCII, punctuation, UI strings, all 3755 GB2312 level-1 hanzi | 496,286 B |
| [`fonts/codeloom_font_22.c`](fonts/codeloom_font_22.c) | 22 px / 4 | Medium | ASCII, punctuation, every UI string | 78,052 B |

- Source: Noto Sans SC 2.004, `Sans/SubsetOTF/SC/NotoSansSC-Regular.otf` (SHA-256
  `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`) and
  `NotoSansSC-Medium.otf` (SHA-256
  `7633f5a016d4dd95e685a69633d818aabc4644c4b08e26bd35b1b30c45ed5dda`) from
  <https://github.com/notofonts/noto-cjk/tree/Sans2.004> (commit
  `523d033d6cb47f4a80c58a35753646f5c3608a78`). The OTF files are not committed.
- License: SIL Open Font License 1.1, copied to
  [`fonts/NotoSansSC-OFL.txt`](fonts/NotoSansSC-OFL.txt). The generated bitmaps are a
  modified version and use their own names (no Reserved Font Name).
- Character inventory: [`tools/gen_codeloom_fonts.py`](../tools/gen_codeloom_fonts.py)
  collects every code point in string literals of `main/codeloom_*.c/.h`, adds
  printable ASCII and a fixed CJK/fullwidth punctuation list (plus U+25A1, the
  replacement box for unsupported dynamic text), and for 16 px adds GB2312 level 1.
  It also writes `main/codeloom_ui_charset.h` for the boot-time glyph self-check.
- Converter: `lv_font_conv` 1.5.3 through `npx`, output compatible with LVGL 9.5.0.
  Regenerate after changing any UI string:

  ```bash
  mkdir -p ~/.cache/codeloom-fonts && cd ~/.cache/codeloom-fonts
  base=https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78
  curl -fLO "$base/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf"
  curl -fLO "$base/Sans/SubsetOTF/SC/NotoSansSC-Medium.otf"
  cd - && python3 tools/gen_codeloom_fonts.py --font-dir ~/.cache/codeloom-fonts
  ```

  Each size runs `npx -y lv_font_conv@1.5.3 --font <otf> -r 0x20-0x7E --symbols <inventory>
  --size <px> --bpp 4 --format lvgl --no-compress --lv-font-name codeloom_font_<px>
  --lv-include lvgl.h -o assets/fonts/codeloom_font_<px>.c`.
- Verification: `tests/test_codeloom_fonts.py` (part of `./tools/validate.sh --static`)
  parses the generated cmaps and fails when a UI string, punctuation mark, or GB2312
  level-1 character is missing, with U+9F98 as a known-absent negative case.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
