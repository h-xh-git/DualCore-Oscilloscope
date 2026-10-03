/*
 * osc_link - UART link to STM32 (connection-core side)
 * Wired to STM32 USART2: ESP RX <- STM PA2(TX), ESP TX -> STM PA3(RX)
 */
#ifndef OSC_LINK_H
#define OSC_LINK_H

#include <Arduino.h>
#include <stddef.h>

/* init the UART link on given RX/TX pins (HardwareSerial1) */
void    osc_link_init(uint32_t baud, int rxPin, int txPin);

/* bytes available to read */
size_t  osc_link_available(void);

/* read up to maxlen bytes, returns count read */
size_t  osc_link_read(uint8_t *buf, size_t maxlen);

/* blocking write */
void    osc_link_write(const uint8_t *buf, size_t len);
void    osc_link_print(const char *s);

#endif /* OSC_LINK_H */
