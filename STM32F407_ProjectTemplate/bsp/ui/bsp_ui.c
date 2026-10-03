/*
 * bsp_ui.c - oscilloscope screen layer for the 2.4" ST7789 (landscape 320x240)
 *
 * Everything here is plain blocking SPI drawing through bsp_lcd; there is no
 * off-screen framebuffer because the panel is refreshed by erasing only the
 * pixels that changed (see ui_trace_draw).
 *
 * The trigger strip on the right is never touched by the traces, so it can be
 * repainted on its own whenever the level changes.
 */
#include "bsp_ui.h"
#include "ui_font_cn14.h"

#include <string.h>

/* ==================================================================
 * text
 * ================================================================== */
#define UI_GLYPH_MAX    16

static uint16_t s_glyph[UI_GLYPH_MAX * UI_GLYPH_MAX];

/* The modal menu floats on top of the canvas, so rebuilding the canvas also
   destroys it.  Set by ui_menu_draw(), cleared by ui_canvas_init(). */
static uint8_t  s_menu_drawn;

/* Trigger level state.
   s_trig_y is the y of the dashed level line that belongs to the canvas
   background (0xFFFF = no line drawn).  ui_bg_vline() deliberately never
   paints over that row, so erasing a trace brings the line back instead of
   chewing a hole in it.  s_strip_* cache the right hand marker so the 20x150
   strip is only repainted when the marker really moved. */
static uint16_t s_trig_y  = 0xFFFFu;
static uint16_t s_strip_y = 0xFFFFu;
static uint16_t s_strip_col;
static uint8_t  s_strip_on;

/* ui_f14_glyphs[] is sorted by unicode -> binary search */
static const ui_f14_glyph_t *ui_find(uint16_t uni)
{
    int lo = 0;
    int hi = (int)UI_F14_N - 1;

    while (lo <= hi)
    {
        int mid = (lo + hi) >> 1;
        uint16_t u = ui_f14_glyphs[mid].uni;

        if (u == uni) return &ui_f14_glyphs[mid];
        if (u < uni)  lo = mid + 1;
        else          hi = mid - 1;
    }
    return 0;
}

/* decode one UTF-8 codepoint and advance *ps (4-byte sequences -> '?') */
static uint16_t ui_utf8(const char **ps)
{
    const unsigned char *p = (const unsigned char *)(*ps);
    uint16_t cp;

    if (p[0] < 0x80u)
    {
        *ps = (const char *)(p + 1);
        return (uint16_t)p[0];
    }
    if (((p[0] & 0xE0u) == 0xC0u) && ((p[1] & 0xC0u) == 0x80u))
    {
        cp = (uint16_t)(((uint16_t)(p[0] & 0x1Fu) << 6) | (uint16_t)(p[1] & 0x3Fu));
        *ps = (const char *)(p + 2);
        return cp;
    }
    if (((p[0] & 0xF0u) == 0xE0u) &&
        ((p[1] & 0xC0u) == 0x80u) && ((p[2] & 0xC0u) == 0x80u))
    {
        cp = (uint16_t)(((uint16_t)(p[0] & 0x0Fu) << 12) |
                        ((uint16_t)(p[1] & 0x3Fu) << 6)  |
                         (uint16_t)(p[2] & 0x3Fu));
        *ps = (const char *)(p + 3);
        return cp;
    }
    *ps = (const char *)(p + 1);
    return (uint16_t)'?';
}

