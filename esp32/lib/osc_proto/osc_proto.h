/*
 * osc_proto.h - inter-core UART framing protocol (ESP32 side)
 *
 * Mirror of the STM32 bsp_proto: same frame format, same CRC-8.
 * Transport: HardwareSerial (Serial1) via osc_link.
 *
 * Dual-core UART protocol.
 */
#ifndef OSC_PROTO_H
#define OSC_PROTO_H

#include <stdint.h>

#define PROTO_SYNC0         0xAA
#define PROTO_SYNC1         0x55
#define PROTO_MAX_PAYLOAD   2048

/* ESP32 -> STM32 (commands) */
#define PROTO_CMD_START     0x01
#define PROTO_CMD_STOP      0x02
#define PROTO_CMD_SET_TB    0x03
#define PROTO_CMD_SET_TRIG  0x04
#define PROTO_CMD_REQ_WAVE  0x05
#define PROTO_CMD_REQ_MEAS  0x06
#define PROTO_CMD_REQ_STAT  0x07

/* STM32 -> ESP32 (responses) */
#define PROTO_RESP_WAVE     0x10
#define PROTO_RESP_MEAS     0x11
#define PROTO_RESP_STAT     0x12

/* ESP32 -> STM32 */
#define PROTO_WIFI_STAT     0x20

void     osc_proto_init(void);
void     osc_proto_send(uint8_t type, const uint8_t *payload, uint16_t len);
uint8_t  osc_proto_recv(uint8_t *type, uint8_t *buf, uint16_t maxlen,
                        uint16_t *len_out);
uint8_t  osc_proto_crc8(const uint8_t *data, uint16_t len);

#endif /* OSC_PROTO_H */
