/*
 * main.c - DualCore Scope - main loop, key/encoder handling and UI state machine
 *
 *   CH1 = PA1 (ADC1 IN1)      CH2 = PA0 (ADC2 IN0)
 *   TFT = ST7789 320x240 landscape (SPI1)   OLED = 128x64 (I2C1, PB6/PB7)
 *   Keys K1..K4 = PC0..PC3   encoder = PC6(A)/PC7(B)/PC8(SW)   buzzer = PB8
 *
 * Acquisition model
 * -----------------
 * TIM2 triggers ADC1+ADC2; DMA2 Stream0 moves 512 packed words (256 samples
 * per channel) into a circular buffer and raises an IRQ on every half.
 * main() drains those halves into a 2048 sample ring.  The ISR counts the
 * halves it completed, so a gap (we were too slow) is detected and the ring
 * restarts - that keeps the captured window strictly contiguous, which is
 * what both the trigger search and the frequency measurement rely on.
 *
 * A frame is UI_PLOT_W (300) samples wide.  64 samples of pre-trigger data
 * are kept so the edge lands a little way in from the left.
 */

#include "board.h"
#include "bsp_uart.h"
#include "bsp_lcd.h"
#include "bsp_ui.h"
#include "bsp_oled.h"
#include "bsp_adc.h"
#include "bsp_key.h"
#include "bsp_buzzer.h"
#include "bsp_encoder.h"
#include "bsp_scope.h"
#include "bsp_uart2.h"
#include "bsp_proto.h"
#include "ui_text.h"

#include <stdio.h>
#include <string.h>

/* ==================================================================
 * time base table (samples per second)
 * ================================================================== */
#define RATE_N        13u
#define RATE_DEF_IDX  6u                     /* 10 kHz */

static const uint32_t k_rates[RATE_N] =
{
    100uL, 200uL, 500uL, 1000uL, 2000uL, 5000uL, 10000uL,
    20000uL, 50000uL, 100000uL, 200000uL, 500000uL, 1000000uL
};

/* ==================================================================
 * vertical zoom (divides the ADC span -> magnifies the trace)
 * ================================================================== */
#define ZOOM_N   4u
static const uint8_t k_zoom[ZOOM_N] = { 0u, 1u, 2u, 3u };

/* ==================================================================
 * ring buffer
 * ================================================================== */
#define RING_SIZE   2048u
#define RING_MASK   (RING_SIZE - 1u)

#define WAVE_N      UI_PLOT_W                          /* 300 samples/frame */
#define TRIG_PRE    64u
#define TRIG_POST   (WAVE_N - TRIG_PRE)                /* 236 */
#define TRIG_SEARCH 200u                               /* minimum search window */
/* The trigger window has to cover a span of TIME, not a fixed number of
   samples.  The old window was a flat 200 samples, which is 2 ms at 100 kS/s
   but only 1 ms at 200 kS/s, so it silently lost lock as soon as the time base
   was speeded up: an unlocked frame free-runs (start = total - WAVE_N), the
   trace hops sideways by one capture batch, every column then needs its full
   old extent erased and its new extent painted, and the TFT crawls for as long
   as the lock is missing.  Ask for ~10 ms of pre-trigger history instead,
   clamped to what the ring can hold. */
#define TRIG_SEARCH_MS  10u                            /* wanted window, ms */
#define TRIG_SEARCH_MAX (RING_SIZE - TRIG_POST - TRIG_PRE) /* 1748 samples */
#define RING_MIN    (TRIG_PRE + TRIG_POST + TRIG_SEARCH)   /* 500 */

static uint16_t s_ring1[RING_SIZE];
static uint16_t s_ring2[RING_SIZE];
static uint32_t s_ring_w;        /* samples pushed so far (write index) */
static uint32_t s_half_seen;     /* halves captured so far (adc_half_count unit) */

static uint16_t s_batch1[2u * ADC_HALF_SIZE];
static uint16_t s_batch2[2u * ADC_HALF_SIZE];

static uint16_t s_search[TRIG_SEARCH_MAX];
static uint16_t s_frame1[WAVE_N];
static uint16_t s_frame2[WAVE_N];

/* ==================================================================
 * application state
 * ================================================================== */
static scope_trig_t s_trig;
static scope_meas_t s_m1;
static scope_meas_t s_m2;

static uint8_t s_trig_src;       /* 0 = off, 1 = CH1, 2 = CH2 */
static uint8_t s_rate_idx;
static uint8_t s_zoom_idx;
static uint8_t s_show1;
static uint8_t s_show2;
static uint8_t s_meas_show;
static uint8_t s_running;
static uint8_t s_ui_dirty;       /* 1 = status bar / readout / menu need a refresh */
static uint8_t s_canvas_dirty;   /* 1 = the wave canvas must be rebuilt as well    */
static uint8_t s_trig_hit;
static uint8_t s_have_frame;     /* 1 = s_frame1/s_frame2 hold a valid frame */
static uint8_t s_bars_dirty;     /* 1 = the menu covered the bars, force a rebuild */

