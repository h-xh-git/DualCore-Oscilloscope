/*
 * bsp_ui.h - Oscilloscope screen layer for the 2.4" ST7789 TFT
 *
 * LANDSCAPE 320 x 240 layout (matches bsp_lcd MADCTL = 0x60):
 *
 *     x   0 .. 299   plot area   : 10 horizontal divisions of 30 px
 *     x 300 .. 319   trig strip  : trigger level indicator
 *
 *     y   0 ..  19   status bar  : time-base | RUN/STOP | sample rate
 *     y  20 .. 169   wave canvas : 10 x 6 graticule (30 x 25 px per div)
 *     y 170 .. 239   readout     : two channel cards (2 x 15 px lines each)
 *
 * Text uses ui_font_cn14.h: a 14 px 1bpp font with printable ASCII plus a
 * CJK subset, generated from font_chinese_14.h by tools/gen_ui_font.py.
 * Strings are UTF-8 (see the S_* macros in bsp/ui/ui_text.h).
 *
 * Trigger level strip + four-line measurement readout.
 */
#ifndef __BSP_UI_H
#define __BSP_UI_H

#include "bsp_lcd.h"

/* this layout is hard-coded for a 320x240 landscape panel */
#if (LCD_W != 320) || (LCD_H != 240)
#error "bsp_ui layout expects a 320x240 landscape panel"
#endif

/* ===================== layout ===================== */
#define UI_W            LCD_W          /* 320 */
#define UI_H            LCD_H          /* 240 */

#define UI_STATUS_Y     0
#define UI_STATUS_H     20

#define UI_PLOT_W       300            /* plot width, 10 x 30 px        */
#define UI_TRIG_X       UI_PLOT_W      /* right hand trigger strip      */
#define UI_TRIG_W       (UI_W - UI_TRIG_X)   /* 20 px                   */

#define UI_WAVE_Y       20
#define UI_WAVE_H       150            /* 20 .. 169, exactly 6 x 25     */

#define UI_DIV_X        30             /* 10 horizontal divisions       */
#define UI_DIV_Y        25             /*  6 vertical divisions         */
#define UI_NDIV_X       10
#define UI_NDIV_Y       6

#define UI_BOTTOM_Y     170
#define UI_BOTTOM_H     70             /* four 15 px lines              */
#define UI_BOTTOM_LINES 4

/* ===================== colors ===================== */
#define UI_BG           COLOR_BLACK
#define UI_GRID_MINOR   RGB565(  0,  56,   0)   /* dark green   */
#define UI_GRID_MAJOR   RGB565(  0, 110,   0)   /* centre cross */
#define UI_TEXT         COLOR_WHITE
#define UI_ACCENT       RGB565(  0, 220, 255)   /* cyan         */
#define UI_CH1_COLOR    RGB565(  0, 255,   0)   /* green        */
#define UI_CH2_COLOR    RGB565(255, 220,   0)   /* amber        */
#define UI_OFF_COLOR    RGB565( 90,  90,  90)   /* channel hidden */

#define UI_BAR_BG       RGB565(  8,  24,  40)
#define UI_RUN_COLOR    RGB565(  0, 230,  90)
#define UI_STOP_COLOR   RGB565(255, 170,  40)

#define UI_STRIP_BG     RGB565( 14,  14,  22)   /* trigger strip  */
#define UI_TRIG_COLOR   RGB565(255,  80,  70)   /* trigger hit    */
#define UI_TRIG_ARM     RGB565(130, 130, 130)   /* armed, no hit  */

#define UI_TRIG_LINE    RGB565(170,  45,  45)   /* dashed level line on the plot */
#define UI_FRAME        RGB565( 62,  66,  78)   /* plot / strip divider          */

/* readout cards (the two measurement blocks under the waveform) */
#define UI_CARD1_BG     RGB565(  0,  22,   0)   /* dim green, CH1 */
#define UI_CARD2_BG     RGB565( 24,  18,   0)   /* dim amber, CH2 */
#define UI_CARD_OFF_BG  RGB565( 16,  16,  18)   /* channel hidden */

#define UI_MENU_BG      RGB565( 10,  20,  50)
#define UI_MENU_SEL     RGB565(  0, 200, 255)
#define UI_MENU_SEL_ED  RGB565(255, 200,   0)

/* ===================== text ===================== */
/* draw a UTF-8 string. y is the TOP of the 15 px line box.
   Every pixel of the glyph box is written (fg or bg), so text is opaque. */
void     ui_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg);

/* advance width of a UTF-8 string in pixels */
uint16_t ui_text_w(const char *s);

/* ===================== canvas ===================== */
/* background color of one canvas pixel (grid + strip aware) */
uint16_t ui_bg_color(uint16_t x, uint16_t y);

/* fill the whole screen, draw the graticule, clears both traces */
void ui_init(void);

/* repaint ONLY the wave area background + graticule + trigger strip
   (both traces must be reset afterwards) */
void ui_canvas_init(void);

/* ===================== traces ===================== */
typedef struct
{
    uint16_t lo[UI_PLOT_W]; /* vertical segment drawn last frame (top)    */
    uint16_t hi[UI_PLOT_W]; /* vertical segment drawn last frame (bottom) */
    uint16_t color;
    uint8_t  valid;         /* 0 = nothing on screen yet                  */
} ui_trace_t;

/* forget what is on screen for this trace (after a canvas repaint) */
void ui_trace_reset(ui_trace_t *t, uint16_t color);

/* draw `n` raw 0..4095 samples stretched over the plot width.
   zoom_shift: 0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x vertical magnification. */
void ui_trace_draw(ui_trace_t *t, const uint16_t *raw, uint16_t n, uint8_t zoom_shift);

/* ===================== trigger strip ===================== */
/* Draw the trigger level: a dashed line across the plot plus the 20 px marker
   strip on the right.  The line is part of the canvas background, so it is
   redrawn here only when the level (or the armed/hit colour) really changed;
   ui_canvas_init() repaints it after a full canvas rebuild.
   hit != 0 => a trigger was found for the frame just drawn. */
void ui_trigger_strip(uint16_t level_raw, uint8_t enabled, uint8_t hit);

/* ===================== bars ===================== */
/* three fields + run colour for the middle one */
void ui_status_draw(const char *left, const char *mid, const char *right, uint8_t running);

/* up to UI_BOTTOM_LINES readout lines, each with its own foreground colour.
   Lines 0/1 share the CH1 card and 2/3 the CH2 card; the card background and
   its left colour bar come from colors[0] / colors[2], so passing UI_OFF_COLOR
   greys the whole card out when that channel is hidden. */
void ui_bottom_draw(const char *const *lines, const uint16_t *colors, uint8_t n);

/* Both bars remember what they drew and only repaint the fields that changed.
   Call this when something else has painted over them (e.g. the modal menu
   was just closed) so the next draw rebuilds them completely. */
void ui_bars_invalidate(void);

/* ===================== menu ===================== */
#define UI_MENU_VIS     6              /* visible rows, scrolls if n > this */

/* draws a modal panel; `first` is the index of the top visible row and
   editing != 0 highlights the active row differently */
void ui_menu_draw(const char *title, const char *const *items, const char *const *vals,
                  uint8_t n, uint8_t first, int8_t sel, uint8_t editing);

#endif /* __BSP_UI_H */
