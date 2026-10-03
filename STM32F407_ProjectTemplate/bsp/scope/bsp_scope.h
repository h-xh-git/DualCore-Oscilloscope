/*
 * bsp_scope.h - oscilloscope trigger + measurement engine
 *
 * Pure signal processing on a block of raw ADC samples: no peripheral access,
 * so it can be reused by the UART/ESP32 side.
 *
 * Edge trigger + automatic measurements.
 */
#ifndef __BSP_SCOPE_H
#define __BSP_SCOPE_H

#include "stm32f4xx.h"

/* ===================== trigger ===================== */
#define SCOPE_EDGE_RISE     0u
#define SCOPE_EDGE_FALL     1u

#define SCOPE_MODE_AUTO     0u          /* always update, trigger if found */
#define SCOPE_MODE_NORMAL   1u          /* update only on a trigger        */
#define SCOPE_MODE_SINGLE   2u          /* update once, then stop          */

typedef struct
{
    uint8_t  enable;        /* 0 = free running                            */
    uint8_t  channel;       /* 0 = CH1, 1 = CH2                            */
    uint8_t  edge;          /* SCOPE_EDGE_RISE / SCOPE_EDGE_FALL           */
    uint8_t  mode;          /* SCOPE_MODE_*                                */
    uint16_t level;         /* raw ADC counts, 0..4095                     */
} scope_trig_t;

/* Search buf[0..n-1] for the first crossing of `level` in the requested
   direction.  Returns the index of the first sample at/after the level,
   or -1 when the level is never crossed. */
int16_t scope_trigger_find(const scope_trig_t *t, const uint16_t *buf, uint16_t n);

/* ===================== measurements ===================== */
typedef struct
{
    uint16_t vmin;              /* raw counts                   */
    uint16_t vmax;
    uint16_t vpp;
    uint16_t vavg;
    uint16_t vrms;
    uint32_t freq_x100;         /* Hz   * 100, 0 = not measured */
    uint32_t period_us_x100;    /* us   * 100, 0 = not measured */
    uint16_t duty_x10;          /* 0.1 %, 0 = not measured      */
    uint8_t  ac_valid;          /* 1 = freq/period/duty usable  */
} scope_meas_t;

/* Measure one block of `n` raw samples taken at `rate_hz` samples/second.
   Frequency/duty use a hysteresis crossing around the block average, so a
   pure DC input reports ac_valid = 0 instead of a bogus value. */
void scope_measure(const uint16_t *buf, uint16_t n, uint32_t rate_hz, scope_meas_t *m);

/* integer square root (used for Vrms) */
uint16_t scope_isqrt(uint32_t v);

#endif /* __BSP_SCOPE_H */
