/*
 * bsp_proto.c - inter-core UART framing protocol (STM32 side)
 *
 * Transport: USART2 (bsp_uart2) at 460800 baud.
 * Dual-core UART protocol.
 */
#include "bsp_proto.h"
#include "bsp_uart2.h"
#include <string.h>

/* ==================================================================
 * CRC-8 (polynomial 0x07, init 0x00) - CRC-8/SMBus variant
 * ==================================================================
 */
uint8_t proto_crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00u;
    uint16_t i;

    for (i = 0u; i < len; i++)
    {
        uint8_t j;
        crc ^= data[i];
        for (j = 0u; j < 8u; j++)
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u)
                                : (uint8_t)(crc << 1);
    }
    return crc;
}

/* single-byte CRC update (used by the receive state machine) */
static uint8_t crc8_update(uint8_t crc, uint8_t data)
{
    uint8_t j;
    crc ^= data;
    for (j = 0u; j < 8u; j++)
        crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u)
                            : (uint8_t)(crc << 1);
    return crc;
}

/* ==================================================================
 * send
 * ==================================================================
 */
static uint32_t s_frame_drops = 0;

void proto_send(uint8_t type, const uint8_t *payload, uint16_t len)
{
    uint8_t hdr[5u];
    uint8_t crc;
    uint16_t i;

    /* Never stall the main loop: if the TX ring cannot take the whole frame
     * (5 B header + payload + 1 B CRC) drop it instead of spinning.  The
     * receiver resynchronises on the next [0xAA][0x55].  The ring is 2048 B
     * and the largest real frame is 1206 B, so this can only trigger if two
     * frames collide inside one frame time (~26 ms at 460800 baud).
     * uart2_tx_free() cannot shrink below the value read here (the ISR only
     * ever frees space), so the frame is queued atomically. */
    if (uart2_tx_free() < (uint16_t)(len + 6u))
    {
        s_frame_drops++;
        return;
    }

    hdr[0u] = PROTO_SYNC0;
    hdr[1u] = PROTO_SYNC1;
    hdr[2u] = type;
    hdr[3u] = (uint8_t)(len & 0xFFu);
    hdr[4u] = (uint8_t)(len >> 8);

    /* CRC over type + len_lo + len_hi + payload */
    crc = crc8_update(0x00u, type);
    crc = crc8_update(crc, hdr[3u]);
    crc = crc8_update(crc, hdr[4u]);
    for (i = 0u; i < len; i++)
        crc = crc8_update(crc, payload[i]);

    uart2_putc(hdr[0u]);
    uart2_putc(hdr[1u]);
    uart2_putc(hdr[2u]);
    uart2_putc(hdr[3u]);
    uart2_putc(hdr[4u]);
    for (i = 0u; i < len; i++)
        uart2_putc(payload[i]);
    uart2_putc(crc);
}

uint32_t proto_drops(void)
{
    return s_frame_drops + uart2_tx_drops();
}

/* ==================================================================
 * receive state machine
 * ==================================================================
 */
typedef enum
{
    RX_SYNC0,
    RX_SYNC1,
    RX_TYPE,
    RX_LEN_LO,
    RX_LEN_HI,
    RX_PAYLOAD,
    RX_CRC
} rx_state_t;

static rx_state_t s_state;
static uint8_t  s_type;
static uint16_t s_len;
static uint16_t s_idx;
static uint8_t  s_crc;
static uint8_t  s_buf[PROTO_MAX_PAYLOAD];

void proto_init(void)
{
    s_state = RX_SYNC0;
    s_len   = 0u;
    s_idx   = 0u;
}

uint8_t proto_recv(uint8_t *type, uint8_t *buf, uint16_t maxlen,
                   uint16_t *len_out)
{
    uint8_t byte;

    while (uart2_getc(&byte) != 0)
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
                ; /* could be start of a new frame, stay */
            else
                s_state = RX_SYNC0;
            break;

        case RX_TYPE:
            s_type = byte;
            s_crc  = crc8_update(0x00u, byte);
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
                    s_state = RX_SYNC0;      /* too big: resync */
                    break;
                }
                s_idx = 0u;
                s_state = (full == 0u) ? RX_CRC : RX_PAYLOAD;
            }
            break;

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
                if (copy > 0u)
                    memcpy(buf, s_buf, copy);
                *type    = s_type;
                *len_out = s_len;
                return 1u;                  /* frame complete */
            }
            /* CRC mismatch: discard, keep scanning */
            break;
        }
    }
    return 0u;
}