static ui_trace_t s_tr1;
static ui_trace_t s_tr2;

/* encoder detents: the TIM8 encoder runs in 4x mode, so every mechanical
   detent produces ENC_DIV counts (change this if the menu moves too fast) */
#define ENC_DIV   4
static int16_t s_enc_acc;

/* ==================================================================
 * menu
 * ================================================================== */
#define MENU_N        10u
#define MENU_TIME     0u
#define MENU_VDIV     1u
#define MENU_CH1      2u
#define MENU_CH2      3u
#define MENU_TRIG     4u
#define MENU_EDGE     5u
#define MENU_LEV      6u
#define MENU_MODE     7u
#define MENU_MEAS     8u
#define MENU_EXIT     9u

static const char *const k_menu_items[MENU_N] =
{
    S_TIMEBASE, S_VDIV, S_CH1, S_CH2, S_TRIG,
    S_EDGE, S_LEV, S_MODE, S_MEAS, S_EXIT
};

static char        s_menu_buf[MENU_N][24];
static const char *s_menu_vals[MENU_N];
static char        s_menu_title[16];
static int8_t      s_menu_sel;
static uint8_t     s_menu_first;
static uint8_t     s_menu_open;
static uint8_t     s_menu_edit;

/* ==================================================================
 * strings / scratch
 * ================================================================== */
static char     s_tb[16];
static char     s_rate[16];
static char     s_vdiv[16];
static char     s_lev[16];
static char     s_status_l[32];

static char     s_meas[UI_BOTTOM_LINES][44];
static const char *s_meas_lines[UI_BOTTOM_LINES];
static uint16_t s_meas_colors[UI_BOTTOM_LINES];

static char     s_oled_last[4][20];

static uint8_t  s_proto_payload[1200];   /* wave TX: 300*u16 CH1 + 300*u16 CH2 */

#define COL_MEAS1   RGB565(  0, 170,   0)
#define COL_MEAS2   RGB565(200, 170,   0)

/* ==================================================================
 * small helpers
 * ================================================================== */
static uint32_t raw_to_mv(uint16_t raw)
{
    return ((uint32_t)raw * 3300uL) / 4096uL;
}

/* one division = 30 samples = 30/rate seconds, printed in tenths of a us */
static void fmt_timebase(uint32_t rate, char *out)
{
    uint32_t t10;

    if (rate == 0u) rate = 1u;
    t10 = 300000000uL / rate;

    if (t10 >= 10000000uL)                       /* >= 1 s      */
        sprintf(out, "%lu.%02lus",
                (unsigned long)(t10 / 10000000uL),
                (unsigned long)((t10 / 100000uL) % 100uL));
    else if (t10 >= 10000uL)                     /* >= 1 ms     */
        sprintf(out, "%lu.%02lums",
                (unsigned long)(t10 / 10000uL),
                (unsigned long)((t10 / 100uL) % 100uL));
    else                                         /* us          */
        sprintf(out, "%lu.%02luus",
                (unsigned long)(t10 / 100uL),
                (unsigned long)(t10 % 100uL));
}

static void fmt_rate(uint32_t rate, char *out)
{
    if (rate >= 1000000uL)
        sprintf(out, "%luMHz", (unsigned long)(rate / 1000000uL));
    else if (rate >= 1000uL)
        sprintf(out, "%lu.%01lukHz",
                (unsigned long)(rate / 1000uL),
                (unsigned long)((rate / 100uL) % 10uL));
    else
        sprintf(out, "%luHz", (unsigned long)rate);
}

/* full scale 3.3 V across 6 divisions = 550 mV/div at zoom 0 */
static void fmt_vdiv(uint8_t zoom, char *out)
{
    uint32_t mv10 = 5500uL >> zoom;              /* tenths of a mV */

    if (mv10 >= 10000uL)
        sprintf(out, "%lu.%02luV",
                (unsigned long)(mv10 / 10000uL),
                (unsigned long)((mv10 / 100uL) % 100uL));
    else
        sprintf(out, "%lu.%01lumV",
                (unsigned long)(mv10 / 10uL),
                (unsigned long)(mv10 % 10uL));
}

static void fmt_freq(uint32_t x100, char *out)
{
    if (x100 == 0u)
    {
        strcpy(out, "--");
    }
    else if (x100 >= 100000000uL)                /* >= 1 MHz */
        sprintf(out, "%lu.%02luMHz",
                (unsigned long)(x100 / 100000000uL),
                (unsigned long)((x100 / 1000000uL) % 100uL));
    else if (x100 >= 100000uL)                   /* >= 1 kHz */
        sprintf(out, "%lu.%02lukHz",
                (unsigned long)(x100 / 100000uL),
                (unsigned long)((x100 / 1000uL) % 100uL));
    else
        sprintf(out, "%lu.%02luHz",
                (unsigned long)(x100 / 100uL),
                (unsigned long)(x100 % 100uL));
}

