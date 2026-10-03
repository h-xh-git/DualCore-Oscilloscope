/*
 * bsp_adc.c - Dual-channel ADC acquisition engine
 *
 * Hardware:
 *   ADC1 (master) + ADC2 (slave), dual regular-simultaneous mode
 *   Trigger: TIM2 update -> TRGO -> ADC external trigger
 *   DMA: DMA2 Stream0 Channel0, peripheral=ADC->CDR (32-bit), circular
 *
 *   CH1 = PA1 = ADC1_IN1   (master, data in CDR[15:0])
 *   CH2 = PA0 = ADC2_IN0   (slave,  data in CDR[31:16])
 *
 * Double-buffer: adc_buf[512] -> HT at 256 (first half), TC at 512 (second half).
 */
#include "board.h"
#include "bsp_adc.h"

/* ---- hardware mapping ---- */
#define ADC_GPIO_PORT      GPIOA
#define ADC_GPIO_CLK       RCC_AHB1Periph_GPIOA
#define ADC_CH1_PIN        GPIO_Pin_1     /* PA1 */
#define ADC_CH2_PIN        GPIO_Pin_0     /* PA0 */
#define ADC_CH1_ADC_CHANNEL  ADC_Channel_1
#define ADC_CH2_ADC_CHANNEL  ADC_Channel_0
#define ADC_SAMPLE_TIME    ADC_SampleTime_15Cycles

#define ADC_DMA_STREAM      DMA2_Stream0
#define ADC_DMA_CHANNEL     DMA_Channel_0
#define ADC_DMA_CLK          RCC_AHB1Periph_DMA2
#define ADC_DMA_IRQn         DMA2_Stream0_IRQn
#define ADC_DMA_IRQHandler   DMA2_Stream0_IRQHandler

#define ADC_TIM             TIM2
#define ADC_TIM_CLK         RCC_APB1Periph_TIM2
#define ADC_TIM_TRGO_SRC    TIM_TRGOSource_Update

/* TIM2 is on APB1: PCLK1 = 42 MHz, timer clock = 2x = 84 MHz */
#define TIM2_CLK_HZ         84000000UL

/* ---- state ---- */
volatile uint32_t adc_buf[ADC_BUF_SIZE];          /* DMA circular buffer */
static volatile uint8_t  s_half_ready = 0;       /* bitmask: bit0=HT(first), bit1=TC(second) */
static volatile uint8_t  s_last_half = 0;        /* 0 or 1: which half to read */
static volatile uint32_t s_half_cnt = 0;         /* halves completed (gap detection) */
static uint32_t s_sample_rate = 0;

/* forward */
void ADC_DMA_IRQHandler(void);

/* =========================================================
 * TIM2: periodic update -> TRGO -> triggers ADC
 *
 * rate = TIM2_CLK_HZ / ((psc + 1) * (arr + 1)) and arr is only 16 bits.
 * With the prescaler pinned at 0 the slowest reachable rate was
 * 84 MHz / 65536 = 1281 Hz, so 100 / 200 / 500 / 1000 S/s all silently ran
 * at ~1281 Hz while adc_get_rate() still reported the requested value
 * (wrong time/div on screen and wrong frequency in the measurements).
 * Pick a prescaler instead.
 * ========================================================= */
static void adc_tim_div(uint32_t sample_rate_hz, uint16_t *psc_out, uint16_t *arr_out)
{
    uint32_t psc = 0u;
    uint32_t ticks;

    if (sample_rate_hz == 0u) sample_rate_hz = 1u;

    ticks = TIM2_CLK_HZ / sample_rate_hz;

    /* the loop only runs while (psc + 1) * rate is still below 1281, so the
       multiply below can never overflow a uint32_t */
    while ((ticks > 0x10000u) && (psc < 0xFFFFu))
    {
        psc++;
        ticks = TIM2_CLK_HZ / ((psc + 1u) * sample_rate_hz);
    }

    if (ticks == 0u) ticks = 1u;
    if (ticks > 0x10000u) ticks = 0x10000u;    /* 16-bit counter */

    *psc_out = (uint16_t)psc;
    *arr_out = (uint16_t)(ticks - 1u);
}

