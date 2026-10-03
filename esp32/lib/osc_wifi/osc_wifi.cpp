/*
 * osc_wifi.cpp - WiFi credentials store (built-in defaults + NVS override)
 *
 * Priority on boot:
 *   1. credentials written from the web page  (NVS namespace "scope")
 *   2. the two built-in default arrays below
 *   3. if neither connects -> the device becomes a setup AP so the page is
 *      still reachable and the credentials can be fixed
 *
 * The password is never sent back to the browser; only "is there one".
 */
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>
#include "osc_wifi.h"

/* ==================================================================
 * Factory defaults — the two credential arrays.
 *
 * They ship EMPTY on purpose: with no built-in credentials the board goes
 * straight to the "Oscilloscope-Setup" access point, where you enter your
 * WiFi once from the web page.  That value is stored in NVS and survives
 * re-flashing, so you never have to compile a password into the firmware.
 *
 * To bake a fixed network into your own build, put it here instead; a
 * value saved from the web page still overrides it.
 * ================================================================== */
static const char WIFI_SSID_DEFAULT[] = "";
static const char WIFI_PASS_DEFAULT[] = "";

/* Setup AP, used only when nothing else connects */
static const char WIFI_AP_SSID[] = "Oscilloscope-Setup";
static const char WIFI_AP_PASS[] = "12345678";

#define NVS_NS           "scope"
#define WIFI_TIMEOUT_MS  15000u

static char    s_ssid[33];      /* 802.11 SSID is at most 32 bytes + NUL */
static char    s_pass[65];      /* WPA2 passphrase is at most 63 chars + NUL */
static uint8_t s_custom  = 0u;  /* 1 = s_ssid/s_pass came from NVS          */
static uint8_t s_ap_mode = 0u;  /* 1 = running the setup AP                 */

static void copy_str(char *dst, size_t n, const char *src)
{
    if (n == 0u) return;
    strncpy(dst, (src != NULL) ? src : "", n - 1u);
    dst[n - 1u] = '\0';
}

/* wait for the STA link, printing a dot every 250 ms */
static bool wifi_wait(uint32_t timeout_ms)
{
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (uint32_t)(millis() - t0) < timeout_ms)
    {
        delay(250u);
        Serial.print(".");
    }
    return WiFi.status() == WL_CONNECTED;
}

/* load NVS values into s_ssid/s_pass, or the defaults when NVS is empty */
static void wifi_load(void)
{
    Preferences prefs;
    bool ok = prefs.begin(NVS_NS, true);   /* read-only */

    s_custom = (ok && prefs.isKey("ssid")) ? 1u : 0u;

    if (s_custom != 0u)
    {
        String a = prefs.getString("ssid", "");
        String b = prefs.getString("pass", "");
        copy_str(s_ssid, sizeof(s_ssid), a.c_str());
        copy_str(s_pass, sizeof(s_pass), b.c_str());
        if (s_ssid[0] == '\0') s_custom = 0u;   /* paranoia: empty entry */
    }
    if (s_custom == 0u)
    {
        copy_str(s_ssid, sizeof(s_ssid), WIFI_SSID_DEFAULT);
        copy_str(s_pass, sizeof(s_pass), WIFI_PASS_DEFAULT);
    }

    if (ok) prefs.end();
}

static void wifi_report(void)
{
    Serial.printf("\n[WIFI] connected to \"%s\"%s\n", s_ssid,
                  (s_custom != 0u) ? " (saved from web page)" : " (built-in default)");
    Serial.printf("[WIFI] IP %s (RSSI %d dBm)\n", osc_wifi_ip(), WiFi.RSSI());
    Serial.printf("[WIFI] open http://%s/ on your phone/laptop\n", osc_wifi_ip());
}

