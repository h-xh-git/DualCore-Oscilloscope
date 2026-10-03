/*
 * bsp_uart2 - USART2 link to ESP32 (PA2=TX, PA3=RX, AF7)
 * Inter-core communication port for the dual-core oscilloscope.
 * Standard Peripheral Driver based. Library files are NOT modified.
 */
#ifndef __BSP_UART2_H__
#define __BSP_UART2_H__

#include "stm32f4xx.h"
#include <stdint.h>

/* init USART2 at given baud, RX interrupt + ring buffer */
void     uart2_init(uint32_t baud);

/* non-blocking send: the byte is queued into the TX ring and pushed out by
 * the TXE interrupt.  A byte is silently dropped (s_tx_drops) only if the
 * ring is completely full, so the caller never stalls the main loop. */
void     uart2_putc(uint8_t c);
void     uart2_puts(const char *s);

/* free space left in the TX ring (bytes that can still be queued) */
uint16_t uart2_tx_free(void);

/* bytes dropped because the TX ring was full */
uint32_t uart2_tx_drops(void);

/* wait until everything queued has left the ring (TX shift reg may still
 * hold one byte).  Only for shutdown/debug - never call from the hot loop. */
void     uart2_tx_flush(void);

/* non-blocking receive from ring buffer. returns 1 if byte fetched, 0 if empty */
int      uart2_getc(uint8_t *out_c);

/* bytes waiting in RX ring */
uint16_t uart2_available(void);

/* IRQ handler (to be wired in, declared here so main/stm32f4xx_it can reference) */
void USART2_IRQHandler(void);

#endif /* __BSP_UART2_H__ */