void ui_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{
    const char *p = s;

    while (*p != '\0')
    {
        const ui_f14_glyph_t *g = ui_find(ui_utf8(&p));
        int16_t gx, gy;

        if (g == 0)                     /* not in the generated font */
        {
            x = (uint16_t)(x + 7u);
            continue;
        }
        if (((uint8_t)g->w > UI_GLYPH_MAX) || ((uint8_t)g->h > UI_GLYPH_MAX))
        {
            x = (uint16_t)(x + g->adv);
            continue;
        }
        if ((g->w != 0u) && (g->h != 0u))
        {
            uint8_t stride = (uint8_t)(((uint8_t)g->w + 7u) >> 3);
            uint8_t r, c;
            uint8_t r0  = 0u;
            uint8_t rows = (uint8_t)g->h;
            uint8_t cols = (uint8_t)g->w;

            gx = (int16_t)((int16_t)x + (int16_t)g->ox);
            gy = (int16_t)((int16_t)y + (int16_t)g->oy);
            if (gx < 0) { x = (uint16_t)(x + g->adv); continue; }

            if (gy < 0)                       /* glyph sticks out above the panel */
            {
                r0 = (uint8_t)(-gy);
                if (r0 >= (uint8_t)g->h) { x = (uint16_t)(x + g->adv); continue; }
                rows = (uint8_t)((uint8_t)g->h - r0);
                gy   = 0;
            }
            if ((int16_t)(gy + rows) > (int16_t)LCD_H)
                rows = (uint8_t)((int16_t)LCD_H - gy);
            if ((int16_t)(gx + cols) > (int16_t)LCD_W)
                cols = (uint8_t)((int16_t)LCD_W - gx);
            if ((rows == 0u) || (cols == 0u)) { x = (uint16_t)(x + g->adv); continue; }

            for (r = 0u; r < rows; r++)
            {
                uint16_t src = (uint16_t)((uint16_t)r0 + r);

                for (c = 0u; c < cols; c++)
                {
                    uint8_t on = (uint8_t)(ui_f14_bits[(uint16_t)g->off + src * stride +
                                                       (uint16_t)(c >> 3)] &
                                           (uint8_t)(0x80u >> (c & 7u)));
                    s_glyph[(uint16_t)r * (uint8_t)g->w + c] = on ? fg : bg;
                }
            }
            lcd_blit((uint16_t)gx, (uint16_t)gy, cols, rows, s_glyph);
        }
        x = (uint16_t)(x + g->adv);
    }
}

uint16_t ui_text_w(const char *s)
{
    const char *p = s;
    uint16_t w = 0u;

    while (*p != '\0')
    {
        const ui_f14_glyph_t *g = ui_find(ui_utf8(&p));
        w = (uint16_t)(w + (g ? g->adv : 7u));
    }
    return w;
}

/* ==================================================================
 * canvas
 * ================================================================== */
uint16_t ui_bg_color(uint16_t x, uint16_t y)
{
    if (y < UI_WAVE_Y) return UI_BG;
    if (y >= (uint16_t)(UI_WAVE_Y + UI_WAVE_H)) return UI_BG;
    if (x >= UI_PLOT_W) return UI_STRIP_BG;

    if (((y - UI_WAVE_Y) % UI_DIV_Y) == 0u)
        return (((y - UI_WAVE_Y) / UI_DIV_Y) == (UI_NDIV_Y / 2u)) ? UI_GRID_MAJOR : UI_GRID_MINOR;

    if ((x % UI_DIV_X) == 0u)
        return ((x / UI_DIV_X) == (UI_NDIV_X / 2u)) ? UI_GRID_MAJOR : UI_GRID_MINOR;

    return UI_BG;
}

/* erase a vertical run: grid pixels stay grid, gaps go back to UI_BG.
   Splits the run at every horizontal grid row so each piece is one
   lcd_fill_rect instead of h single-pixel writes. */
static void ui_bg_vline(uint16_t x, uint16_t y0, uint16_t h)
{
    uint16_t y    = y0;
    uint16_t yend = (uint16_t)(y0 + h);

    while (y < yend)
    {
        uint16_t gy   = (uint16_t)(y - UI_WAVE_Y);
        uint16_t rem  = (uint16_t)(gy % UI_DIV_Y);
        uint16_t run  = (uint16_t)(UI_DIV_Y - rem);
        uint16_t left = (uint16_t)(yend - y);

        if (run > left) run = left;

        if (y == s_trig_y)
        {
            /* the dashed trigger level line is part of the background, so the
               old trace must not be erased off it -- but it must not be left
               behind either, because the trace was drawn over the line last
               frame.  Restore the line's own pixel for this column. */
            lcd_draw_pixel(x, y, (((x & 7u) < 4u) ? UI_TRIG_LINE
                                                 : ui_bg_color(x, y)));
            y++;
            continue;
        }
        if (rem == 0)                       /* this pixel IS a grid row */
        {
            lcd_draw_pixel(x, y, ui_bg_color(x, y));
            y++;
            if (run) run--;
        }
        if (run)
        {
            /* stop the fill just below the trigger line */
            if ((s_trig_y > y) && (s_trig_y < (uint16_t)(y + run)))
                run = (uint16_t)(s_trig_y - y);

            if (run)
            {
                lcd_fill_rect(x, y, 1, run, ui_bg_color(x, y));
                y = (uint16_t)(y + run);
            }
        }
    }
}

/* ==================================================================
 * trigger level line (part of the canvas background)
 * ================================================================== */
