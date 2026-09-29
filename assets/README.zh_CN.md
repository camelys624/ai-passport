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

### 口袋垂钓 Noto Sans SC 子集

供“口袋垂钓”固件使用（由 `main/CMakeLists.txt` 编译；`main/fish_fonts.c` 选用，
Montserrat 回退只用于 `LV_SYMBOL_*` 电池图标）。

| 文件 | 字号 / bpp | 源字重 | 覆盖范围 | Flash（.rodata） |
| --- | --- | --- | --- | ---: |
| [`fonts/fish_font_16.c`](fonts/fish_font_16.c) | 16 px / 4 | Regular | ASCII、标点、全部界面文字 | 40,395 B |
| [`fonts/fish_font_24.c`](fonts/fish_font_24.c) | 24 px / 4 | Medium | ASCII、标点、全部界面文字 | 83,677 B |
| [`fonts/fish_font_40.c`](fonts/fish_font_40.c) | 40 px / 4 | Medium | 仅 `FISH_LARGE_TEXT`（`▲▼OK咬钩了！`） | 4,315 B |

- 来源：Noto Sans SC 2.004，`Sans/SubsetOTF/SC/NotoSansSC-Regular.otf`（SHA-256
  `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`）与
  `NotoSansSC-Medium.otf`（SHA-256
  `7633f5a016d4dd95e685a69633d818aabc4644c4b08e26bd35b1b30c45ed5dda`），取自
  <https://github.com/notofonts/noto-cjk/tree/Sans2.004>（提交
  `523d033d6cb47f4a80c58a35753646f5c3608a78`）。OTF 文件不提交到仓库。
- 许可：SIL Open Font License 1.1，全文见
  [`fonts/NotoSansSC-OFL.txt`](fonts/NotoSansSC-OFL.txt)。生成的位图属于修改版本，
  使用自己的名称（未使用保留字体名称）。
- 字符清单：[`tools/gen_fish_fonts.py`](../tools/gen_fish_fonts.py) 收集
  `main/fish_*.c/.h` 字符串字面量中的全部码点，加上可打印 ASCII 与固定标点表，并生成
  开机字形自检用的 `main/fish_ui_charset.h`。40 px 字体只覆盖 `main/fish_fonts.h`
  中的 `FISH_LARGE_TEXT`。
- 转换工具：通过 `npx` 调用 `lv_font_conv` 1.5.3，输出兼容 LVGL 9.5.0。
  修改任何界面文字或渔获条目后重新生成：

  ```bash
  mkdir -p ~/.cache/noto-sans-sc && cd ~/.cache/noto-sans-sc
  base=https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78
  curl -fLO "$base/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf"
  curl -fLO "$base/Sans/SubsetOTF/SC/NotoSansSC-Medium.otf"
  cd - && python3 tools/gen_fish_fonts.py --font-dir ~/.cache/noto-sans-sc
  ```

- 验证：`tests/test_fish_fonts.py`（属于 `./tools/validate.sh --static`）解析生成的
  cmap，界面文字、渔获名称或大号提示字形缺失时失败，并以 U+9F98 作为必然缺失的反例。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |
| [`images/fishing-sprites-preview.png`](images/fishing-sprites-preview.png) | 784 × 492，PNG RGB | “口袋垂钓”像素素材的实际显示尺寸总览：背景、渔获、剪影、饵料图标、云与水波纹。由 `tools/gen_fish_sprites.py --preview` 生成，仅供查看，固件不使用。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

### 口袋垂钓 8-bit 像素素材

- 来源：[`tools/gen_fish_sprites.py`](../tools/gen_fish_sprites.py) 以程序方式绘制的原创素材
  （形状、固定 16 色调色板、自动 1 px 描边），为本仓库编写，不含第三方图片。
- 集成：`main/CMakeLists.txt` 在构建时运行生成脚本，并编译构建目录中的输出；放大后的数组
  不提交到仓库。`main/fish_sprites.h` 声明这些图片；素材顺序读取自 `main/fish_catalog.c`，
  图鉴新增条目却没有对应画法时构建失败。
- 格式：原生像素按 ×3 最近邻放大。背景为未压缩 RGB565，精灵为 RGB565A8；LVGL 9.5 直接从
  Flash 绘制，不解码到 RAM。不要在运行时缩放这些图片。

| 图片组 | 原生 → 显示尺寸 | Flash |
| --- | --- | ---: |
| 背景（傍晚天空、落日、山丘、湖面；水平线在 y = 150；不含云和水波纹） | 80 × 107 → 240 × 320 | 153,600 B |
| 渔获，每个 `FISH_ENTRIES` 条目一张 | 32 × 20 → 96 × 60 | 172,800 B |
| 同一批渔获的未发现剪影 | 32 × 20 → 96 × 60 | 172,800 B |
| 饵料图标，每个 `FISH_BAITS` 条目一张 | 16 × 16 → 48 × 48 | 20,736 B |
| 三朵飘动的云 | 16 × 5、11 × 4、7 × 2 → ×3 | 3,726 B |
| 水波纹：短、长、中三种波峰，奶白、浅蓝、蓝三种颜色 | 8 × 2、14 × 2、11 × 2 → ×3 | 3,564 B |

- 动效：`main/fish_ui.c` 用一个 100 ms 的 LVGL 定时器让云和水波纹从右向左移动，每次都按
  整个像素点（3 px）移动，保留 8-bit 的顿挫感。云约 2–3 px/s，水波纹约 3–6 px/s。
  云和水波纹只放在各页面都没有文字的行。黑屏时定时器停止。

- 修改画法后重新生成总览图：
  `python3 tools/gen_fish_sprites.py --preview assets/images/fishing-sprites-preview.png`。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