/* ==================================================================
 * ring buffer / capture
 * ================================================================== */
static void ring_reset(void)
{
    s_ring_w = 0u;
    s_half_seen = 0u;
}

static void capture(void)
{
    uint8_t  half;
    uint32_t c0;
    uint16_t n;
    uint16_t off;
    uint32_t i;

    if (adc_half_count() == s_half_seen) return;    /* nothing new */

    c0 = adc_half_count();
    n  = 0u;

    while (adc_poll(&half) != 0)
    {
        off = (uint16_t)(n * (uint16_t)ADC_HALF_SIZE);
        for (i = 0u; i < ADC_HALF_SIZE; i++)
        {
            s_batch1[(uint16_t)(off + i)] = adc_ch1(i);
            s_batch2[(uint16_t)(off + i)] = adc_ch2(i);
        }
        n++;
        if (n >= 2u) break;                          /* the driver has 2 flags */
    }
    if (n == 0u) return;

    /* contiguous only when the oldest half of this batch directly follows the
       newest half already in the ring */
    if ((s_half_seen == 0u) || ((c0 - (uint32_t)n) != s_half_seen))
        s_ring_w = 0u;

    for (i = 0u; i < ((uint32_t)n * ADC_HALF_SIZE); i++)
    {
        s_ring1[s_ring_w & RING_MASK] = s_batch1[i];
        s_ring2[s_ring_w & RING_MASK] = s_batch2[i];
        s_ring_w++;
    }
    s_half_seen = c0;
}

/* ==================================================================
 * frame window
 *   returns 0 = no frame, 1 = frame (no trigger), 2 = frame (triggered)
 * ================================================================== */
static uint8_t build_frame(void)
{
    uint32_t total = s_ring_w;
    uint32_t base  = 0u;
    uint32_t start;
    uint32_t sn    = 0u;
    int32_t  off = -1;
    uint32_t i;

    if (total == 0u) return 0u;

    s_trig.enable  = (uint8_t)((s_trig_src != 0u) ? 1u : 0u);
    s_trig.channel = (uint8_t)((s_trig_src == 2u) ? 1u : 0u);

    if ((s_trig.enable != 0u) && (total >= RING_MIN))
    {
        /* as much of the pre-trigger history as the ring can still hold */
        /* window that covers TRIG_SEARCH_MS milliseconds at the current rate */
        sn = (adc_get_rate() / 1000u) * (uint32_t)TRIG_SEARCH_MS;
        if (sn > (uint32_t)TRIG_SEARCH_MAX) sn = (uint32_t)TRIG_SEARCH_MAX;
        if (sn < (uint32_t)TRIG_SEARCH)     sn = (uint32_t)TRIG_SEARCH;
        if (sn > (total - TRIG_POST - TRIG_PRE))
        {
            sn = total - TRIG_POST - TRIG_PRE;
        }

        base = total - TRIG_POST - sn;
        for (i = 0u; i < sn; i++)
        {
            uint32_t k = (base + i) & RING_MASK;
            s_search[i] = (s_trig.channel == 0u) ? s_ring1[k] : s_ring2[k];
        }
        off = (int32_t)scope_trigger_find(&s_trig, s_search, (uint16_t)sn);
    }

    if (off >= 0)
    {
        start = (uint32_t)((int32_t)base + off - (int32_t)TRIG_PRE);
    }
    else
    {
        if (total < WAVE_N) return 0u;
        start = total - WAVE_N;
    }

    for (i = 0u; i < WAVE_N; i++)
    {
        uint32_t k = start + i;
        if (k >= total) k = total - 1u;
        s_frame1[i] = s_ring1[k & RING_MASK];
        s_frame2[i] = s_ring2[k & RING_MASK];
    }

    return (off >= 0) ? 2u : 1u;
}

/* ==================================================================
 * text under the waveform
 * ================================================================== */
static void meas_channel(uint8_t slot, const char *name, const scope_meas_t *m, uint8_t vis)
{
    uint8_t l1 = (uint8_t)(slot * 2u);
    uint8_t l2 = (uint8_t)(l1 + 1u);
    char    fb[20];

    if (vis == 0u)
    {
        sprintf(s_meas[l1], "%s %s", name, S_OFF);
        s_meas[l2][0] = '\0';
        return;
    }

    sprintf(s_meas[l1], "%s %s %umV %s %umV",
            name,
            S_VPP, (unsigned int)raw_to_mv(m->vpp),
            S_VAVG, (unsigned int)raw_to_mv(m->vavg));

    if (m->ac_valid != 0u)
    {
        fmt_freq(m->freq_x100, fb);
        sprintf(s_meas[l2], "  %s %s %s %lu.%01lu%%",
                S_FREQ, fb, S_DUTY,
                (unsigned long)(m->duty_x10 / 10u),
                (unsigned long)(m->duty_x10 % 10u));
    }
    else
    {
        sprintf(s_meas[l2], "  %s --", S_FREQ);
    }
}

