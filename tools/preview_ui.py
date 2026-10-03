# -*- coding: utf-8 -*-
"""Software preview of the STM32 TFT UI -> PNG.

Renders a 320x240 frame using the REAL 1bpp font bitmaps
(bsp/ui/ui_font_cn14.h), the REAL layout constants (bsp/ui/bsp_ui.h) and the
REAL label strings (bsp/ui/ui_text.h), so the geometry (overlaps, text
overflow, panel coverage) can be checked without flashing the board.

    python tools/preview_ui.py ui_preview.png

Anything printed under "== geometry ==" is an assertion result; FAIL means the
layout is broken.
"""
import os
import re
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SW = os.path.join(ROOT, "STM32F407_ProjectTemplate")
UI_DIR = os.path.join(SW, "bsp", "ui")
FONT_H = os.path.join(UI_DIR, "ui_font_cn14.h")
UI_H = os.path.join(UI_DIR, "bsp_ui.h")
TEXT_H = os.path.join(UI_DIR, "ui_text.h")
MAIN_C = os.path.join(SW, "app", "main.c")


# ---------------------------------------------------------------- font
def load_font():
    txt = open(FONT_H, encoding="utf-8").read()
    body = re.search(r"ui_f14_bits\[\] = \{(.*?)\n\};", txt, re.S).group(1)
    bits = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    glyphs = {}
    for m in re.finditer(
            r"\{0x([0-9A-F]{4}),(\d+),(\d+),(\d+),(-?\d+),(-?\d+),(\d+)\}", txt):
        glyphs[int(m.group(1), 16)] = dict(
            adv=int(m.group(2)), w=int(m.group(3)), h=int(m.group(4)),
            ox=int(m.group(5)), oy=int(m.group(6)), off=int(m.group(7)))
    return bits, glyphs


BITS, GLYPHS = load_font()


def text_w(s):
    """mirror of ui_text_w()"""
    return sum(GLYPHS[ord(c)]["adv"] if ord(c) in GLYPHS else 7 for c in s)


def draw_text(px, x, y, s, fg, bg):
    """mirror of ui_text() -- returns the pen x after the string"""
    for ch in s:
        g = GLYPHS.get(ord(ch))
        if g is None:
            x += 7
            continue
        if g["w"] and g["h"]:
            stride = (g["w"] + 7) // 8
            gx, gy = x + g["ox"], y + g["oy"]
            for r in range(g["h"]):
                for c in range(g["w"]):
                    on = BITS[g["off"] + r * stride + (c >> 3)] & (0x80 >> (c & 7))
                    if on:
                        px[gx + c, gy + r] = fg
        x += g["adv"]
    return x