/* colour of horizontal grid row `y`, or UI_BG when `y` is not a grid row */
static uint16_t ui_hgrid_color(uint16_t y)
{
    uint16_t d;

    if ((y < UI_WAVE_Y) || (y >= (uint16_t)(UI_WAVE_Y + UI_WAVE_H))) return UI_BG;
    if (((y - UI_WAVE_Y) % UI_DIV_Y) != 0u) return UI_BG;

    d = (uint16_t)((y - UI_WAVE_Y) / UI_DIV_Y);
    return (d == (uint16_t)(UI_NDIV_Y / 2u)) ? UI_GRID_MAJOR : UI_GRID_MINOR;
}

/* put row `y` back to plain graticule (used when the level moved away) */
static void ui_trig_row_erase(uint16_t y)
{
    uint16_t g = ui_hgrid_color(y);
    uint16_t i, x;

    if (g != UI_BG)                     /* whole row is a grid line */
    {
        lcd_draw_hline(0u, y, UI_PLOT_W, g);
        return;
    }

    lcd_draw_hline(0u, y, UI_PLOT_W, UI_BG);

    for (i = 0u; i <= UI_NDIV_X; i++)   /* restore the vertical grid pixels */
    {
        x = (uint16_t)(i * UI_DIV_X);
        if (x >= UI_PLOT_W) x = (uint16_t)(UI_PLOT_W - 1u);
        lcd_draw_pixel(x, y, (i == (UI_NDIV_X / 2)) ? UI_GRID_MAJOR : UI_GRID_MINOR);
    }
}

/* 4 px on / 4 px off dashed level line */
static void ui_trig_row_draw(uint16_t y)
{
    uint16_t x;

    for (x = 0u; x < UI_PLOT_W; x = (uint16_t)(x + 8u))
    {
        uint16_t w = (uint16_t)(UI_PLOT_W - x);
        if (w > 4u) w = 4u;
        lcd_draw_hline(x, y, w, UI_TRIG_LINE);
    }
}

void ui_canvas_init(void)
{
    uint16_t i;

    lcd_fill_rect(0, UI_WAVE_Y, UI_W, UI_WAVE_H, UI_BG);

    for (i = 0; i <= UI_NDIV_X; i++)
    {
        uint16_t x = (uint16_t)(i * UI_DIV_X);
        if (x >= UI_PLOT_W) x = (uint16_t)(UI_PLOT_W - 1u);
        lcd_draw_vline(x, UI_WAVE_Y, UI_WAVE_H,
                       (i == (UI_NDIV_X / 2)) ? UI_GRID_MAJOR : UI_GRID_MINOR);
    }
    for (i = 0; i <= UI_NDIV_Y; i++)
    {
        uint16_t y = (uint16_t)(UI_WAVE_Y + i * UI_DIV_Y);
        if (y >= (uint16_t)(UI_WAVE_Y + UI_WAVE_H)) y = (uint16_t)(UI_WAVE_Y + UI_WAVE_H - 1);
        lcd_draw_hline(0, y, UI_PLOT_W,
                       (i == (UI_NDIV_Y / 2)) ? UI_GRID_MAJOR : UI_GRID_MINOR);
    }

    /* trigger strip + neutral divider between plot and strip */
    lcd_fill_rect(UI_TRIG_X, UI_WAVE_Y, UI_TRIG_W, UI_WAVE_H, UI_STRIP_BG);
    lcd_draw_vline((uint16_t)(UI_TRIG_X - 1u), UI_WAVE_Y, UI_WAVE_H, UI_FRAME);

    /* the level line belongs to the canvas background, so it has to be put
       back after the wipe above */
    if (s_trig_y != 0xFFFFu) ui_trig_row_draw(s_trig_y);

    /* the canvas overdraws the modal menu and the trigger strip marker */
    s_menu_drawn = 0u;
    s_strip_y    = 0xFFFFu;
}

void ui_init(void)
{
    lcd_fill_screen(UI_BG);
    ui_canvas_init();
    lcd_fill_rect(0, UI_STATUS_Y, UI_W, UI_STATUS_H, UI_BAR_BG);
    lcd_fill_rect(0, UI_BOTTOM_Y, UI_W, UI_BOTTOM_H, UI_BG);
}

/* ==================================================================
 * traces
 * ================================================================== */
void ui_trace_reset(ui_trace_t *t, uint16_t color)
{
    t->color = color;
    t->valid = 0;
}

