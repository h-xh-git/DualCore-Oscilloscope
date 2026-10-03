#include "bsp_key.h"
#include "board.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} key_pin_t;

static const key_pin_t key_pins[4] =
{
    { GPIOC, GPIO_Pin_0 },      /* KEY1 */
    { GPIOC, GPIO_Pin_1 },      /* KEY2 */
    { GPIOC, GPIO_Pin_2 },      /* KEY3 */
    { GPIOC, GPIO_Pin_3 },      /* KEY4 */
};

/* Debounced state: bit i set = key (i+1) is currently held down. */
static uint8_t key_last = 0;

uint8_t key_raw(void)
{
    uint8_t v = 0;
    uint8_t i;

    for (i = 0; i < 4; i++)
        if (GPIO_ReadInputDataBit(key_pins[i].port, key_pins[i].pin) == Bit_RESET)
            v |= (uint8_t)(1u << i);

    return v;
}

void key_init(void)
{
    GPIO_InitTypeDef gi;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

    gi.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
    gi.GPIO_Mode  = GPIO_Mode_IN;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_2MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &gi);

    delay_ms(2);
    key_last = key_raw();       /* a key already held at boot is not an event */
}

uint8_t key_scan(void)
{
    uint8_t raw = key_raw();
    uint8_t pressed;
    uint8_t i;

    if (raw == key_last)
        return KEY_NONE;

    delay_ms(15);               /* debounce, only when something moved */
    raw = key_raw();
    if (raw == key_last)
        return KEY_NONE;

    pressed  = (uint8_t)(raw & (uint8_t)(~key_last));   /* bits that just went down */
    key_last = raw;

    for (i = 0; i < 4; i++)
        if (pressed & (uint8_t)(1u << i))
            return (uint8_t)(i + 1);

    return KEY_NONE;
}

uint8_t key_down(uint8_t key)
{
    if (key < KEY_1 || key > KEY_4)
        return 0;

    return (key_last & (uint8_t)(1u << (key - 1))) ? 1 : 0;
}
