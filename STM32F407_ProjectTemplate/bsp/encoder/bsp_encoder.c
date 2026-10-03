/*
 * bsp_encoder.c - rotary encoder via TIM8 hardware quadrature (PC6/PC7)
 *                 push switch on PC8
 *
 * Hardware: TIM8 is set to Encoder Mode TI12 (counts on both TI1 and TI2
 * edges = 4x quadrature).  The counter increments/decrements automatically
 * with zero CPU load.  We just read TIM8->CNT and diff it.
 *
 *   PC6 = A phase  (TIM8_CH1, AF3)
 *   PC7 = B phase  (TIM8_CH2, AF3)
 *   PC8 = push switch (GPIO input, internal pull-up, active LOW)
 *
 * TIM8 is an advanced-control timer on APB2; for encoder mode no output-
 * compare / MOE setup is needed - the counter simply counts input edges.
 */
#include "bsp_encoder.h"
#include "board.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_tim.h"

#define ENC_TIMER        TIM8
#define ENC_GPIO_PORT     GPIOC
#define ENC_A_PIN         GPIO_Pin_6      /* TIM8_CH1 */
#define ENC_B_PIN         GPIO_Pin_7      /* TIM8_CH2 */
#define ENC_SW_PIN        GPIO_Pin_8

static uint16_t s_last = 0;

void encoder_init(void)
{
    GPIO_InitTypeDef gi;
    TIM_TimeBaseInitTypeDef tb;

    /* clocks: GPIOC + TIM8 (APB2) */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM8, ENABLE);

    /* PC6/PC7 -> AF3 (TIM8 CH1/CH2), push-pull, pull-up */
    gi.GPIO_Pin   = ENC_A_PIN | ENC_B_PIN;
    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(ENC_GPIO_PORT, &gi);
    GPIO_PinAFConfig(ENC_GPIO_PORT, GPIO_PinSource6, GPIO_AF_TIM8);
    GPIO_PinAFConfig(ENC_GPIO_PORT, GPIO_PinSource7, GPIO_AF_TIM8);

    /* PC8 -> plain input, internal pull-up (press = LOW) */
    gi.GPIO_Pin   = ENC_SW_PIN;
    gi.GPIO_Mode  = GPIO_Mode_IN;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(ENC_GPIO_PORT, &gi);

    /* time-base: full 16-bit range, no prescaler */
    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Period        = 0xFFFF;
    tb.TIM_Prescaler     = 0;
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(ENC_TIMER, &tb);

    /* encoder mode TI12 = count on both TI1 & TI2 edges (4x) */
    TIM_EncoderInterfaceConfig(ENC_TIMER,
                               TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising,
                               TIM_ICPolarity_Rising);

    TIM_SetCounter(ENC_TIMER, 0);
    TIM_Cmd(ENC_TIMER, ENABLE);

    s_last = 0;
}

int16_t encoder_value(void)
{
    return (int16_t)TIM_GetCounter(ENC_TIMER);
}

int16_t encoder_delta(void)
{
    uint16_t now = TIM_GetCounter(ENC_TIMER);
    int16_t  d   = (int16_t)(uint16_t)(now - s_last);   /* wraps correctly */
    s_last = now;
    return d;
}

uint8_t encoder_button(void)
{
    return (GPIO_ReadInputDataBit(ENC_GPIO_PORT, ENC_SW_PIN) == Bit_RESET) ? 1u : 0u;
}
