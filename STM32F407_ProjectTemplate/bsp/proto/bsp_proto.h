/*
 * bsp_proto.h - inter-core UART framing protocol (STM32 side)
 *
 * Frame format (little-endian, CRC-8 protected):
 *   [0xAA][0x55][type][len_lo][len_hi][payload...][crc8]
 *   CRC-8 covers type + len_lo + len_hi + payload (poly 0x07, init 0x00)
 *
 * Dual-core UART protocol between STM32 and ESP32.
 */
#ifndef BSP_PROTO_H
#define BSP_PROTO_H

#include <stdint.h>

#define PROTO_SYNC0         0xAAu
#define PROTO_SYNC1         0x55u
#define PROTO_MAX_PAYLOAD   2048u

/* ---- ESP32 -> STM32 (commands) ---- */
#define PROTO_CMD_START     0x01u      /* payload: none                      */
#define PROTO_CMD_STOP      0x02u      /* payload: none                      */
#define PROTO_CMD_SET_TB    0x03u      /* payload: [1] rate_idx              */
#define PROTO_CMD_SET_TRIG  0x04u      /* payload: [mode][edge][ch][lev_lo][lev_hi] */
#define PROTO_CMD_REQ_WAVE  0x05u      /* payload: none                      */
#define PROTO_CMD_REQ_MEAS  0x06u      /* payload: none                      */
#define PROTO_CMD_REQ_STAT  0x07u      /* payload: none                      */

/* ---- STM32 -> ESP32 (responses) ---- */
#define PROTO_RESP_WAVE     0x10u      /* 1200 B: 300*u16 CH1 + 300*u16 CH2  */
#define PROTO_RESP_MEAS     0x11u      /*  20 B: 2 * {vpp vavg freq duty}    */
#define PROTO_RESP_STAT     0x12u      /*  11 B: run/tb/trig/level/rate     */

/* ---- ESP32 -> STM32 notifications ---- */
#define PROTO_WIFI_STAT     0x20u

/* ---- API ---- */

/* Reset the receive state machine. */
void proto_init(void);

/* Send one framed message.  Non-blocking: the frame is queued into the
 * UART2 TX ring and drained by the TXE interrupt.  If the ring cannot hold
 * the whole frame it is dropped and counted in proto_drops(). */
void proto_send(uint8_t type, const uint8_t *payload, uint16_t len);

/* Frames + bytes discarded because the TX ring was full (0 = healthy). */
uint32_t proto_drops(void);

/* Non-blocking receive.  Returns 1 when a complete valid frame is ready. */
uint8_t proto_recv(uint8_t *type, uint8_t *buf, uint16_t maxlen,
                   uint16_t *len_out);

/* CRC-8 (poly 0x07, init 0x00) over a block of data. */
uint8_t proto_crc8(const uint8_t *data, uint16_t len);

#endif /* BSP_PROTO_H */
