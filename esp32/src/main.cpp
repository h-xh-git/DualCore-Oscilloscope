/* ============================================================
 * WiFi Web Scope + OTA (ESP32 connectivity core)
 * ------------------------------------------------------------
 * The ESP32 becomes a self-contained web oscilloscope:
 *
 *   1. Connects to external WiFi (STA) using the credentials in osc_wifi.cpp,
 *      overridable from the web page (saved to NVS, applied on next boot)
 *      Falls back to a setup AP "Oscilloscope-Setup" if nothing connects.
 *   2. Serves an embedded HTML scope page at http://<dhcp-ip>/
 *   3. Polls the STM32 for WAVE (10 Hz), STAT + MEAS (2 Hz)
 *   4. Caches the latest frames for web clients
 *   5. Forwards web commands (Run/Stop/Rate/Trigger) to the STM32
 *   6. CSV export endpoint
 *   7. ArduinoOTA firmware update
 *
 * Wiring:
 *   ESP32 GPIO17 (RX) <- STM32 PA2 (USART2 TX)
 *   ESP32 GPIO18 (TX) -> STM32 PA3 (USART2 RX)
 *   GND <-> GND
 *
 * Usage:
 *   1. Flash this firmware via COMx (CH340)
 *   2. Open Serial Monitor (115200) to read the DHCP-assigned IP
 *   3. On phone/laptop (same WiFi): open browser to that IP
 *   4. OTA: use Arduino IDE / PlatformIO "Upload OTA" to that IP
 * ============================================================ */

#include <Arduino.h>
#include <WiFi.h>
#include "osc_link.h"
#include "osc_proto.h"
#include "osc_wifi.h"
#include "osc_web.h"

#define LINK_RX_PIN   17    /* <- STM32 PA2 (USART2 TX) */
#define LINK_TX_PIN   18    /* -> STM32 PA3 (USART2 RX) */
#define LINK_BAUD     460800u

/* poll timers */
static uint32_t t_wave = 0;
static uint32_t t_stat = 0;
static uint32_t t_meas = 0;

void setup()
{
    Serial.begin(115200);
    delay(200);

    /* UART link to STM32 */
    osc_link_init(LINK_BAUD, LINK_RX_PIN, LINK_TX_PIN);
    osc_proto_init();

    /* WiFi STA (credentials from osc_wifi.cpp / NVS, set via the web page).
       Must run before osc_web_init(): the HTTP server and mDNS need a link. */
    osc_wifi_init();

    /* WebServer + OTA */
    osc_web_init();

    t_wave = t_stat = t_meas = millis();
}

void loop()
{
    uint32_t now = millis();

    /* ---- 1. receive + cache frames from STM32 ---- */
    static uint8_t  buf[PROTO_MAX_PAYLOAD];   /* 2048 B: keep off the stack */
    uint8_t          type;
    uint16_t         len;

    while (osc_proto_recv(&type, buf, sizeof(buf), &len) != 0u)
    {
        switch (type)
        {
        case PROTO_RESP_STAT:
            osc_web_cache_stat(buf, len);
            break;
        case PROTO_RESP_MEAS:
            osc_web_cache_meas(buf, len);
            break;
        case PROTO_RESP_WAVE:
            osc_web_cache_wave(buf, len);
            break;
        default:
            break;
        }
    }

    /* ---- 2. request data from STM32 ---- */
    if ((uint32_t)(now - t_wave) >= 100u)     /* 10 Hz wave for live display */
    {
        t_wave = now;
        osc_proto_send(PROTO_CMD_REQ_WAVE, NULL, 0u);
    }
    if ((uint32_t)(now - t_stat) >= 500u)     /* 2 Hz stat */
    {
        t_stat = now;
        osc_proto_send(PROTO_CMD_REQ_STAT, NULL, 0u);
    }
    if ((uint32_t)(now - t_meas) >= 500u)    /* 2 Hz meas */
    {
        t_meas = now;
        osc_proto_send(PROTO_CMD_REQ_MEAS, NULL, 0u);
    }

    /* ---- 3. web server + OTA ---- */
    osc_web_poll();

    delay(1u);   /* yield to WiFi stack */
}
