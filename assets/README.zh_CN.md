<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### Codeloom 思源黑体（Noto Sans SC）子集

供 [Codeloom 审批器](../docs/reference/camelys624/codeloom/README.zh_CN.md) 固件使用
（由 `main/CMakeLists.txt` 编译；`main/codeloom_fonts.c` 选用，并以 Montserrat 作为
`LV_SYMBOL_*` 图标的回退字体）。

| 文件 | 字号 / bpp | 源字重 | 覆盖范围 | Flash（.rodata） |
| --- | --- | --- | --- | ---: |
| [`fonts/codeloom_font_14.c`](fonts/codeloom_font_14.c) | 14 px / 4 | Regular | ASCII、标点、全部界面文字 | 35,940 B |
| [`fonts/codeloom_font_16.c`](fonts/codeloom_font_16.c) | 16 px / 4 | Regular | ASCII、标点、界面文字、全部 3755 个 GB2312 一级汉字 | 496,286 B |
| [`fonts/codeloom_font_22.c`](fonts/codeloom_font_22.c) | 22 px / 4 | Medium | ASCII、标点、全部界面文字 | 78,052 B |

- 来源：Noto Sans SC 2.004，`Sans/SubsetOTF/SC/NotoSansSC-Regular.otf`（SHA-256
  `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`）和
  `NotoSansSC-Medium.otf`（SHA-256
  `7633f5a016d4dd95e685a69633d818aabc4644c4b08e26bd35b1b30c45ed5dda`），取自
  <https://github.com/notofonts/noto-cjk/tree/Sans2.004>（commit
  `523d033d6cb47f4a80c58a35753646f5c3608a78`）。OTF 文件不提交到仓库。
- 许可：SIL Open Font License 1.1，全文见 [`fonts/NotoSansSC-OFL.txt`](fonts/NotoSansSC-OFL.txt)。
  生成的位图属于修改版本，使用自己的名称（未使用保留字体名）。
- 字符表：[`tools/gen_codeloom_fonts.py`](../tools/gen_codeloom_fonts.py) 收集
  `main/codeloom_*.c/.h` 字符串字面量中的全部码点，加入可打印 ASCII 和一组固定的中文/全角
  标点（以及用于替换不支持动态字符的 U+25A1 方框）；16 px 另加 GB2312 一级汉字。脚本同时
  生成供开机字形自检使用的 `main/codeloom_ui_charset.h`。
- 转换器：通过 `npx` 使用 `lv_font_conv` 1.5.3，输出与 LVGL 9.5.0 兼容。修改任何界面文字后重新生成：

  ```bash
  mkdir -p ~/.cache/codeloom-fonts && cd ~/.cache/codeloom-fonts
  base=https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78
  curl -fLO "$base/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf"
  curl -fLO "$base/Sans/SubsetOTF/SC/NotoSansSC-Medium.otf"
  cd - && python3 tools/gen_codeloom_fonts.py --font-dir ~/.cache/codeloom-fonts
  ```

  每个字号执行 `npx -y lv_font_conv@1.5.3 --font <otf> -r 0x20-0x7E --symbols <字符表>
  --size <px> --bpp 4 --format lvgl --no-compress --lv-font-name codeloom_font_<px>
  --lv-include lvgl.h -o assets/fonts/codeloom_font_<px>.c`。
- 验证：`tests/test_codeloom_fonts.py`（属于 `./tools/validate.sh --static`）解析生成字体的
  cmap，缺少任何界面文字、标点或 GB2312 一级汉字都会失败；U+9F98 作为已知缺失的反例。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
