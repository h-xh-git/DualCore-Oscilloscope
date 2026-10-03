/*
 * bsp_uart2 - USART2 link to ESP32 (PA2=TX, PA3=RX, AF7)
 * Standard Peripheral Driver based. Library files are NOT modified.
 */
#include "bsp_uart2.h"

#define UART2_RX_BUF_SIZE 256u
#define UART2_TX_BUF_SIZE 2048u   /* must hold one 1206 B proto frame + slack */

static volatile uint8_t  s_rxbuf[UART2_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0;   /* write index (ISR) */
static volatile uint16_t s_rx_tail = 0;   /* read index (main) */

static volatile uint8_t  s_txbuf[UART2_TX_BUF_SIZE];
static volatile uint16_t s_tx_head = 0;   /* read index  (ISR)  */
static volatile uint16_t s_tx_tail = 0;   /* write index (main) */
static volatile uint32_t s_tx_drops = 0;

void uart2_init(uint32_t baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* clocks: GPIOA (AHB1) + USART2 (APB1) */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* PA2(TX) PA3(RX) -> AF7 = USART2 */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);

    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;       /* TX */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3;       /* RX */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* USART config: 8N1, no flow control */
    USART_StructInit(&USART_InitStructure);
    USART_InitStructure.USART_BaudRate            = baud;
    USART_InitStructure.USART_WordLength           = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity               = USART_Parity_No;
    USART_InitStructure.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStructure.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    USART_Init(USART2, &USART_InitStructure);

    USART_ClearFlag(USART2, USART_FLAG_RXNE);
    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_tx_head = 0u;
    s_tx_tail = 0u;
    s_tx_drops = 0u;
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);

    /* NVIC: USART2 IRQ (lower priority than USART1 debug) */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority  = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority         = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                  = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/* Queue one byte for transmission.  Never blocks: the TXE interrupt drains
 * the ring while the main loop keeps running (a 1205 B wave frame would
 * otherwise stall the UI for ~26 ms at 460800 baud). */
void uart2_putc(uint8_t c)
{
    uint16_t next;
    uint32_t prim;

    next = (uint16_t)((s_tx_tail + 1u) % UART2_TX_BUF_SIZE);
    if (next == s_tx_head)
    {
        s_tx_drops++;                    /* ring full: drop, never spin */
        return;
    }

    /* The ISR may disable TXEIE between the store and the enable, so the
     * store + enable must be atomic with respect to it. */
    prim = __get_PRIMASK();
    __disable_irq();
    s_txbuf[s_tx_tail] = c;
    s_tx_tail = next;
    USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
    if (prim == 0u) __enable_irq();
}

void uart2_puts(const char *s)
{
    while (*s) uart2_putc((uint8_t)*s++);
}

uint16_t uart2_tx_free(void)
{
    int32_t used = (int32_t)s_tx_tail - (int32_t)s_tx_head;
    if (used < 0) used += (int32_t)UART2_TX_BUF_SIZE;
    return (uint16_t)((int32_t)UART2_TX_BUF_SIZE - 1 - used);
}

uint32_t uart2_tx_drops(void)
{
    return s_tx_drops;
}

void uart2_tx_flush(void)
{
    while (s_tx_head != s_tx_tail) { }
}

int uart2_getc(uint8_t *out_c)
{
    if (s_rx_head == s_rx_tail) return 0;            /* empty */
    *out_c = s_rxbuf[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) % UART2_RX_BUF_SIZE);
    return 1;
}

uint16_t uart2_available(void)
{
    int32_t n = (int32_t)s_rx_head - (int32_t)s_rx_tail;
    if (n < 0) n += (int32_t)UART2_RX_BUF_SIZE;
    return (uint16_t)n;
}

void USART2_IRQHandler(void)
{
    /* RX not empty: read DR clears the flag */
    if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
    {
        uint8_t  b   = (uint8_t)(USART2->DR & 0xFFu);
        uint16_t next = (uint16_t)((s_rx_head + 1u) % UART2_RX_BUF_SIZE);
        if (next != s_rx_tail)                       /* drop on overflow */
        {
            s_rxbuf[s_rx_head] = b;
            s_rx_head = next;
        }
    }
    /* overrun: clear by reading SR then DR */
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
    {
        (void)USART2->SR;
        (void)USART2->DR;
    }
    /* TX empty: feed the next queued byte (TXEIE is checked too, so this
     * only runs while the TX path is actually armed) */
    if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET)
    {
        if (s_tx_head != s_tx_tail)
        {
            USART_SendData(USART2, s_txbuf[s_tx_head]);
            s_tx_head = (uint16_t)((s_tx_head + 1u) % UART2_TX_BUF_SIZE);
        }
        else
        {
            USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
        }
    }
}
