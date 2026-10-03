/*
 * bsp_adc.h - Dual-channel ADC acquisition engine
 *
 * ADC1 + ADC2 in dual regular-simultaneous mode, triggered by TIM2 TRGO,
 * transferred by DMA2 Stream0 Channel0 into a circular double-buffer.
 *
 *   CH1 = PA1  -> ADC1 IN1   (ADC1 is the master)
 *   CH2 = PA0  -> ADC2 IN0   (ADC2 is the slave, follows master trigger)
 *
 * Each TIM2 update fires one conversion on BOTH ADCs simultaneously.
 * The 32-bit common data register (CDR) holds [31:16]=ADC2, [15:0]=ADC1.
 * DMA moves CDR (32-bit word) into adc_buf[], 512 words, circular.
 * Half-transfer IRQ  -> first  256 samples ready.
 * Transfer-complete IRQ -> second 256 samples ready.
 *
 * Max sample rate ~1 MHz (ADC clock 42 MHz, 15-cycle sample + 12 = 27 cycles).
 */
#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#include <stdint.h>

/* Double-buffer: 256 samples per half, 2 channels */
#define ADC_HALF_SIZE   256u
#define ADC_BUF_SIZE    (ADC_HALF_SIZE * 2u)   /* 512 x uint32_t */

/* Raw DMA buffer (each word: [15:0]=ADC1/CH1, [31:16]=ADC2/CH2) */
extern volatile uint32_t adc_buf[ADC_BUF_SIZE];

/* Remaining DMA transfers (debug: 512=not started, 0=full, decreasing=running) */
uint32_t adc_dma_remaining(void);

/* Number of completed half-buffers since adc_start(). The ISR increments this
   once per 256-sample half, so a jump of more than one between two polls means
   the caller missed a half (the ring buffer must restart to stay contiguous). */
uint32_t adc_half_count(void);

/* Initialise the ADC engine (GPIO + TIM2 + DMA2 + ADC1/ADC2).
   sample_rate_hz: 1 .. ~1,000,000.  Does NOT start sampling. */
void adc_init(uint32_t sample_rate_hz);

/* Start / stop continuous sampling (TIM2 counter on/off). */
void adc_start(void);
void adc_stop(void);

/* Returns 1 if a new half-buffer is ready (and clears that flag).
   *half_idx_out = 0 (first 256) or 1 (second 256). 0 = no new data. */
int  adc_poll(uint8_t *half_idx_out);

/* Access samples from the most recently reported half-buffer.
   index 0..255. */
uint16_t adc_ch1(uint32_t index);
uint16_t adc_ch2(uint32_t index);

/* Change sample rate at runtime (reconfigure TIM2 period only). */
void adc_set_rate(uint32_t sample_rate_hz);

/* Current configured sample rate in Hz. */
uint32_t adc_get_rate(void);

#endif /* __BSP_ADC_H */