uint8_t osc_wifi_init(void)
{
    wifi_load();

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    /* With no credentials at all (factory state: NVS empty and the built-in
       defaults left blank) there is nothing to try — skip the STA attempt,
       which would only burn the full timeout, and fall through to the AP. */
    if (s_ssid[0] != '\0')
    {
        WiFi.begin(s_ssid, s_pass);
        Serial.printf("[WIFI] connecting to \"%s\"%s ...\n", s_ssid,
                      (s_custom != 0u) ? " (saved from web page)" : " (built-in default)");

        if (wifi_wait(WIFI_TIMEOUT_MS))
        {
            wifi_report();
            return 1u;
        }

        /* the saved entry may be wrong / the router may be gone: try the
           compile-time defaults once before giving up on STA */
        if ((s_custom != 0u) && (WIFI_SSID_DEFAULT[0] != '\0') &&
            ((strcmp(s_ssid, WIFI_SSID_DEFAULT) != 0) ||
             (strcmp(s_pass, WIFI_PASS_DEFAULT) != 0)))
        {
            Serial.println("\n[WIFI] saved credentials failed — trying built-in defaults");
            WiFi.disconnect();
            delay(200u);
            WiFi.begin(WIFI_SSID_DEFAULT, WIFI_PASS_DEFAULT);
            if (wifi_wait(WIFI_TIMEOUT_MS))
            {
                wifi_report();
                return 1u;
            }
        }
    }
    else
    {
        Serial.println("[WIFI] no credentials stored");
    }

    /* nothing worked: become a setup AP so the configuration page is still
       reachable (otherwise a wrong password would need a re-flash to fix).
       Do not reboot — the phone joins this hotspot and opens the config page. */
    Serial.println("\n[WIFI] no router — starting setup AP");
    WiFi.disconnect();
    delay(100u);
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS))
    {
        /* extremely unlikely (bad password length / out of memory): keep the
           STA link attempt alive and report, so the fault is visible */
        Serial.println("[WIFI] softAP() FAILED — no configuration page available");
        return 0u;
    }
    s_ap_mode = 1u;
    Serial.printf("[WIFI] AP \"%s\"  password \"%s\"\n", WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.printf("[WIFI] open http://%s/ to enter your WiFi, then reboot\n", osc_wifi_ip());
    return 0u;
}

const char *osc_wifi_ssid(void)
{
    return s_ssid;
}

const char *osc_wifi_ip(void)
{
    static char buf[16];
    IPAddress ip = (s_ap_mode != 0u) ? WiFi.softAPIP() : WiFi.localIP();
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return buf;
}

uint8_t osc_wifi_custom(void)   { return s_custom;  }
uint8_t osc_wifi_has_pass(void) { return (s_pass[0] != '\0') ? 1u : 0u; }
uint8_t osc_wifi_ap_mode(void)  { return s_ap_mode; }

const char *osc_wifi_ap_ssid(void) { return WIFI_AP_SSID; }

uint8_t osc_wifi_save(const char *ssid, const char *pass, const char **err)
{
    size_t n;
    const char *keep;

    if (ssid == NULL) { if (err) *err = "SSID 不能为空"; return 0u; }
    n = strlen(ssid);
    if (n == 0u)      { if (err) *err = "SSID 不能为空"; return 0u; }
    if (n > 32u)      { if (err) *err = "SSID 最长 32 字节"; return 0u; }

    if ((pass != NULL) && (pass[0] != '\0'))
    {
        n = strlen(pass);
        if ((n < 8u) || (n > 63u)) { if (err) *err = "WiFi 密码需 8~63 位"; return 0u; }
        keep = pass;
    }
    else
    {
        keep = s_pass;    /* NULL or empty -> keep the current password */
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NS, false)) { if (err) *err = "NVS 打开失败"; return 0u; }
    prefs.putString("ssid", ssid);
    prefs.putString("pass", keep);
    prefs.end();

    copy_str(s_ssid, sizeof(s_ssid), ssid);
    copy_str(s_pass, sizeof(s_pass), keep);
    s_custom = 1u;

    Serial.printf("[WIFI] new credentials saved: \"%s\" (password %s) — reboot to apply\n",
                  s_ssid, (s_pass[0] != '\0') ? "set" : "none");
    return 1u;
}

void osc_wifi_clear(void)
{
    Preferences prefs;
    if (prefs.begin(NVS_NS, false))
    {
        prefs.clear();
        prefs.end();
    }
    s_custom = 0u;
    copy_str(s_ssid, sizeof(s_ssid), WIFI_SSID_DEFAULT);
    copy_str(s_pass, sizeof(s_pass), WIFI_PASS_DEFAULT);
    Serial.println("[WIFI] saved credentials cleared — back to built-in defaults");
}