static void refresh_bottom(void)
{
    uint8_t i;

    if (s_meas_show == 0u)
    {
        sprintf(s_meas[0], "%s %s", S_MEAS, S_OFF);
        s_meas[1][0] = '\0';
        s_meas[2][0] = '\0';
        s_meas[3][0] = '\0';
    }
    else
    {
        meas_channel(0u, S_CH1, &s_m1, s_show1);
        meas_channel(1u, S_CH2, &s_m2, s_show2);
    }

    for (i = 0u; i < UI_BOTTOM_LINES; i++)
        s_meas_lines[i] = s_meas[i];

    s_meas_colors[0] = s_show1 ? UI_CH1_COLOR : UI_OFF_COLOR;
    s_meas_colors[1] = COL_MEAS1;
    s_meas_colors[2] = s_show2 ? UI_CH2_COLOR : UI_OFF_COLOR;
    s_meas_colors[3] = COL_MEAS2;

    ui_bottom_draw(s_meas_lines, s_meas_colors, UI_BOTTOM_LINES);
}

static void refresh_status(void)
{
    char mid[8];

    fmt_timebase(adc_get_rate(), s_tb);
    fmt_rate(adc_get_rate(), s_rate);
    sprintf(s_status_l, "%s %s", S_TIMEBASE, s_tb);
    strcpy(mid, (s_running != 0u) ? S_RUN : S_STOP);

    ui_status_draw(s_status_l, mid, s_rate, s_running);
}

/* ==================================================================
 * menu
 * ================================================================== */
static void menu_build_vals(void)
{
    fmt_timebase(adc_get_rate(), s_tb);
    fmt_vdiv(s_zoom_idx, s_vdiv);
    sprintf(s_lev, "%umV", (unsigned int)raw_to_mv(s_trig.level));

    strcpy(s_menu_buf[MENU_TIME], s_tb);
    strcpy(s_menu_buf[MENU_VDIV], s_vdiv);
    strcpy(s_menu_buf[MENU_CH1], (s_show1 != 0u) ? S_ON : S_OFF);
    strcpy(s_menu_buf[MENU_CH2], (s_show2 != 0u) ? S_ON : S_OFF);
    strcpy(s_menu_buf[MENU_TRIG], (s_trig_src == 0u) ? S_OFF : ((s_trig_src == 1u) ? S_CH1 : S_CH2));
    strcpy(s_menu_buf[MENU_EDGE], (s_trig.edge == SCOPE_EDGE_RISE) ? S_RISE : S_FALL);
    strcpy(s_menu_buf[MENU_LEV], s_lev);
    strcpy(s_menu_buf[MENU_MODE], (s_trig.mode == SCOPE_MODE_AUTO) ? S_AUTO :
                                  ((s_trig.mode == SCOPE_MODE_NORMAL) ? S_NORM : S_SINGL));
    strcpy(s_menu_buf[MENU_MEAS], (s_meas_show != 0u) ? S_ON : S_OFF);
    strcpy(s_menu_buf[MENU_EXIT], "");

    {
        uint8_t i;
        for (i = 0u; i < MENU_N; i++) s_menu_vals[i] = s_menu_buf[i];
    }

    sprintf(s_menu_title, "%s %u/%u", S_MENU,
            (unsigned int)(s_menu_sel + 1), (unsigned int)MENU_N);
}

static void draw_menu(void)
{
    if (s_menu_open == 0u) return;
    menu_build_vals();
    ui_menu_draw(s_menu_title, k_menu_items, s_menu_vals,
                 (uint8_t)MENU_N, s_menu_first, s_menu_sel, s_menu_edit);
}

static void menu_dismiss(void)
{
    s_menu_open  = 0u;
    s_menu_edit  = 0u;
    s_bars_dirty = 1u;      /* the panel overlapped the readout lines */
    s_ui_dirty   = 1u;
    s_canvas_dirty = 1u;    /* ... and the wave canvas underneath it  */
}