/* raw 0..4095 -> canvas y, magnified about the vertical centre */
static uint16_t ui_map_raw(uint16_t raw, uint8_t zoom_shift)
{
    int32_t v    = (int32_t)raw - 2048;              /* -2048 .. +2047 */
    int32_t half = (int32_t)(UI_WAVE_H / 2);         /* 75 px          */
    int32_t d, y;

    if (zoom_shift)
    {
        if (v < 0) v = -(int32_t)((-v) >> zoom_shift);
        else       v = (int32_t)(v >> zoom_shift);
    }

    d = (v * half) / 2048;
    y = (int32_t)(UI_WAVE_Y + UI_WAVE_H / 2) - d;

    if (y < (int32_t)UI_WAVE_Y) y = (int32_t)UI_WAVE_Y;
    if (y > (int32_t)(UI_WAVE_Y + UI_WAVE_H - 1)) y = (int32_t)(UI_WAVE_Y + UI_WAVE_H - 1);
    return (uint16_t)y;
}

void ui_trace_draw(ui_trace_t *t, const uint16_t *raw, uint16_t n, uint8_t zoom_shift)
{
    uint16_t x, prev;

    if (n == 0) return;
    prev = ui_map_raw(raw[0], zoom_shift);

    for (x = 0; x < UI_PLOT_W; x++)
    {
        uint32_t idx = ((uint32_t)x * (uint32_t)n) / UI_PLOT_W;
        uint16_t y, lo, hi;

        if (idx >= (uint32_t)n) idx = (uint32_t)n - 1u;
        y = ui_map_raw(raw[idx], zoom_shift);

        /* connect to the previous column -> no gaps on fast edges */
        lo = (prev < y) ? prev : y;
        hi = (prev > y) ? prev : y;

        if (t->valid)
        {
            if (t->lo[x] < lo) ui_bg_vline(x, t->lo[x], (uint16_t)(lo - t->lo[x]));
            if (t->hi[x] > hi) ui_bg_vline(x, (uint16_t)(hi + 1), (uint16_t)(t->hi[x] - hi));
        }

        lcd_draw_vline(x, lo, (uint16_t)(hi - lo + 1), t->color);

        t->lo[x] = lo;
        t->hi[x] = hi;
        prev = y;
    }
    t->valid = 1;
}

/* ==================================================================
 * trigger strip
 * ================================================================== */
void ui_trigger_strip(uint16_t level_raw, uint8_t enabled, uint8_t hit)
{
    uint16_t y  = 0xFFFFu;
    uint16_t cl = UI_TRIG_ARM;
    uint8_t  i;

    if (enabled != 0u)
    {
        y  = ui_map_raw(level_raw, 0u);
        cl = hit ? UI_TRIG_COLOR : UI_TRIG_ARM;
    }

    /* ---- dashed level line across the plot (canvas background) ----
       Only touched when the level really moved: redrawing it every frame
       would stamp a 300 px line on top of the traces 25 times a second. */
    if ((enabled != 0u) ? (s_trig_y != y) : (s_trig_y != 0xFFFFu))
    {
        if (s_trig_y != 0xFFFFu) ui_trig_row_erase(s_trig_y);
        s_trig_y = (enabled != 0u) ? y : 0xFFFFu;
        if (s_trig_y != 0xFFFFu) ui_trig_row_draw(s_trig_y);
    }

    /* ---- right hand marker strip ----
       This used to be repainted unconditionally, i.e. 20x150 px of SPI on
       every frame; now only when the marker itself changed. */
    if ((s_strip_on == enabled) && (s_strip_y == y) && (s_strip_col == cl))
        return;

    s_strip_on  = enabled;
    s_strip_y   = y;
    s_strip_col = cl;

    lcd_fill_rect(UI_TRIG_X, UI_WAVE_Y, UI_TRIG_W, UI_WAVE_H, UI_STRIP_BG);
    lcd_draw_vline((uint16_t)(UI_TRIG_X - 1u), UI_WAVE_Y, UI_WAVE_H, UI_FRAME);

    if (enabled == 0u) return;

    /* filled left pointing triangle, apex on the divider, then a stub line */
    for (i = 0u; i < 6u; i++)
    {
        uint16_t x  = (uint16_t)(UI_TRIG_X + 1u + i);
        uint16_t h2 = (uint16_t)(1u + i);              /* half height 1..6 */
        uint16_t t  = (y >= h2) ? (uint16_t)(y - h2) : UI_WAVE_Y;
        uint16_t b  = (uint16_t)(y + h2);

        if (b >= (uint16_t)(UI_WAVE_Y + UI_WAVE_H))
            b = (uint16_t)(UI_WAVE_Y + UI_WAVE_H - 1u);

        lcd_draw_vline(x, t, (uint16_t)(b - t + 1u), cl);
    }

    lcd_draw_hline((uint16_t)(UI_TRIG_X + 7u), y, (uint16_t)(UI_TRIG_W - 8u), cl);
    lcd_draw_hline(UI_TRIG_X, y, 1u, cl);
}

