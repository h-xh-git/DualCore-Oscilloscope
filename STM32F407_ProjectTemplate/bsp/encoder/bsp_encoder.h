#ifndef __BSP_ENCODER_H
#define __BSP_ENCODER_H
#include <stdint.h>

/*
 * bsp_encoder.h - rotary encoder via TIM8 hardware quadrature on PC6/PC7,
 * push switch on PC8.
 *
 *   PC6 = A phase  (TIM8_CH1, AF3)
 *   PC7 = B phase  (TIM8_CH2, AF3)
 *   PC8 = push switch (plain GPIO input, internal pull-up; press = LOW)
 *
 * TIM8 is set to Encoder Mode TI12 (4x quadrature): the hardware counter
 * increments/decrements on every A/B edge with no CPU involvement.  A typical
 * 24-detent encoder therefore produces ~4 counts per detent.
 *
 *   encoder_delta()  - signed counts accumulated since the last call
 *   encoder_value()  - current raw 16-bit counter (signed)
 *   encoder_button() - 1 while the knob is pushed down
 *
 * If the rotation direction is reversed, either swap the A/B wires or flip
 * one of the TIM_ICPolarity arguments in bsp_encoder.c.
 */

void     encoder_init(void);
int16_t  encoder_delta(void);          /* signed counts since last call    */
int16_t  encoder_value(void);          /* current raw counter (signed)     */
uint8_t  encoder_button(void);         /* 1 while pushed                    */

#endif /* __BSP_ENCODER_H */