static void adc_tim_init(uint32_t sample_rate_hz)
{
    TIM_TimeBaseInitTypeDef tim;
    uint16_t psc;
    uint16_t arr;

    RCC_APB1PeriphClockCmd(ADC_TIM_CLK, ENABLE);

    adc_tim_div(sample_rate_hz, &psc, &arr);

    tim.TIM_Prescaler         = psc;
    tim.TIM_Period            = arr;
    tim.TIM_ClockDivision     = TIM_CKD_DIV1;
    tim.TIM_CounterMode       = TIM_CounterMode_Up;
    tim.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(ADC_TIM, &tim);

    /* Generate TRGO on update event -> triggers ADC */
    TIM_SelectOutputTrigger(ADC_TIM, ADC_TIM_TRGO_SRC);
    TIM_Cmd(ADC_TIM, DISABLE);               /* leave stopped; adc_start() enables */
}

/* =========================================================
 * GPIO: PA0 + PA1 as analog inputs
 * ========================================================= */
static void adc_gpio_init(void)
{
    GPIO_InitTypeDef gi;
    RCC_AHB1PeriphClockCmd(ADC_GPIO_CLK, ENABLE);

    gi.GPIO_Pin  = ADC_CH1_PIN | ADC_CH2_PIN;
    gi.GPIO_Mode = GPIO_Mode_AN;
    gi.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(ADC_GPIO_PORT, &gi);
}

/* =========================================================
 * DMA2 Stream0 Ch0: ADC->CDR -> adc_buf, circular, 32-bit
 * ========================================================= */
static void adc_dma_init(void)
{
    DMA_InitTypeDef dma;
    NVIC_InitTypeDef nvic;

    RCC_AHB1PeriphClockCmd(ADC_DMA_CLK, ENABLE);

    DMA_DeInit(ADC_DMA_STREAM);
    while (DMA_GetCmdStatus(ADC_DMA_STREAM) != DISABLE) { }

    dma.DMA_Channel            = ADC_DMA_CHANNEL;
    dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC->CDR;
    dma.DMA_Memory0BaseAddr    = (uint32_t)adc_buf;
    dma.DMA_DIR                = DMA_DIR_PeripheralToMemory;
    dma.DMA_BufferSize         = ADC_BUF_SIZE;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Word;
    dma.DMA_Mode               = DMA_Mode_Circular;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    dma.DMA_FIFOThreshold      = DMA_FIFOThreshold_HalfFull;
    dma.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    dma.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(ADC_DMA_STREAM, &dma);

    /* Half-transfer + transfer-complete interrupts */
    DMA_ITConfig(ADC_DMA_STREAM, DMA_IT_TC | DMA_IT_HT, ENABLE);
    DMA_ClearITPendingBit(ADC_DMA_STREAM, DMA_IT_HTIF0 | DMA_IT_TCIF0);

    nvic.NVIC_IRQChannel                   = ADC_DMA_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    DMA_Cmd(ADC_DMA_STREAM, ENABLE);
    while (DMA_GetCmdStatus(ADC_DMA_STREAM) == DISABLE) { }
}

/* =========================================================
 * ADC1 + ADC2: dual regular-simultaneous, TIM2-triggered
 * ========================================================= */
static void adc_analog_init(void)
{
    ADC_CommonInitTypeDef cci;
    ADC_InitTypeDef ai;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_ADC2, ENABLE);

    /* Common: dual regular-simultaneous, DMA mode 2, prescaler /2 (42 MHz) */
    cci.ADC_Mode             = ADC_DualMode_RegSimult;
    cci.ADC_Prescaler        = ADC_Prescaler_Div2;
    cci.ADC_DMAAccessMode    = ADC_DMAAccessMode_2;
    cci.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
    ADC_CommonInit(&cci);

    /* ADC1 (master) */
    ai.ADC_Resolution           = ADC_Resolution_12b;
    ai.ADC_ScanConvMode         = DISABLE;
    ai.ADC_ContinuousConvMode   = DISABLE;
    ai.ADC_ExternalTrigConvEdge  = ADC_ExternalTrigConvEdge_Rising;
    ai.ADC_ExternalTrigConv     = ADC_ExternalTrigConv_T2_TRGO;
    ai.ADC_DataAlign            = ADC_DataAlign_Right;
    ai.ADC_NbrOfConversion      = 1;
    ADC_Init(ADC1, &ai);

    /* ADC2 (slave) - same config; in dual mode it follows the master trigger */
    ADC_Init(ADC2, &ai);

    /* Channel mapping: ADC1 rank1 = CH1, ADC2 rank1 = CH2 */
    ADC_RegularChannelConfig(ADC1, ADC_CH1_ADC_CHANNEL, 1, ADC_SAMPLE_TIME);
    ADC_RegularChannelConfig(ADC2, ADC_CH2_ADC_CHANNEL, 1, ADC_SAMPLE_TIME);

    /* DMA request from master (ADC1) */
    ADC_DMACmd(ADC1, ENABLE);
    /* DDS=1: keep issuing DMA requests after last transfer (continuous circular) */
    ADC_DMARequestAfterLastTransferCmd(ADC1, ENABLE);

    /* Enable both ADCs */
    ADC_Cmd(ADC1, ENABLE);
    ADC_Cmd(ADC2, ENABLE);
}

