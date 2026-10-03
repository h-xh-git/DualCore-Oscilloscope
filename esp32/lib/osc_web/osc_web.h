/*
 * osc_web.h - WiFi AP + WebServer + OTA (ESP32 connectivity core)
 *
 * Turns the ESP32 into a self-contained web oscilloscope.
 * The ESP32 hosts a WiFi AP, serves an HTML scope page, and bridges
 * web clients to the STM32 via the framed UART protocol (osc_proto).
 *
 * Data flow:
 *   main loop -> osc_proto_recv -> cache STAT/MEAS/WAVE -> web clients
 *   web clients -> /cmd -> osc_proto_send -> STM32
 */
#ifndef OSC_WEB_H
#define OSC_WEB_H

#include <stdint.h>

/* Start WiFi AP + WebServer + ArduinoOTA.
 * Call once in setup().  Serial must already be up for diagnostic prints. */
void osc_web_init(void);

/* Handle HTTP clients + OTA.  Call every loop() iteration. */
void osc_web_poll(void);

/* Cache the latest frames received from the STM32 so that web clients
 * always read fresh data without stalling the UART link. */
void osc_web_cache_stat(const uint8_t *data, uint16_t len);  /* 11 B */
void osc_web_cache_meas(const uint8_t *data, uint16_t len);  /* 20 B */
void osc_web_cache_wave(const uint8_t *data, uint16_t len);  /* 1200 B */

#endif /* OSC_WEB_H */