static void menu_activate(void)
{
    switch (s_menu_sel)
    {
    case MENU_TIME:
    case MENU_VDIV:
    case MENU_LEV:
        s_menu_edit = (uint8_t)((s_menu_edit != 0u) ? 0u : 1u);
        break;
    case MENU_CH1:
        s_show1 = (uint8_t)((s_show1 != 0u) ? 0u : 1u);
        s_ui_dirty = 1u;
        break;
    case MENU_CH2:
        s_show2 = (uint8_t)((s_show2 != 0u) ? 0u : 1u);
        s_ui_dirty = 1u;
        break;
    case MENU_TRIG:
        s_trig_src = (uint8_t)((s_trig_src + 1u) % 3u);
        s_ui_dirty = 1u;
        break;
    case MENU_EDGE:
        s_trig.edge = (uint8_t)((s_trig.edge == SCOPE_EDGE_RISE) ? SCOPE_EDGE_FALL : SCOPE_EDGE_RISE);
        s_ui_dirty = 1u;
        break;
    case MENU_MODE:
        s_trig.mode = (uint8_t)((s_trig.mode + 1u) % 3u);
        s_ui_dirty = 1u;
        break;
    case MENU_MEAS:
        s_meas_show = (uint8_t)((s_meas_show != 0u) ? 0u : 1u);
        refresh_bottom();
        break;
    default:
        menu_dismiss();
        break;
    }
}

/* ==================================================================
 * time base changes
 * ================================================================== */
static void step_rate(int8_t dir)
{
    int16_t idx = (int16_t)s_rate_idx + (int16_t)dir;

    if (idx < 0) idx = 0;
    if (idx > (int16_t)(RATE_N - 1u)) idx = (int16_t)(RATE_N - 1u);
    if ((uint8_t)idx == s_rate_idx) return;

    s_rate_idx = (uint8_t)idx;
    adc_set_rate(k_rates[s_rate_idx]);
    ring_reset();                       /* old samples are at the old rate */
    s_ui_dirty = 1u;
}

static void encoder_apply(int16_t steps)
{
    if (steps == 0) return;

    if ((s_menu_open != 0u) && (s_menu_edit == 0u))
    {
        int16_t sel = (int16_t)s_menu_sel + steps;

        if (sel < 0) sel = 0;
        if (sel > (int16_t)(MENU_N - 1u)) sel = (int16_t)(MENU_N - 1u);
        s_menu_sel = (int8_t)sel;

        if (s_menu_sel < (int8_t)s_menu_first)
            s_menu_first = (uint8_t)s_menu_sel;
        if ((uint8_t)s_menu_sel >= (uint8_t)(s_menu_first + UI_MENU_VIS))
            s_menu_first = (uint8_t)((uint8_t)s_menu_sel - UI_MENU_VIS + 1u);

        draw_menu();
        return;
    }

    if ((s_menu_open != 0u) && (s_menu_edit != 0u))
    {
        switch (s_menu_sel)
        {
        case MENU_TIME:
            step_rate((int8_t)steps);
            break;
        case MENU_VDIV:
            {
                int16_t z = (int16_t)s_zoom_idx + steps;
                if (z < 0) z = 0;
                if (z > (int16_t)(ZOOM_N - 1u)) z = (int16_t)(ZOOM_N - 1u);
                s_zoom_idx = (uint8_t)z;
            }
            break;
        case MENU_LEV:
            {
                int32_t lv = (int32_t)s_trig.level + (int32_t)steps * 64;
                if (lv < 0) lv = 0;
                if (lv > 4095) lv = 4095;
                s_trig.level = (uint16_t)lv;
                ui_trigger_strip(s_trig.level, s_trig.enable, s_trig_hit);
            }
            break;
        default:
            break;
        }
        draw_menu();
        return;
    }

    step_rate((int8_t)steps);
}

/* ==================================================================
 * OLED (128x64 text panel)
 * ================================================================== */
static void oled_line(uint8_t idx, const char *s)
{
    if (strcmp(s_oled_last[idx], s) == 0) return;
    strcpy(s_oled_last[idx], s);

    OLED_ShowString(0u, (uint8_t)(idx * 16u), "                ", 16u, 1u);
    if (idx == 0u) OLED_ShowStringBold(0u, 0u, s, 16u, 1u);
    else           OLED_ShowString(0u, (uint8_t)(idx * 16u), s, 16u, 1u);
    OLED_Refresh();
}

static void oled_update(void)
{
    char buf[20];

    oled_line(0u, "DualCore Scope");
    oled_line(1u, (s_running != 0u) ? "RUN" : "STOP");

    sprintf(buf, "T %s", s_tb);
    oled_line(2u, buf);

    if (s_trig_src == 0u)          sprintf(buf, "TR OFF");
    else if (s_trig.mode == SCOPE_MODE_AUTO)   sprintf(buf, "TR AUTO");
    else if (s_trig.mode == SCOPE_MODE_NORMAL) sprintf(buf, "TR NORM");
    else                           sprintf(buf, "TR SINGL");
    oled_line(3u, buf);
}

/* ==================================================================
 * inter-core protocol
 * ================================================================== */
