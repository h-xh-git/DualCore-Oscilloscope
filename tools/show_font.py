# -*- coding: utf-8 -*-
"""Render characters from the generated ui_font_cn14.h as ASCII art.

    python tools/show_font.py 时基通道1
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDR = os.path.join(ROOT, "STM32F407_ProjectTemplate", "bsp", "ui", "ui_font_cn14.h")

text = open(HDR, encoding="utf-8").read()
body = re.search(r"ui_f14_bits\[\] = \{(.*?)\n\};", text, re.S).group(1)
bits = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body))

glyphs = {}
for m in re.finditer(r"\{0x([0-9A-F]{4}),(\d+),(\d+),(\d+),(-?\d+),(-?\d+),(\d+)\}", text):
    uni = int(m.group(1), 16)
    glyphs[uni] = dict(
        adv=int(m.group(2)), w=int(m.group(3)), h=int(m.group(4)),
        ox=int(m.group(5)), oy=int(m.group(6)), off=int(m.group(7)))

LINE_H = int(re.search(r"UI_F14_LINE_H\s+(\d+)", text).group(1))

for ch in (sys.argv[1] if len(sys.argv) > 1 else "时基A1"):
    g = glyphs.get(ord(ch))
    print("=== %s  U+%04X ===" % (ch, ord(ch)))
    if g is None:
        print("  <not in font>\n")
        continue
    stride = (g["w"] + 7) // 8
    for row in range(g["h"]):
        line = "".join(
            "#" if bits[g["off"] + row * stride + (c >> 3)] & (0x80 >> (c & 7)) else "."
            for c in range(g["w"]))
        print("  " + line)
    print("  adv=%d box=%dx%d ox=%d oy=%d (line %d px)\n"
          % (g["adv"], g["w"], g["h"], g["ox"], g["oy"], LINE_H))