/* ==================================================================
 * bars
 * ================================================================== */
/* ---- status bar ----
   Each of the three fields (left / centre / right) is repainted on its own
   only when its text really changed, so a RUN/STOP or a time-base change no
   longer rewrites the whole 320x20 bar.  The old rectangle is erased first
   (the right-hand field is right aligned and can move left). */
static char     s_stat_l[40];
static char     s_stat_m[24];
static char     s_stat_r[24];
static uint16_t s_stat_lw;
static uint16_t s_stat_mw, s_stat_mx;
static uint16_t s_stat_rw, s_stat_rx;
static uint8_t  s_stat_run;
static uint8_t  s_stat_first = 1u;

static void ui_status_erase(uint16_t xa, uint16_t wa, uint16_t xb, uint16_t wb,
                            uint16_t y)
{
    uint16_t x0 = (xa < xb) ? xa : xb;
    uint16_t e1 = (uint16_t)(xa + wa);
    uint16_t e2 = (uint16_t)(xb + wb);
    uint16_t e  = (e1 > e2) ? e1 : e2;

    if (e <= x0) return;
    lcd_fill_rect(x0, y, (uint16_t)(e - x0), 15u, UI_BAR_BG);
}

void ui_status_draw(const char *left, const char *mid, const char *right, uint8_t running)
{
    uint16_t y = (uint16_t)(UI_STATUS_Y + 3u);
    uint16_t lw, mw, rw, mx, rx;

    lw = ui_text_w(left);
    mw = ui_text_w(mid);
    rw = ui_text_w(right);
    mx = (uint16_t)((UI_W - mw) / 2u);
    rx = (uint16_t)(UI_W - 4u - rw);

    if (s_stat_first != 0u)
    {
        s_stat_first = 0u;

        lcd_fill_rect(0, UI_STATUS_Y, UI_W, UI_STATUS_H, UI_BAR_BG);
        lcd_draw_hline(0, (uint16_t)(UI_STATUS_H - 1u), UI_W,
                       running ? UI_RUN_COLOR : UI_STOP_COLOR);

        ui_text(4u, y, left,  UI_TEXT,  UI_BAR_BG);
        ui_text(mx,  y, mid,   running ? UI_RUN_COLOR : UI_STOP_COLOR, UI_BAR_BG);
        ui_text(rx,  y, right, UI_ACCENT, UI_BAR_BG);
    }
    else
    {
        /* the line under the bar doubles as a RUN/STOP indicator, so recolour
           it whenever the run state flips */
        if (s_stat_run != running)
        {
            lcd_draw_hline(0, (uint16_t)(UI_STATUS_H - 1u), UI_W,
                           running ? UI_RUN_COLOR : UI_STOP_COLOR);
        }

        if ((strcmp(s_stat_l, left) != 0) || (lw != s_stat_lw))
        {
            ui_status_erase(4u, lw, 4u, s_stat_lw, y);
            ui_text(4u, y, left, UI_TEXT, UI_BAR_BG);
        }

        if ((strcmp(s_stat_m, mid) != 0) || (s_stat_run != running))
        {
            ui_status_erase(mx, mw, s_stat_mx, s_stat_mw, y);
            ui_text(mx, y, mid, running ? UI_RUN_COLOR : UI_STOP_COLOR, UI_BAR_BG);
        }

        if ((strcmp(s_stat_r, right) != 0) || (rw != s_stat_rw))
        {
            ui_status_erase(rx, rw, s_stat_rx, s_stat_rw, y);
            ui_text(rx, y, right, UI_ACCENT, UI_BAR_BG);
        }
    }

    strcpy(s_stat_l, left);
    strcpy(s_stat_m, mid);
    strcpy(s_stat_r, right);
    s_stat_lw = lw;
    s_stat_mw = mw;
    s_stat_mx = mx;
    s_stat_rw = rw;
    s_stat_rx = rx;
    s_stat_run = running;
}

