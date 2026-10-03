#include "bsp_buzzer.h"
#include "board.h"

void buzzer_init(void)
{
    GPIO_InitTypeDef gi;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    /* Pre-load the idle (silent) level into the output register BEFORE the pin
     * is switched to output mode, so no glitch beep is heard during init. */
#if BUZZER_ACTIVE_LOW
    GPIO_SetBits(BUZZER_PORT, BUZZER_PIN);      /* HIGH = silent */
#else
    GPIO_ResetBits(BUZZER_PORT, BUZZER_PIN);    /* LOW  = silent */
#endif

    gi.GPIO_Pin   = BUZZER_PIN;
    gi.GPIO_Mode  = GPIO_Mode_OUT;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(BUZZER_PORT, &gi);

    buzzer_off();
}

void buzzer_on(void)
{
#if BUZZER_ACTIVE_LOW
    GPIO_ResetBits(BUZZER_PORT, BUZZER_PIN);    /* LOW  = sound */
#else
    GPIO_SetBits(BUZZER_PORT, BUZZER_PIN);
#endif
}

void buzzer_off(void)
{
#if BUZZER_ACTIVE_LOW
    GPIO_SetBits(BUZZER_PORT, BUZZER_PIN);      /* HIGH = silent */
#else
    GPIO_ResetBits(BUZZER_PORT, BUZZER_PIN);
#endif
}

void buzzer_beep(uint16_t ms)
{
    buzzer_on();
    delay_ms(ms);
    buzzer_off();
}
