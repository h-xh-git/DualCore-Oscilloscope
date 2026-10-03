/*
 * osc_link - UART link to STM32 (HardwareSerial1 wrapper)
 */
#include "osc_link.h"

static HardwareSerial *s_link = nullptr;

void osc_link_init(uint32_t baud, int rxPin, int txPin)
{
    /* A RESP_WAVE frame is 1205 bytes and arrives in ~26 ms at 460800 baud,
     * which overflows the default 256-byte RX ring if the loop is busy
     * printing.  2048 B holds a whole frame.  Must precede begin(). */
    Serial1.setRxBufferSize(2048);
    Serial1.begin(baud, SERIAL_8N1, rxPin, txPin);
    s_link = &Serial1;
}

size_t osc_link_available(void)
{
    return s_link ? (size_t)s_link->available() : 0;
}

size_t osc_link_read(uint8_t *buf, size_t maxlen)
{
    if (!s_link) return 0;
    size_t n = 0;
    while (n < maxlen && s_link->available())
    {
        buf[n++] = (uint8_t)s_link->read();
    }
    return n;
}

void osc_link_write(const uint8_t *buf, size_t len)
{
    if (s_link) s_link->write(buf, len);
}

void osc_link_print(const char *s)
{
    if (s_link) s_link->print(s);
}