/* ---- measurement panel ----
   Two channel cards instead of four bare text lines: every card owns a
   background, a 3 px colour bar on its left edge and two 15 px text rows.
   The card frame is repainted only when that channel's colour changed (i.e.
   the channel was switched on/off) and each row only when its text really
   changed.  A full 320x70 repaint costs ~45 KB of SPI, which is what made
   the panel flicker at 7 Hz. */
#define UI_CARD_H       34u
#define UI_CARD_GAP      2u
#define UI_CARD_X        2u
#define UI_CARD_W       (UI_W - 4u)
#define UI_CARD_BAR_W    3u
#define UI_CARD_TXT_X   10u

static char     s_bot[UI_BOTTOM_LINES][48];
static uint16_t s_bot_w[UI_BOTTOM_LINES];
static uint16_t s_bot_c[UI_BOTTOM_LINES];
static uint8_t  s_bot_ok[UI_BOTTOM_LINES];
static uint16_t s_card_bar[2];
static uint8_t  s_bot_first = 1u;

static uint16_t ui_card_y(uint8_t card)
{
    return (uint16_t)(UI_BOTTOM_Y + (uint16_t)card * (UI_CARD_H + UI_CARD_GAP));
}

/* a hidden channel gets a neutral card instead of a coloured one */
static uint16_t ui_card_bg(uint8_t card, uint16_t bar)
{
    if (bar == UI_OFF_COLOR) return UI_CARD_OFF_BG;
    return (card == 0u) ? UI_CARD1_BG : UI_CARD2_BG;
}

void ui_bottom_draw(const char *const *lines, const uint16_t *colors, uint8_t n)
{
    uint8_t  i, card;
    uint16_t w, y, x, cw, bar, bg, cy;

    if (s_bot_first != 0u)
    {
        s_bot_first = 0u;

        lcd_fill_rect(0, UI_BOTTOM_Y, UI_W, UI_BOTTOM_H, UI_BG);
        for (i = 0u; i < UI_BOTTOM_LINES; i++)
        {
            s_bot[i][0] = '\0';
            s_bot_w[i]  = 0u;
            s_bot_c[i]  = 0u;
            s_bot_ok[i] = 0u;
        }
        s_card_bar[0] = 0u;             /* values no colour can take -> repaint */
        s_card_bar[1] = 0u;
    }

    /* card frame + left colour bar; the bar colour is colors[card * 2], i.e.
       the colour main.c gives to that channel's first readout line */
    for (card = 0u; (card < 2u) && (((uint16_t)card * 2u) < (uint16_t)n); card++)
    {
        bar = colors[(uint16_t)card * 2u];
        if (s_card_bar[card] == bar) continue;

        s_card_bar[card] = bar;
        bg = ui_card_bg(card, bar);
        cy = ui_card_y(card);

        lcd_fill_rect(UI_CARD_X, cy, UI_CARD_W, UI_CARD_H, bg);
        lcd_fill_rect(UI_CARD_X, cy, UI_CARD_BAR_W, UI_CARD_H, bar);

        s_bot_ok[(uint16_t)card * 2u]      = 0u;   /* both rows are stale now */
        s_bot_ok[(uint16_t)card * 2u + 1u] = 0u;
    }

    for (i = 0u; (i < n) && (i < UI_BOTTOM_LINES); i++)
    {
        card = (uint8_t)(i >> 1);
        bg   = ui_card_bg(card, s_card_bar[card]);
        w    = ui_text_w(lines[i]);

        if ((s_bot_ok[i] != 0u) && (strcmp(s_bot[i], lines[i]) == 0) &&
            (s_bot_c[i] == colors[i]))
        {
            continue;                       /* this row did not change */
        }

        y  = (uint16_t)(ui_card_y(card) + 3u + (uint16_t)(i & 1u) * 15u);
        x  = UI_CARD_TXT_X;
        cw = (w > s_bot_w[i]) ? w : s_bot_w[i];

        if ((uint16_t)(x + cw) > (uint16_t)(UI_CARD_X + UI_CARD_W))
            cw = (uint16_t)(UI_CARD_X + UI_CARD_W - x);
        if (cw == 0u) cw = 1u;

        lcd_fill_rect(x, y, cw, 15u, bg);
        ui_text(x, y, lines[i], colors[i], bg);

        strcpy(s_bot[i], lines[i]);
        s_bot_w[i]  = w;
        s_bot_c[i]  = colors[i];
        s_bot_ok[i] = 1u;
    }
}

void ui_bars_invalidate(void)
{
    s_stat_first = 1u;      /* next ui_status_draw rebuilds the whole bar   */
    s_bot_first  = 1u;      /* next ui_bottom_draw clears the readout panel */
}