/* =========================================================
 * Public API
 * ========================================================= */
void adc_init(uint32_t sample_rate_hz)
{
    s_sample_rate = sample_rate_hz;
    s_half_ready = 0;
    s_last_half = 0;
    s_half_cnt = 0;

    adc_gpio_init();
    adc_tim_init(sample_rate_hz);
    adc_dma_init();
    adc_analog_init();
}

void adc_start(void)
{
    s_half_ready = 0;
    s_half_cnt = 0;
    TIM_SetCounter(ADC_TIM, 0);
    TIM_Cmd(ADC_TIM, ENABLE);
}

void adc_stop(void)
{
    TIM_Cmd(ADC_TIM, DISABLE);
}

int adc_poll(uint8_t *half_idx_out)
{
    uint8_t h;
    h = s_half_ready;          /* volatile read */
    if (h == 0) return 0;

    /* Report HT (first half, older) BEFORE TC (second half) so that draining
       both flags in one loop yields the samples in chronological order. */
    if (h & 0x01)
    {
        s_half_ready &= ~0x01;
        s_last_half = 0;
        if (half_idx_out) *half_idx_out = 0;
        return 1;
    }
    if (h & 0x02)
    {
        s_half_ready &= ~0x02;
        s_last_half = 1;
        if (half_idx_out) *half_idx_out = 1;
        return 1;
    }
    return 0;
}

uint16_t adc_ch1(uint32_t index)
{
    uint32_t base = (s_last_half == 0) ? 0 : ADC_HALF_SIZE;
    uint32_t i = base + index;
    if (i >= ADC_BUF_SIZE) i = base;
    return (uint16_t)(adc_buf[i] & 0xFFFFu);
}

uint16_t adc_ch2(uint32_t index)
{
    uint32_t base = (s_last_half == 0) ? 0 : ADC_HALF_SIZE;
    uint32_t i = base + index;
    if (i >= ADC_BUF_SIZE) i = base;
    return (uint16_t)(adc_buf[i] >> 16);
}

void adc_set_rate(uint32_t sample_rate_hz)
{
    uint16_t psc;
    uint16_t arr;

    s_sample_rate = sample_rate_hz;
    adc_tim_div(sample_rate_hz, &psc, &arr);

    TIM_PrescalerConfig(ADC_TIM, psc, TIM_PSCReloadMode_Immediate);
    TIM_SetAutoreload(ADC_TIM, arr);
}

uint32_t adc_get_rate(void)
{
    return s_sample_rate;
}

uint32_t adc_dma_remaining(void)
{
    return DMA_GetCurrDataCounter(ADC_DMA_STREAM);
}

uint32_t adc_half_count(void)
{
    return s_half_cnt;
}

/* =========================================================
 * DMA2 Stream0 IRQ: half-transfer + transfer-complete
 * ========================================================= */
void ADC_DMA_IRQHandler(void)
{
    /* HT: first 256 words transferred -> first half ready */
    if (DMA_GetITStatus(ADC_DMA_STREAM, DMA_IT_HTIF0))
    {
        DMA_ClearITPendingBit(ADC_DMA_STREAM, DMA_IT_HTIF0);
        s_half_ready |= 0x01;
        s_half_cnt++;
    }
    /* TC: all 512 words transferred -> second half ready */
    if (DMA_GetITStatus(ADC_DMA_STREAM, DMA_IT_TCIF0))
    {
        DMA_ClearITPendingBit(ADC_DMA_STREAM, DMA_IT_TCIF0);
        s_half_ready |= 0x02;
        s_half_cnt++;
    }
}
