# bakefont

把 TrueType 字体离线烘成 `swinx/src/gdi/freertos/sfr_font_data.h` 里的位图字模表。
**这是宿主工具**：只用一次（或改动字号/字体/码位范围时重跑），产物入库，target
构建既不依赖 FreeType，也不依赖 ttf 文件存在。

## 为什么烘而不是在目标上跑 FreeType

| | FreeType | 烘焙位图 |
| --- | --- | --- |
| 代码体积 | ~300KB + 字形缓存 | 0（只有一张查表 + cairo mask 回调，约 0.5KB） |
| 数据体积 | ttf 文件（数百 KB 起） | 每面 5-9KB，本次 10 面共 67KB |
| advance | 分数，逐字累加会漂移 | **整数**，测宽与绘制同源，天然一致 |
| 换字号 | 任意 | 吸附到最近烘焙字号 + cairo 缩放 |
| 换语种 | 全字库 | 只覆盖烘焙时给的码位 |

320x240 / 4MB flash 量级的设备上，这笔账很清楚。代价写在最前面：**字形集合是
穷举的**，没烘的字符统一落到 `?`。

## 构建

需要 FreeType 头文件与库。MSYS2/mingw 上：

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"     # 少了这句会 rc=127（找不到 libfreetype-6.dll）
gcc -O2 -Wall -o bakefont.exe bakefont.c \
    -IC:/msys64/mingw64/include/freetype2 -LC:/msys64/mingw64/lib -lfreetype
```

装了 `pkg-config` 的话等价于：

```bash
gcc -O2 -Wall -o bakefont bakefont.c $(pkg-config --cflags --libs freetype2)
```

两个踩过的坑：

- **路径要写成 `C:/...` 而不是 `/c/...`**：FreeType 的 `FT_New_Face` 在 Windows
  上不认 MSYS 风格路径。
- **`./bakefont.exe` 直接跑会 rc=127**：DLL 在 `mingw64/bin`，务必先 export PATH。

## 运行

```bash
./bakefont.exe \
    --regular C:/msys64/mingw64/share/doc/qemu/_static/fonts/Lato/lato-regular.ttf \
    --bold    C:/msys64/mingw64/share/doc/qemu/_static/fonts/Lato/lato-bold.ttf \
    --sizes 12,13,14,15,16 \
    --chars 0x20-0x7E \
    --name Lato \
    --out ../../src/gdi/freertos/sfr_font_data.h
```

| 参数 | 说明 |
| --- | --- |
| `--regular` / `--bold` | 两个 ttf；只给 `--regular` 时 bold 缺省也用它（只是不加粗） |
| `--sizes` | **em 尺寸**，即 swinx 传给 `cairo_set_font_size(abs(lfHeight))` 的那个数。行高会等于该面的 ascent+descent（Lato 约 1.08 em），不是请求值 |
| `--chars` | 码位范围，可重复/逗号分隔；只收 `FT_PIXEL_MODE_GRAY` 的字形 |
| `--name` | 只写进头文件注释，运行时不用 |
| `--out` | 输出头文件路径 |

当前仓库里的 `sfr_font_data.h` 就是上面这条命令的产物（Lato，OFL 许可），10 个
面（5 个字号 × regular/bold），每面 95 个字形：

```
{12,10,3,0,95,12,5076}  {13,11,3,0,95,13,5795}  {14,12,3,0,95,15,6812}
{15,13,3,0,95,15,7347}  {16,13,4,0,95,16,8388}   # px,asc,desc,bold,glyphs,maxAdv,bits
```

## 生成的结构

```c
typedef struct SfrBakedGlyph { uint16_t code; uint8_t w, h; int8_t left, top;
                               uint8_t adv; uint32_t off; } SfrBakedGlyph;
typedef struct SfrBakedFace  { uint16_t px; int8_t ascent, descent; uint8_t bold;
                               uint16_t glyphCount; uint8_t maxAdv; uint32_t bitsLen;
                               const SfrBakedGlyph *glyphs; const uint8_t *bits; } SfrBakedFace;
```

`left/top` 是相对笔位置的整数像素偏移（`top` 向上为正），`adv` 是整数 advance，
`bits` 是紧密排布的 8-bit alpha 块（每字形 `w*h` 字节，无 stride）。`glyphs` 按
码位升序，运行时二分查找。

**只包含在一个 TU 里**：数组是 `static`，被多处包含会让 67KB 进 flash 多份 ——
只有 `src/gdi/freertos/sfr_font.cpp` 包含它。

## 换成别的字体 / 加中文

重跑命令即可，注意三点：

1. 中文字形数大，**不要烘全字库**。按业务文案取子集（`--chars` 支持多段范围），
   16px 一个汉字的 alpha 位图约 150-300B。
2. `--sizes` 之外的字号会吸附 + 缩放，中文字形在小字号上缩放模糊更明显，建议把
   实际用到的字号都烘一遍（每个面 +几 KB）。
3. 烘完跑一次字体自检确认没变形：

   ```bash
   cd soui-freertos/soui_sim/build
   SOUI_SIM_FONTPROBE=1 SDL_VIDEODRIVER=dummy ./soui_sim.exe
   ```

   自检里的三串文本是 ASCII，中文子集的验证要靠 `SOUI_SIM_FONTPROBE_DUMP=<dir>`
   导出的条带图（或直接跑模拟器看界面）。