/* ==================================================================
 * modal menu
 *
 * The panel has a FIXED height of UI_MENU_VIS rows, so its geometry never
 * changes while it is open.  Each row remembers what it put on screen and is
 * repainted part by part: the highlight background, the item text and the
 * right aligned value are compared separately, so scrolling the selection only
 * recolours two rows and editing a value only rewrites that value's box.  The
 * title strip is repainted only when the title really changed.
 * ================================================================== */
#define UI_MENU_X       70
#define UI_MENU_W       180
#define UI_MENU_Y       25
#define UI_MENU_HDR     22
#define UI_MENU_ROW     26
#define UI_MENU_H       (UI_MENU_HDR + UI_MENU_VIS * UI_MENU_ROW + 4)

#define UI_MENU_TXT_X   10u
#define UI_MENU_TXT_DY   5u

/* x of the first item pixel / one past the last value pixel (absolute) */
#define UI_MENU_IT_X    ((uint16_t)(UI_MENU_X + UI_MENU_TXT_X))
#define UI_MENU_VAL_R   ((uint16_t)(UI_MENU_X + UI_MENU_W - UI_MENU_TXT_X))

static char     s_men_title[24];
static uint8_t  s_men_idx[UI_MENU_VIS];       /* 0xFF = slot is empty           */
static char     s_men_it[UI_MENU_VIS][20];    /* item text shown in the slot    */
static char     s_men_val[UI_MENU_VIS][24];   /* value text shown in the slot   */
static uint16_t s_men_bg[UI_MENU_VIS];        /* background currently on screen */
static uint16_t s_men_itw[UI_MENU_VIS];
static uint16_t s_men_valw[UI_MENU_VIS];

/* Repaint one row of the modal panel.
 *
 * Three independent things can change on a row: the background (selection or
 * edit highlight), the item text and the right aligned value.  Each one is
 * compared against what is actually on screen and only the part that really
 * changed gets touched - turning the encoder while editing a value therefore
 * repaints the value box alone instead of clearing and re-drawing the whole
 * row (background + item + value).  That is roughly 5x fewer pixels per click. */
static void ui_menu_row(uint8_t slot, uint8_t idx, const char *item, const char *val,
                        int8_t sel, uint8_t editing)
{
    uint16_t ry = (uint16_t)(UI_MENU_Y + UI_MENU_HDR + (uint16_t)slot * UI_MENU_ROW);
    uint16_t ty = (uint16_t)(ry + UI_MENU_TXT_DY);
    uint16_t bg = UI_MENU_BG;
    uint16_t fg = UI_TEXT;
    uint16_t iw, vw, vx, oldiw, oldvw, oldvx;
    uint16_t lo, hi, lim;
    uint8_t  hl;

    hl = (uint8_t)(((int8_t)idx == sel) ? (editing ? 2u : 1u) : 0u);
    if (hl == 1u)      bg = UI_MENU_SEL;
    else if (hl == 2u) bg = UI_MENU_SEL_ED;
    if (hl != 0u)      fg = COLOR_BLACK;

    iw = ui_text_w(item);
    if (val[0] != '\0')
    {
        vw = ui_text_w(val);
        vx = (uint16_t)(UI_MENU_VAL_R - vw);
    }
    else
    {
        vw = 0u;
        vx = UI_MENU_VAL_R;
    }

    oldiw = s_men_itw[slot];
    oldvw = s_men_valw[slot];
    oldvx = (uint16_t)(UI_MENU_VAL_R - oldvw);

    if ((s_men_idx[slot] == 0xFFu) || (s_men_bg[slot] != bg))
    {
        /* empty slot, or the highlight changed colour: whole row */
        lcd_fill_rect((uint16_t)(UI_MENU_X + 2u), ry,
                      (uint16_t)(UI_MENU_W - 4u), (uint16_t)(UI_MENU_ROW - 2u), bg);
        ui_text(UI_MENU_IT_X, ty, item, fg, bg);
        if (vw != 0u) ui_text(vx, ty, val, fg, bg);
    }
    else
    {
        if ((strcmp(s_men_it[slot], item) != 0) || (iw != oldiw))
        {
            /* clear the wider of the old and the new item box, stopping short
               of the value column so an unchanged value is never clipped */
            lim = (vw > oldvw) ? vw : oldvw;
            lim = (uint16_t)(UI_MENU_VAL_R - lim - 4u);
            if (lim < UI_MENU_IT_X) lim = UI_MENU_IT_X;

            lo = UI_MENU_IT_X;
            hi = (uint16_t)(UI_MENU_IT_X + ((iw > oldiw) ? iw : oldiw) + 2u);
            if (hi > lim) hi = lim;
            if (hi > lo)
            {
                lcd_fill_rect(lo, (uint16_t)(ry + 2u), (uint16_t)(hi - lo),
                              (uint16_t)(UI_MENU_ROW - 4u), bg);
            }
            ui_text(UI_MENU_IT_X, ty, item, fg, bg);
        }

        if ((strcmp(s_men_val[slot], val) != 0) || (vw != oldvw))
        {
            /* clear the wider of the old and the new value box, stopping short
               of the item text on the left */
            lo = (vx < oldvx) ? vx : oldvx;
            lim = (uint16_t)(UI_MENU_IT_X + iw + 4u);
            if (lo < lim) lo = lim;
            hi = UI_MENU_VAL_R;
            if (hi > lo)
            {
                lcd_fill_rect(lo, (uint16_t)(ry + 2u), (uint16_t)(hi - lo),
                              (uint16_t)(UI_MENU_ROW - 4u), bg);
            }
            if (vw != 0u) ui_text(vx, ty, val, fg, bg);
        }
    }

    s_men_idx[slot]  = idx;
    s_men_bg[slot]   = bg;
    s_men_itw[slot]  = iw;
    s_men_valw[slot] = vw;
    strcpy(s_men_it[slot],  item);
    strcpy(s_men_val[slot], val);
}

