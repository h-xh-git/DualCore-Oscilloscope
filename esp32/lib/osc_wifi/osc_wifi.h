/*
 * osc_wifi.h - WiFi credentials store for the ESP32 connectivity core
 *
 * Two compile-time arrays in osc_wifi.cpp hold the factory defaults; anything
 * saved from the web page is written to NVS and wins over them on the next
 * boot, so changing networks no longer needs a re-flash.
 */
#ifndef OSC_WIFI_H
#define OSC_WIFI_H

#include <stdint.h>

/* Connect using the credentials saved in NVS, falling back to the built-in
 * defaults.  If neither connects, bring up the setup AP instead so the page
 * stays reachable and the credentials can be corrected.
 * Returns 1 when connected to a router, 0 when running the setup AP. */
uint8_t osc_wifi_init(void);

/* SSID currently in use (never NULL). */
const char *osc_wifi_ssid(void);

/* Dotted-quad IP of the active interface (STA IP or setup-AP IP). */
const char *osc_wifi_ip(void);

uint8_t osc_wifi_custom(void);    /* 1 = credentials came from the web page (NVS) */
uint8_t osc_wifi_has_pass(void);  /* 1 = a password is set (the password is never returned) */
uint8_t osc_wifi_ap_mode(void);   /* 1 = running the setup AP, not connected to a router */

/* SSID of the fallback setup AP (the page shows it so the phone knows which
 * hotspot to join).  Meaningful only while osc_wifi_ap_mode() == 1. */
const char *osc_wifi_ap_ssid(void);

/* Save new credentials to NVS.  pass == NULL keeps the current password.
 * Returns 1 on success; on failure returns 0 and, when err != NULL, sets
 * *err to a static Chinese message. */
uint8_t osc_wifi_save(const char *ssid, const char *pass, const char **err);

/* Forget the saved credentials and go back to the built-in defaults. */
void osc_wifi_clear(void);

#endif /* OSC_WIFI_H */
