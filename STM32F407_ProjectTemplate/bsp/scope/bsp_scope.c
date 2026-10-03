/*
 * bsp_scope.c - trigger detection + automatic measurements
 *
 * All arithmetic is integer only: the Keil target may or may not have the FPU
 * enabled and microlib's printf does not support %f anyway.
 */
#include "bsp_scope.h"

/* ==================================================================
 * trigger
 * ================================================================== */
int16_t scope_trigger_find(const scope_trig_t *t, const uint16_t *buf, uint16_t n)
{
    uint16_t i;
    uint16_t lv = t->level;

    if (n < 2u) return -1;

    for (i = 1u; i < n; i++)
    {
        uint16_t a = buf[i - 1u];
        uint16_t b = buf[i];

        if (t->edge == SCOPE_EDGE_RISE)
        {
            if ((a < lv) && (b >= lv)) return (int16_t)i;
        }
        else
        {
            if ((a > lv) && (b <= lv)) return (int16_t)i;
        }
    }
    return -1;
}

/* ==================================================================
 * integer square root (bit by bit, no floating point)
 * ================================================================== */
uint16_t scope_isqrt(uint32_t v)
{
    uint32_t r = 0u;
    uint32_t b = 1uL << 30;

    while (b > v) b >>= 2;
    while (b != 0u)
    {
        if (v >= (r + b))
        {
            v -= (r + b);
            r = (r >> 1) + b;
        }
        else
        {
            r >>= 1;
        }
        b >>= 2;
    }
    return (uint16_t)r;
}

/* ==================================================================
 * measurements
 * ================================================================== */
#define SCOPE_CROSS_MAX     16u

void scope_measure(const uint16_t *buf, uint16_t n, uint32_t rate_hz, scope_meas_t *m)
{
    uint32_t sum  = 0u;
    uint64_t sum2 = 0u;
    uint16_t mn = 0xFFFFu;
    uint16_t mx = 0u;
    uint16_t i;
    uint32_t mid;
    uint32_t hys;
    uint16_t cross[SCOPE_CROSS_MAX];
    uint8_t  nc = 0u;
    uint8_t  state;

    m->vmin          = 0u;
    m->vmax          = 0u;
    m->vpp           = 0u;
    m->vavg          = 0u;
    m->vrms          = 0u;
    m->freq_x100     = 0u;
    m->period_us_x100 = 0u;
    m->duty_x10      = 0u;
    m->ac_valid      = 0u;

    if (n == 0u) return;

    /* ---- DC statistics ---- */
    for (i = 0u; i < n; i++)
    {
        uint16_t v = buf[i];

        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum  += (uint32_t)v;
        sum2 += (uint64_t)((uint32_t)v * (uint32_t)v);
    }

    m->vmin = mn;
    m->vmax = mx;
    m->vpp  = (uint16_t)(mx - mn);
    m->vavg = (uint16_t)(sum / (uint32_t)n);
    m->vrms = scope_isqrt((uint32_t)(sum2 / (uint64_t)n));

    /* ---- AC statistics: hysteresis crossings around the average ---- */
    mid = (uint32_t)m->vavg;
    hys = (uint32_t)(m->vpp / 8u);
    if (hys < 2u) hys = 2u;

    state = (buf[0] > mid) ? 1u : 0u;

    for (i = 1u; (i < n) && (nc < SCOPE_CROSS_MAX); i++)
    {
        uint32_t v = (uint32_t)buf[i];

        if (state == 0u)
        {
            if (v > (mid + hys))            /* rising crossing */
            {
                cross[nc] = i;
                nc++;
                state = 1u;
            }
        }
        else
        {
            if ((v + hys) < mid) state = 0u;   /* falling crossing */
        }
    }

    if (nc >= 2u)
    {
        uint32_t span    = (uint32_t)(cross[nc - 1u] - cross[0]);
        uint32_t per_q8  = (span << 8) / (uint32_t)(nc - 1u);   /* samples * 256 */

        if ((per_q8 != 0u) && (rate_hz != 0u))
        {
            m->period_us_x100 = (uint32_t)(((uint64_t)per_q8 * 100000000uL) /
                                           ((uint64_t)256u * (uint64_t)rate_hz));
            m->freq_x100      = (uint32_t)(((uint64_t)rate_hz * 25600uL) /
                                           (uint64_t)per_q8);
            m->ac_valid       = 1u;

            /* duty cycle: high time between the first rising edge and the
               falling edge that follows it */
            for (i = (uint16_t)(cross[0] + 1u); i < n; i++)
            {
                if (((uint32_t)buf[i] + hys) < mid)
                {
                    uint32_t high = (uint32_t)(i - cross[0]);
                    uint32_t per  = (uint32_t)(cross[1] - cross[0]);

                    if (per != 0u) m->duty_x10 = (uint16_t)((high * 1000uL) / per);
                    break;
                }
            }
        }
    }
}