# ---------------------------------------------------------------- config
def load_colors():
    c = {}
    for path in (UI_H, MAIN_C):
        raw = open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(
                r"#define\s+(\w+)\s+RGB565\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", raw):
            c[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
    c["UI_BG"] = (0, 0, 0)                 # COLOR_BLACK
    c["UI_TEXT"] = (255, 255, 255)         # COLOR_WHITE
    c["UI_ACCENT"] = c.get("UI_ACCENT", (0, 220, 255))
    return c


C = load_colors()
UIH_TEXT = open(UI_H, encoding="utf-8").read()


_DEFS = {"LCD_W": 320, "LCD_H": 240}
for _src in (UI_H, os.path.join(UI_DIR, "bsp_ui.c")):
    for _m in re.finditer(r"#define\s+([A-Za-z_]\w*)\s+([^\n]*)",
                          open(_src, encoding="utf-8", errors="replace").read()):
        _e = re.sub(r"/\*.*?\*/", "", _m.group(2)).strip()
        if _e:
            _DEFS[_m.group(1)] = _e


def _resolve(name, depth=0):
    if name in ("LCD_W", "LCD_H"):
        return int(_DEFS[name])
    if depth > 12:
        return None
    expr = _DEFS.get(name)
    if expr is None:
        return None
    if re.fullmatch(r"\d+[uUlL]*", expr):
        return int(re.sub(r"[uUlL]+$", "", expr))
    e = re.sub(r"\b(\d+)[uUlL]+\b", r"\1", expr)
    resolved = _resolve_ids(e, depth)
    if resolved is None:
        return None
    try:
        return int(eval(resolved, {"__builtins__": {}}, {}))
    except Exception:
        return None


def _resolve_ids(expr, depth):
    out = []
    for tok in re.split(r"([A-Za-z_]\w*)", expr):
        if re.fullmatch(r"[A-Za-z_]\w*", tok):
            v = _resolve(tok, depth + 1)
            if v is None:
                return None
            out.append(str(v))
        else:
            out.append(tok)
    return "".join(out)


def K(name):
    v = _resolve(name)
    if v is None:
        raise SystemExit("missing constant " + name)
    return v


W, H = K("UI_W"), K("UI_H")
STATUS_Y, STATUS_H = K("UI_STATUS_Y"), K("UI_STATUS_H")
WAVE_Y, WAVE_H = K("UI_WAVE_Y"), K("UI_WAVE_H")
PLOT_W = K("UI_PLOT_W")
DIV_X, DIV_Y = K("UI_DIV_X"), K("UI_DIV_Y")
NDIV_X, NDIV_Y = K("UI_NDIV_X"), K("UI_NDIV_Y")
TRIG_X, TRIG_W = K("UI_TRIG_X"), K("UI_TRIG_W")
BOT_Y, BOT_H = K("UI_BOTTOM_Y"), K("UI_BOTTOM_H")

CARD_H, CARD_GAP = K("UI_CARD_H"), K("UI_CARD_GAP")
CARD_X = K("UI_CARD_X")
CARD_W = W - 4
CARD_BAR_W, CARD_TXT_X = K("UI_CARD_BAR_W"), K("UI_CARD_TXT_X")


def load_labels():
    lab = {}
    for m in re.finditer(r'#define\s+(S_\w+)\s+"((?:[^"\\]|\\.)*)"',
                         open(TEXT_H, encoding="utf-8").read()):
        b = m.group(2).encode("latin-1").decode("unicode_escape").encode("latin-1")
        lab[m.group(1)] = b.decode("utf-8")
    return lab


L = load_labels()


# ---------------------------------------------------------------- frame
img = Image.new("RGB", (W, H), C["UI_BG"])
px = img.load()

# --- status bar
px_rect = lambda x, y, w, h, col: [px.__setitem__((xx, yy), col)
                                   for yy in range(y, y + h) for xx in range(x, x + w)]
px_rect(0, STATUS_Y, W, STATUS_H, C["UI_BAR_BG"])
px_rect(0, STATUS_H - 1, W, 1, C["UI_RUN_COLOR"])

# --- canvas
px_rect(0, WAVE_Y, W, WAVE_H, C["UI_BG"])
for i in range(NDIV_X + 1):
    x = min(i * DIV_X, PLOT_W - 1)
    col = C["UI_GRID_MAJOR"] if i == NDIV_X // 2 else C["UI_GRID_MINOR"]
    px_rect(x, WAVE_Y, 1, WAVE_H, col)
for i in range(NDIV_Y + 1):
    y = min(WAVE_Y + i * DIV_Y, WAVE_Y + WAVE_H - 1)
    col = C["UI_GRID_MAJOR"] if i == NDIV_Y // 2 else C["UI_GRID_MINOR"]
    px_rect(0, y, PLOT_W, 1, col)

# --- trigger strip
px_rect(TRIG_X, WAVE_Y, TRIG_W, WAVE_H, C["UI_STRIP_BG"])
px_rect(TRIG_X - 1, WAVE_Y, 1, WAVE_H, C["UI_FRAME"])

TRIG_RAW = 2560
trig_y = WAVE_Y + WAVE_H // 2 - int((TRIG_RAW - 2048) * (WAVE_H // 2) / 2048)

# dashed level line (4 on / 8 pitch)
for x in range(0, PLOT_W, 8):
    px_rect(x, trig_y, min(4, PLOT_W - x), 1, C["UI_TRIG_LINE"])

# left pointing triangle + stub
for i in range(6):
    x = TRIG_X + 1 + i
    h2 = 1 + i
    t = max(trig_y - h2, WAVE_Y)
    b = min(trig_y + h2, WAVE_Y + WAVE_H - 1)
    px_rect(x, t, 1, b - t + 1, C["UI_TRIG_COLOR"])
px_rect(TRIG_X + 7, trig_y, TRIG_W - 8, 1, C["UI_TRIG_COLOR"])
px_rect(TRIG_X, trig_y, 1, 1, C["UI_TRIG_COLOR"])

# --- waveform (mirror of ui_map_raw with zoom 0)
import math
for ch, col in ((0, C["UI_CH1_COLOR"]), (1, C["UI_CH2_COLOR"])):
    prev = None
    for x in range(PLOT_W):
        if ch == 0:
            v = 2048 + int(1500 * math.sin(x / 300.0 * 4 * math.pi))
        else:
            v = 2048 + int(1000 * math.sin(x / 300.0 * 4 * math.pi + 1.0))
        v = max(0, min(4095, v))
        y = WAVE_Y + WAVE_H // 2 - int((v - 2048) * (WAVE_H // 2) / 2048)
        if prev is not None:
            y0, y1 = min(prev, y), max(prev, y)
            px_rect(x, y0, 1, y1 - y0 + 1, col)
        prev = y

# --- measurement cards
tb = "3.00ms"
rate = "10.0kHz"
status_l = L["S_TIMEBASE"] + " " + tb
status_m = L["S_RUN"]
status_r = rate
draw_text(px, 4, STATUS_Y + 3, status_l, C["UI_TEXT"], C["UI_BAR_BG"])
mw = text_w(status_m)
draw_text(px, (W - mw) // 2, STATUS_Y + 3, status_m, C["UI_RUN_COLOR"], C["UI_BAR_BG"])
rw = text_w(status_r)
draw_text(px, W - 4 - rw, STATUS_Y + 3, status_r, C["UI_ACCENT"], C["UI_BAR_BG"])

lines = [
    "%s %s 2000mV %s 1000mV" % (L["S_CH1"], L["S_VPP"], L["S_VAVG"]),
    "  %s 1.00kHz %s 50.0%%" % (L["S_FREQ"], L["S_DUTY"]),
    "%s %s 1234mV %s 620mV" % (L["S_CH2"], L["S_VPP"], L["S_VAVG"]),
    "  %s 2.00kHz %s 25.0%%" % (L["S_FREQ"], L["S_DUTY"]),
]
colors = [C["UI_CH1_COLOR"], C["COL_MEAS1"], C["UI_CH2_COLOR"], C["COL_MEAS2"]]
bgs = [C["UI_CARD1_BG"], C["UI_CARD1_BG"], C["UI_CARD2_BG"], C["UI_CARD2_BG"]]

for card in range(2):
    cy = BOT_Y + card * (CARD_H + CARD_GAP)
    px_rect(CARD_X, cy, CARD_W, CARD_H, bgs[card * 2])
    px_rect(CARD_X, cy, CARD_BAR_W, CARD_H, colors[card * 2])

for i, s in enumerate(lines):
    card = i >> 1
    cy = BOT_Y + card * (CARD_H + CARD_GAP)
    draw_text(px, CARD_TXT_X, cy + 3 + (i & 1) * 15, s, colors[i], bgs[i])

# ---------------------------------------------------------------- checks
fails = []
print("== geometry ==")
for i, s in enumerate(lines):
    card = i >> 1
    x0 = CARD_TXT_X
    limit = CARD_X + CARD_W
    w = text_w(s)
    ok = x0 + w <= limit
    print("  row %d card%d  x=%d w=%d right=%d limit=%d  %s"
          % (i, card, x0, w, x0 + w, limit, "ok" if ok else "FAIL overflow"))
    if not ok:
        fails.append("row %d overflows by %d px" % (i, x0 + w - limit))

c2y = BOT_Y + CARD_H + CARD_GAP
ok = c2y + CARD_H <= BOT_Y + BOT_H
print("  cards bottom=%d panel bottom=%d  %s"
      % (c2y + CARD_H, BOT_Y + BOT_H, "ok" if ok else "FAIL"))
if not ok:
    fails.append("card 2 outside the bottom panel")

lw, mw, rw = text_w(status_l), text_w(status_m), text_w(status_r)
print("  status l=%d mid=%d(mx=%d) r=%d(rx=%d)" % (lw, mw, (W - mw) // 2, rw, W - 4 - rw))
if 4 + lw > (W - mw) // 2 or (W - mw) // 2 + mw > W - 4 - rw:
    fails.append("status fields overlap")
    print("  status fields OVERLAP -- FAIL")

print("  layer coverage: status %d..%d  plot %d..%d  cards %d..%d"
      % (STATUS_Y, STATUS_Y + STATUS_H, WAVE_Y, WAVE_Y + WAVE_H, BOT_Y, BOT_Y + BOT_H))

print("== result: %s ==" % ("OK" if not fails else "; ".join(fails)))

# ---------------------------------------------------------------- save
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "ui_preview.png")
scale = 3
img.resize((W * scale, H * scale), Image.NEAREST).save(out)
print("wrote %s (%dx%d, %dx)" % (out, W, H, scale))