void ui_menu_draw(const char *title, const char *const *items, const char *const *vals,
                  uint8_t n, uint8_t first, int8_t sel, uint8_t editing)
{
    uint8_t i;
    uint8_t vis;

    if (n == 0u) return;
    if (first >= n) first = 0u;

    vis = (uint8_t)(n - first);
    if (vis > UI_MENU_VIS) vis = UI_MENU_VIS;

    if (s_menu_drawn == 0u)
    {
        /* first paint after the canvas was rebuilt: panel, frame, all rows */
        lcd_fill_rect(UI_MENU_X, UI_MENU_Y, UI_MENU_W, (uint16_t)UI_MENU_H, UI_MENU_BG);
        lcd_draw_rect(UI_MENU_X, UI_MENU_Y, UI_MENU_W, (uint16_t)UI_MENU_H, UI_ACCENT);

        s_menu_drawn = 1u;
        s_men_title[0] = '\0';
        for (i = 0u; i < UI_MENU_VIS; i++)
        {
            s_men_idx[i]  = 0xFFu;
            s_men_bg[i]   = UI_MENU_BG;
            s_men_itw[i]  = 0u;
            s_men_valw[i] = 0u;
        }
    }

    if (strcmp(s_men_title, title) != 0)
    {
        uint16_t tw;

        strcpy(s_men_title, title);
        lcd_fill_rect((uint16_t)(UI_MENU_X + 2u), (uint16_t)(UI_MENU_Y + 2u),
                      (uint16_t)(UI_MENU_W - 4u), (uint16_t)(UI_MENU_HDR - 4u),
                      UI_MENU_BG);
        tw = ui_text_w(title);
        ui_text((uint16_t)(UI_MENU_X + (UI_MENU_W - tw) / 2u), (uint16_t)(UI_MENU_Y + 4u),
                title, UI_ACCENT, UI_MENU_BG);
    }

    for (i = 0u; i < UI_MENU_VIS; i++)
    {
        if (i >= vis)
        {
            /* slot fell out of the visible window (scrolled to the end) */
            if (s_men_idx[i] != 0xFFu)
            {
                uint16_t ry = (uint16_t)(UI_MENU_Y + UI_MENU_HDR + (uint16_t)i * UI_MENU_ROW);

                lcd_fill_rect((uint16_t)(UI_MENU_X + 2u), ry,
                              (uint16_t)(UI_MENU_W - 4u), (uint16_t)(UI_MENU_ROW - 2u),
                              UI_MENU_BG);
                s_men_idx[i]  = 0xFFu;
                s_men_bg[i]   = UI_MENU_BG;
                s_men_itw[i]  = 0u;
                s_men_valw[i] = 0u;
                s_men_it[i][0]  = '\0';
                s_men_val[i][0] = '\0';
            }
            continue;
        }

        ui_menu_row(i, (uint8_t)(first + i), items[first + i], vals[first + i],
                    sel, editing);
    }
}
