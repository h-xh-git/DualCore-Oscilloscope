#ifndef __BSP_BUZZER_H
#define __BSP_BUZZER_H
#include <stdint.h>

/*
 * bsp_buzzer.h - ACTIVE buzzer on PB8, LOW-level trigger.
 *
 * Hardware in use: an ACTIVE buzzer (it has its own oscillator, so it only
 * needs a DC level) wired as a low-side switch - it sounds while PB8 is pulled
 * LOW and stays silent while PB8 is HIGH.
 *
 *   idle (silent) = PB8 HIGH
 *   beep (sound)  = PB8 LOW
 *
 * BUZZER_ACTIVE_LOW selects that polarity; set it to 0 if you ever swap in a
 * buzzer that sounds on a HIGH level.  No software tone/frequency is generated
 * - for an active buzzer the level alone does it.
 *
 * NOTE: buzzer_beep() blocks for the whole beep (a plain delay_ms).  That is
 * fine for UI clicks; a future revision could drive PB8 from a timer if
 * non-blocking beeps are ever needed.
 *
 * Wiring: buzzer - (or the module's trigger input) -> PB8, buzzer + -> 3.3V/5V,
 * and the module's GND tied to the board GND.
 * If the sound is weak, the GPIO is being asked to sink more than it should:
 * drive it through an NPN transistor (PB8 -> 1k -> base, buzzer between VCC
 * and collector, emitter GND) and flip BUZZER_ACTIVE_LOW back to 0.
 */

#define BUZZER_PORT         GPIOB
#define BUZZER_PIN          GPIO_Pin_8
#define BUZZER_ACTIVE_LOW   1

void buzzer_init(void);
void buzzer_on(void);                   /* start sounding */
void buzzer_off(void);                  /* silence */
void buzzer_beep(uint16_t ms);          /* blocking beep, then silence */

#endif /* __BSP_BUZZER_H */