static void proto_send_stat(void)
{
    uint8_t  s[11];
    uint32_t sr;

    s[0u]  = s_running;
    s[1u]  = s_rate_idx;
    s[2u]  = s_trig.mode;
    s[3u]  = s_trig.edge;
    s[4u]  = s_trig.channel;
    s[5u]  = (uint8_t)(s_trig.level & 0xFFu);
    s[6u]  = (uint8_t)(s_trig.level >> 8);

    sr = adc_get_rate();
    s[7u]  = (uint8_t)(sr);
    s[8u]  = (uint8_t)(sr >> 8);
    s[9u]  = (uint8_t)(sr >> 16);
    s[10u] = (uint8_t)(sr >> 24);

    proto_send(PROTO_RESP_STAT, s, 11u);
}

static void proto_send_meas(void)
{
    uint8_t m[20];

    /* CH1: vpp u16, vavg u16, freq_x100 u32, duty_x10 u16 */
    m[0u]  = (uint8_t)(s_m1.vpp);
    m[1u]  = (uint8_t)(s_m1.vpp >> 8);
    m[2u]  = (uint8_t)(s_m1.vavg);
    m[3u]  = (uint8_t)(s_m1.vavg >> 8);
    m[4u]  = (uint8_t)(s_m1.freq_x100);
    m[5u]  = (uint8_t)(s_m1.freq_x100 >> 8);
    m[6u]  = (uint8_t)(s_m1.freq_x100 >> 16);
    m[7u]  = (uint8_t)(s_m1.freq_x100 >> 24);
    m[8u]  = (uint8_t)(s_m1.duty_x10);
    m[9u]  = (uint8_t)(s_m1.duty_x10 >> 8);

    m[10u] = (uint8_t)(s_m2.vpp);
    m[11u] = (uint8_t)(s_m2.vpp >> 8);
    m[12u] = (uint8_t)(s_m2.vavg);
    m[13u] = (uint8_t)(s_m2.vavg >> 8);
    m[14u] = (uint8_t)(s_m2.freq_x100);
    m[15u] = (uint8_t)(s_m2.freq_x100 >> 8);
    m[16u] = (uint8_t)(s_m2.freq_x100 >> 16);
    m[17u] = (uint8_t)(s_m2.freq_x100 >> 24);
    m[18u] = (uint8_t)(s_m2.duty_x10);
    m[19u] = (uint8_t)(s_m2.duty_x10 >> 8);

    proto_send(PROTO_RESP_MEAS, m, 20u);
}

static void proto_send_wave(void)
{
    memcpy(s_proto_payload, s_frame1, 600u);
    memcpy(&s_proto_payload[600], s_frame2, 600u);
    proto_send(PROTO_RESP_WAVE, s_proto_payload, 1200u);
}

static void proto_handle(void)
{
    uint8_t  ptype;
    uint8_t  cmd[8];
    uint16_t plen;
    uint8_t  mode;
    uint8_t  edge;
    uint8_t  chan;
    uint16_t level;

    while (proto_recv(&ptype, cmd, sizeof(cmd), &plen) != 0u)
    {
        switch (ptype)
        {
        case PROTO_CMD_START:
            if (s_running == 0u)
            {
                s_running = 1u;
                ring_reset();
                s_ui_dirty = 1u;
                refresh_status();
            }
            break;

        case PROTO_CMD_STOP:
            if (s_running != 0u)
            {
                s_running = 0u;
                refresh_status();
            }
            break;

        /* Only act when the value really changes: the ESP32 re-sends its
         * configuration every cycle, and an unconditional s_ui_dirty would
         * repaint the whole 320x150 canvas (~48 000 px over SPI) each time. */
        case PROTO_CMD_SET_TB:
            if ((plen >= 1u) && (cmd[0u] < (uint8_t)RATE_N) &&
                (cmd[0u] != s_rate_idx))
            {
                s_rate_idx = cmd[0u];
                adc_set_rate(k_rates[s_rate_idx]);
                ring_reset();
                s_ui_dirty = 1u;
                refresh_status();
            }
            break;

        case PROTO_CMD_SET_TRIG:
            if (plen >= 5u)
            {
                mode  = cmd[0u];
                edge  = cmd[1u];
                chan  = cmd[2u];
                level = (uint16_t)(cmd[3u] | ((uint16_t)cmd[4u] << 8));

                /* s_trig_src is the single source of truth for which channel
                 * to trigger on (0=off,1=CH1,2=CH2).  build_frame() derives
                 * s_trig.channel from it every call, so we must NOT set
                 * s_trig.channel directly here -- it would be overwritten.
                 * Compare against the derived value to detect real changes. */
                if ((mode != s_trig.mode) || (edge != s_trig.edge) ||
                    (chan != ((s_trig_src == 2u) ? 1u : 0u)) ||
                    (level != s_trig.level))
                {
                    s_trig.mode  = mode;
                    s_trig.edge  = edge;
                    s_trig_src   = (uint8_t)(chan + 1u);  /* 0->CH1(1), 1->CH2(2) */
                    s_trig.level = level;
                    s_ui_dirty = 1u;

                    /* Moving the level makes the UI erase the old dashed row,
                       which also takes whatever trace pixels were on it.  Only
                       rebuild the canvas when the frame loop will not paint
                       them back on its next pass. */
                    if ((s_running == 0u) || (s_menu_open != 0u)) s_canvas_dirty = 1u;

                    refresh_status();
                }
            }
            break;

        case PROTO_CMD_REQ_STAT:
            proto_send_stat();
            break;

        case PROTO_CMD_REQ_MEAS:
            proto_send_meas();
            break;

        case PROTO_CMD_REQ_WAVE:
            proto_send_wave();
            break;

        default:
            break;
        }
    }
}

