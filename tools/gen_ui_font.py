# -*- coding: utf-8 -*-
r"""
Generate a compact 1bpp C font header for the STM32 scope UI.

Input : font_chinese_14.h          (LVGL v8 "fmt_txt", simsun 14px, 4bpp)
Output: STM32F407_ProjectTemplate\bsp\ui\ui_font_cn14.h

The LVGL font is not usable on the STM32 as-is: it needs pgmspace.h, lv_font_t
and a 4bpp LVGL renderer.  This script decodes the subset of glyphs the UI
actually needs (all printable ASCII + the CJK set below) into plain 1bpp
row-major bitmaps that bsp_ui.c can blit straight to the TFT.

LVGL 4bpp layout (verified against the glyph_dsc bitmap_index values):
    stride = (box_w + 1) // 2 bytes per row, each row padded to a whole byte
    pixel (row, col): byte = row*stride + col//2, high nibble = even col
    nibble >= 8  ->  ink
Glyph placement: ofs_y is the distance from the baseline UP to the glyph top,
so the glyph's first row inside the line box is (LINE_H - BASE_LINE) - ofs_y.
adv_w is in 1/16 pixel units.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "font_chinese_14.h")
OUT = os.path.join(ROOT, "STM32F407_ProjectTemplate", "bsp", "ui", "ui_font_cn14.h")

LINE_H = 15
BASE_LINE = 2          # from the lv_font_t initialiser
BASE_Y = LINE_H - BASE_LINE

# CJK characters baked into the UI font.  To support new text just add the
# characters here and re-run the script.
CJK = (
    "双核示波器时基电压通道一二三退出开关运行停止采样率峰值"
    "触发模式自动单次常态边沿上升下降水平垂直菜单设置测量"
    "周期频幅度位置格伏毫微秒千兆大小最平均有效同步脉宽占"
    "空比正负直流交流耦合带宽限制探头衰减校准自检复位保存"
    "加载闪烁蜂鸣亮对睡眠唤醒池量充低刷新帧缓冲深预后插值"
    "斜率窗口计数直方图记录回放信连接网端"
)

# ----------------------------------------------------------------------
# parse the LVGL header
# ----------------------------------------------------------------------
text = open(SRC, encoding="utf-8-sig", errors="replace").read()


def array_body(name, tail="\n};"):
    m = re.search(re.escape(name) + r"\[\][^{]*\{(.*?)" + re.escape(tail), text, re.S)
    if not m:
        sys.exit("cannot find array " + name)
    return m.group(1)


bitmap = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", array_body("font_chinese_14_glyph_bitmap")))

dsc = []
for e in re.finditer(
    r"\{\.bitmap_index=(\d+),\.adv_w=(\d+),\.box_w=(\d+),"
    r"\.box_h=(\d+),\.ofs_x=(-?\d+),\.ofs_y=(-?\d+)\}",
    array_body("font_chinese_14_glyph_dsc"),
):
    dsc.append(tuple(int(v) for v in e.groups()))

uni_list = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4})", array_body("font_chinese_14_cmap_unicode_list"))]

# glyph id -> unicode :  cmap0 = 32..126 dense, cmap1 = sparse list from id 95
id2uni = {}
for i in range(95):
    id2uni[i] = 32 + i
for k, u in enumerate(uni_list):
    id2uni[95 + k] = u
uni2id = {u: i for i, u in id2uni.items()}

print("bitmap bytes : %d" % len(bitmap))
print("glyph count  : %d" % len(dsc))
print("unicode count: %d (ids 95..%d)" % (len(uni_list), 94 + len(uni_list)))


# ----------------------------------------------------------------------
# decode one glyph to 1bpp row-major
# ----------------------------------------------------------------------
def decode(uid):
    bidx, adv_w, w, h, ox, oy = dsc[uid]
    stride4 = (w + 1) // 2
    stride1 = (w + 7) // 8
    out = bytearray(stride1 * h)
    for row in range(h):
        base = bidx + row * stride4
        for col in range(w):
            b = bitmap[base + col // 2]
            nib = (b >> 4) if (col % 2 == 0) else (b & 0x0F)
            if nib >= 4:          # ink; the ASCII glyphs are heavily antialiased
                out[row * stride1 + (col >> 3)] |= 0x80 >> (col & 7)
    return {
        "uni": id2uni[uid],
        "adv": (adv_w + 8) >> 4,
        "w": w,
        "h": h,
        "ox": ox,
        "oy": BASE_Y - oy,          # top row inside the line box
        "bits": bytes(out),
    }


wanted = [ord(c) for c in sorted(set(CJK + "".join(chr(c) for c in range(32, 127))))]
glyphs = []
missing = []
for u in wanted:
    uid = uni2id.get(u)
    if uid is None:
        missing.append(u)
        continue
    glyphs.append(decode(uid))

glyphs.sort(key=lambda g: g["uni"])

# Override: use a narrow (4 px) space instead of the LVGL full-width (14 px)
# space so mixed CJK/ASCII lines stay compact and readable.
for g in glyphs:
    if g["uni"] == 0x20:
        g["adv"] = 4

if missing:
    print("MISSING (%d): %s" % (len(missing), "".join(chr(u) for u in missing)))
else:
    print("missing      : none")

bits = bytearray()
for g in glyphs:
    g["off"] = len(bits)
    bits += g["bits"]
    while len(bits) & 1:
        bits.append(0)

print("glyphs kept  : %d" % len(glyphs))
print("bitmap out   : %d bytes" % len(bits))


# ----------------------------------------------------------------------
# emit C
# ----------------------------------------------------------------------
def hex_lines(data, per=16):
    lines = []
    for i in range(0, len(data), per):
        lines.append("".join("0x%02X," % b for b in data[i:i + per]))
    return "\n".join(lines)


with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write("/* Auto-generated by tools/gen_ui_font.py -- DO NOT EDIT.\n")
    f.write(" * Source : font_chinese_14.h (LVGL v8 fmt_txt, simsun 14px, 4bpp)\n")
    f.write(" * Format : 1bpp, row-major, stride=(w+7)/8, MSB = leftmost pixel.\n")
    f.write(" * Glyphs : %d (printable ASCII + the CJK set in gen_ui_font.py)\n" % len(glyphs))
    f.write(" */\n")
    f.write("#ifndef UI_FONT_CN14_H\n#define UI_FONT_CN14_H\n\n")
    f.write("#define UI_F14_LINE_H  %d\n" % LINE_H)
    f.write("#define UI_F14_BASE_Y  %d\n" % BASE_Y)
    f.write("#define UI_F14_N       %d\n\n" % len(glyphs))
    f.write("typedef struct {\n")
    f.write("    unsigned short uni;   /* unicode codepoint      */\n")
    f.write("    unsigned char  adv;   /* advance width, pixels  */\n")
    f.write("    unsigned char  w;     /* bitmap box width       */\n")
    f.write("    unsigned char  h;     /* bitmap box height      */\n")
    f.write("    signed char    ox;    /* left edge from pen     */\n")
    f.write("    signed char    oy;    /* top row in the line box*/\n")
    f.write("    unsigned short off;   /* byte offset into bits  */\n")
    f.write("} ui_f14_glyph_t;\n\n")
    f.write("static const unsigned char ui_f14_bits[] = {\n")
    f.write(hex_lines(bits))
    f.write("\n};\n\n")
    f.write("static const ui_f14_glyph_t ui_f14_glyphs[UI_F14_N] = {\n")
    for g in glyphs:
        f.write("{0x%04X,%d,%d,%d,%d,%d,%d},\n" % (
            g["uni"], g["adv"], g["w"], g["h"], g["ox"], g["oy"], g["off"]))
    f.write("};\n\n#endif /* UI_FONT_CN14_H */\n")

print("wrote %s (%d bytes)" % (OUT, os.path.getsize(OUT)))
