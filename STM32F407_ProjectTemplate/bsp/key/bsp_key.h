#ifndef __BSP_KEY_H
#define __BSP_KEY_H
#include <stdint.h>

/*
 * bsp_key.h - 4 user keys on PC0..PC3.
 *
 * Pins are inputs with the INTERNAL PULL-UP enabled, so each key only has to
 * short the pin to GND when pressed (no external resistor needed):
 *
 *   PC0 = KEY1, PC1 = KEY2, PC2 = KEY3, PC3 = KEY4
 *
 * key_scan() is non-blocking: it returns the key that was just pressed
 * (KEY_1..KEY_4) exactly ONCE per press and KEY_NONE otherwise.  A 15 ms
 * debounce delay is only spent when the raw inputs actually change.
 */

#define KEY_NONE   0
#define KEY_1      1
#define KEY_2      2
#define KEY_3      3
#define KEY_4      4

void    key_init(void);
uint8_t key_scan(void);              /* KEY_1..KEY_4 on a fresh press, else KEY_NONE */
uint8_t key_down(uint8_t key);       /* 1 while that key is held down */
uint8_t key_raw(void);               /* bit i set = key (i+1) is down (no debounce) */

#endif /* __BSP_KEY_H */