/* ==================================================================
 * main
 * ================================================================== */
int main(void)
{
    uint8_t  k;
    int16_t  d;
    int16_t  steps;
    uint8_t  btn;
    uint8_t  btn_prev;
    uint8_t  r;
    uint8_t  rebuilt;
    uint32_t ms;
    uint32_t t_render;
    uint32_t t_ui;
    uint32_t t_meas;
    uint32_t t_proto;
    uint8_t  i;

    /* ---------- hardware ---------- */
    board_init();
    uart1_init(115200u);
    printf("\r\nDualCore Scope ready\r\n");
    OLED_Init();
    lcd_init();
    lcd_set_backlight(1u);
    key_init();
    buzzer_init();
    encoder_init();
    uart2_init(460800u);
    proto_init();

    /* ---------- state ---------- */
    s_trig_src  = 1u;                       /* trigger on CH1 */
    s_trig.enable = 1u;
    s_trig.channel = 0u;
    s_trig.edge = SCOPE_EDGE_RISE;
    s_trig.mode = SCOPE_MODE_AUTO;
    s_trig.level = 2048u;

    s_rate_idx  = RATE_DEF_IDX;
    s_zoom_idx  = 0u;
    s_show1     = 1u;
    s_show2     = 1u;
    s_meas_show = 1u;
    s_running   = 1u;
    s_ui_dirty  = 1u;
    s_canvas_dirty = 1u;
    s_trig_hit  = 0u;
    s_menu_open = 0u;
    s_menu_edit = 0u;
    s_menu_sel  = 0;
    s_menu_first = 0u;
    s_enc_acc   = 0;
    btn_prev    = 0u;

    ring_reset();
    for (i = 0u; i < 4u; i++) s_oled_last[i][0] = '\0';

    ui_init();
    ui_trace_reset(&s_tr1, UI_CH1_COLOR);
    ui_trace_reset(&s_tr2, UI_CH2_COLOR);
    ui_trigger_strip(s_trig.level, 1u, 0u);
    refresh_status();
    refresh_bottom();
    oled_update();

    /* ---------- acquisition ---------- */
    adc_init(k_rates[s_rate_idx]);
    adc_start();

    ms = 0u;
    t_render = 0u;
    t_ui     = 0u;
    t_meas   = 0u;
    t_proto  = 0u;

    for (;;)
    {
        ms++;

        /* ---- 1. acquisition first: keeps the ring contiguous ---- */
        if (s_running != 0u) capture();

        /* ---- 2. keys ---- */
        k = key_scan();
        if (k != KEY_NONE)
        {
            buzzer_beep(15u);
            if (k == KEY_1)
            {
                step_rate(-1);
            }
            else if (k == KEY_2)
            {
                step_rate(+1);
            }
            else if (k == KEY_3)
            {
                s_running = (uint8_t)((s_running != 0u) ? 0u : 1u);
                if (s_running != 0u) ring_reset();
                s_ui_dirty = 1u;
            }
            else
            {
                /* Opening only paints the panel on top of what is already
                   there, so the canvas survives; closing must rebuild it
                   because the panel was covering part of the graticule. */
                if (s_menu_open != 0u)
                {
                    s_bars_dirty   = 1u;    /* panel is going away        */
                    s_canvas_dirty = 1u;    /* it covered the wave canvas */
                    s_menu_open    = 0u;
                }
                else
                {
                    s_menu_open = 1u;
                }
                s_menu_edit = 0u;
                s_ui_dirty  = 1u;
            }
        }

        /* ---- 3. encoder ---- */
        d = encoder_delta();
        s_enc_acc = (int16_t)(s_enc_acc + d);
        steps = 0;
        while (s_enc_acc >= ENC_DIV)  { s_enc_acc = (int16_t)(s_enc_acc - ENC_DIV); steps++; }
        while (s_enc_acc <= -ENC_DIV) { s_enc_acc = (int16_t)(s_enc_acc + ENC_DIV); steps--; }
        encoder_apply(steps);

        btn = encoder_button();
        if ((btn != 0u) && (btn_prev == 0u))
        {
            buzzer_beep(15u);
            if (s_menu_open == 0u)
            {
                s_menu_open = 1u;
                s_menu_edit = 0u;
                s_ui_dirty  = 1u;
            }
            else
            {
                menu_activate();
            }
        }
        btn_prev = btn;

        /* ---- 4. UI repaint (menu toggles, time base changes, ...) ----
         *
         * Most events only change text (time base, trigger source, edge,
         * mode, start/stop).  Those must NOT wipe the 300x150 wave canvas:
         * regridding it costs ~48 000 pixels over SPI, i.e. a visible hitch
         * on every encoder click, and the graticule never changes anyway.
         * Only s_canvas_dirty - a menu closing over it, a channel being
         * switched off, the first paint - really needs the rebuild. */
        if (s_ui_dirty != 0u)
        {
            s_ui_dirty = 0u;
            rebuilt    = 0u;

            if (s_canvas_dirty != 0u)
            {
                s_canvas_dirty = 0u;
                rebuilt = 1u;

                ui_canvas_init();

                if (s_bars_dirty != 0u)
                {
                    s_bars_dirty = 0u;
                    ui_bars_invalidate();   /* menu may have painted over them */
                }

                ui_trace_reset(&s_tr1, UI_CH1_COLOR);
                ui_trace_reset(&s_tr2, UI_CH2_COLOR);
            }

            /* the level line lives in the canvas background, so it has to be
               re-stated whenever the canvas was wiped; the call is cached in
               the UI layer and costs nothing when the level did not move */
            ui_trigger_strip(s_trig.level, s_trig.enable, s_trig_hit);

            /* the canvas was just cleared: put the held frame back while
               stopped, otherwise the grid would come up empty */
            if ((rebuilt != 0u) && (s_running == 0u) && (s_have_frame != 0u))
            {
                if (s_show1 != 0u) ui_trace_draw(&s_tr1, s_frame1, (uint16_t)WAVE_N, k_zoom[s_zoom_idx]);
                if (s_show2 != 0u) ui_trace_draw(&s_tr2, s_frame2, (uint16_t)WAVE_N, k_zoom[s_zoom_idx]);
            }

            refresh_status();
            /* the modal panel covers the readout lines: repainting them now
               would only be overwritten by draw_menu() right below */
            if (s_menu_open == 0u) refresh_bottom();
            draw_menu();
        }

        /* ---- 5. waveform, ~25 fps ---- */
        /* Nothing at all is repainted while stopped: the whole screen keeps
           the frozen frame, so STOP really freezes the picture. */
        if ((s_running != 0u) && (s_menu_open == 0u) &&
            ((uint32_t)(ms - t_render) >= 40u))
        {
            t_render = ms;
            r = build_frame();

            if (r != 0u)
            {
                uint8_t draw = 0u;

                s_have_frame = 1u;

                if (r == 2u)                          draw = 1u;
                else if (s_trig.enable == 0u)         draw = 1u;
                else if (s_trig.mode == SCOPE_MODE_AUTO) draw = 1u;

                if (draw != 0u)
                {
                    scope_measure(s_frame1, (uint16_t)WAVE_N, adc_get_rate(), &s_m1);
                    scope_measure(s_frame2, (uint16_t)WAVE_N, adc_get_rate(), &s_m2);

                    if (s_show1 != 0u) ui_trace_draw(&s_tr1, s_frame1, (uint16_t)WAVE_N, k_zoom[s_zoom_idx]);
                    if (s_show2 != 0u) ui_trace_draw(&s_tr2, s_frame2, (uint16_t)WAVE_N, k_zoom[s_zoom_idx]);

                    s_trig_hit = (uint8_t)((r == 2u) ? 1u : 0u);
                    ui_trigger_strip(s_trig.level, s_trig.enable, s_trig_hit);

                    if ((s_trig.mode == SCOPE_MODE_SINGLE) && (r == 2u))
                    {
                        s_running = 0u;               /* freeze on the first edge */
                        refresh_status();
                    }
                }
            }
        }

        /* ---- 6. measurement readout, ~7 Hz (repainting it every frame
           would flicker, so it is refreshed on its own timer) ---- */
        /* Skipped while the menu is open: the panel reaches down into the
           readout area and the lines would be painted over it. */
        if ((s_running != 0u) && (s_menu_open == 0u) &&
            ((uint32_t)(ms - t_meas) >= 150u))
        {
            t_meas = ms;
            refresh_bottom();
        }

        /* ---- 7. OLED, ~3 Hz (only changed lines are pushed out) ---- */
        if ((uint32_t)(ms - t_ui) >= 300u)
        {
            t_ui = ms;
            oled_update();
        }

        /* ---- 8. inter-core protocol: service the ESP32 ---- */
        proto_handle();

        /* ---- 9. periodic status push to the ESP32, ~1 Hz ---- */
        if ((uint32_t)(ms - t_proto) >= 1000u)
        {
            t_proto = ms;
            proto_send_stat();
        }

        delay_ms(1u);
    }
}
