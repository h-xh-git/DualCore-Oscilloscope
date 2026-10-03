/*
 * osc_proto.cpp - inter-core UART framing protocol (ESP32 side)
 *
 * Mirror of STM32 bsp_proto.c: same frame format, same CRC-8.
 * Transport: HardwareSerial (Serial1) via osc_link.
 *
 * Dual-core UART protocol.
 */
#include "osc_proto.h"
#include "osc_link.h"
#include <string.h>

/* ==================================================================
 * CRC-8 (polynomial 0x07, init 0x00)
 * ==================================================================
 */
uint8_t osc_proto_crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07)
                               : (uint8_t)(crc << 1);
    }
    return crc;
}

static uint8_t crc8_update(uint8_t crc, uint8_t data)
{
    crc ^= data;
    for (int j = 0; j < 8; j++)
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07)
                           : (uint8_t)(crc << 1);
    return crc;
}

/* ==================================================================
 * send
 * ==================================================================
 */
void osc_proto_send(uint8_t type, const uint8_t *payload, uint16_t len)
{
    uint8_t hdr[5];
    uint8_t crc;

    hdr[0] = PROTO_SYNC0;
    hdr[1] = PROTO_SYNC1;
    hdr[2] = type;
    hdr[3] = (uint8_t)(len & 0xFF);
    hdr[4] = (uint8_t)(len >> 8);

    crc = crc8_update(0x00, type);
    crc = crc8_update(crc, hdr[3]);
    crc = crc8_update(crc, hdr[4]);
    for (uint16_t i = 0; i < len; i++)
        crc = crc8_update(crc, payload[i]);

    osc_link_write(hdr, 5);
    if (len > 0)
        osc_link_write(payload, len);
    osc_link_write(&crc, 1);
}

/* ==================================================================
 * receive state machine
 * ==================================================================
 */
enum RxState
{
    RX_SYNC0,
    RX_SYNC1,
    RX_TYPE,
    RX_LEN_LO,
    RX_LEN_HI,
    RX_PAYLOAD,
    RX_CRC
};

static RxState s_state = RX_SYNC0;
static uint8_t  s_type;
static uint16_t s_len;
static uint16_t s_idx;
static uint8_t  s_crc;
static uint8_t  s_buf[PROTO_MAX_PAYLOAD];

void osc_proto_init(void)
{
    s_state = RX_SYNC0;
    s_len   = 0;
    s_idx   = 0;
}

uint8_t osc_proto_recv(uint8_t *type, uint8_t *buf, uint16_t maxlen,
                       uint16_t *len_out)
{
    uint8_t byte;

    while (osc_link_read(&byte, 1) > 0)
    {
        switch (s_state)
        {
        case RX_SYNC0:
            if (byte == PROTO_SYNC0)
                s_state = RX_SYNC1;
            break;

        case RX_SYNC1:
            if (byte == PROTO_SYNC1)
                s_state = RX_TYPE;
            else if (byte == PROTO_SYNC0)
                ;
            else
                s_state = RX_SYNC0;
            break;

        case RX_TYPE:
            s_type = byte;
            s_crc  = crc8_update(0x00, byte);
            s_state = RX_LEN_LO;
            break;

        case RX_LEN_LO:
            s_len = (uint16_t)byte;
            s_crc = crc8_update(s_crc, byte);
            s_state = RX_LEN_HI;
            break;

        case RX_LEN_HI:
        {
            uint16_t full = (uint16_t)(s_len | ((uint16_t)byte << 8));
            s_crc = crc8_update(s_crc, byte);
            s_len = full;
            if (full > PROTO_MAX_PAYLOAD)
            {
                s_state = RX_SYNC0;
                break;
            }
            s_idx = 0;
            s_state = (full == 0) ? RX_CRC : RX_PAYLOAD;
            break;
        }

        case RX_PAYLOAD:
            if (s_idx < PROTO_MAX_PAYLOAD)
                s_buf[s_idx] = byte;
            s_crc = crc8_update(s_crc, byte);
            s_idx++;
            if (s_idx >= s_len)
                s_state = RX_CRC;
            break;

        case RX_CRC:
            s_state = RX_SYNC0;
            if (byte == s_crc)
            {
                uint16_t copy = (s_len <= maxlen) ? s_len : maxlen;
                if (copy > 0)
                    memcpy(buf, s_buf, copy);
                *type    = s_type;
                *len_out = s_len;
                return 1;
            }
            break;
        }
    }
    return 0;
}
