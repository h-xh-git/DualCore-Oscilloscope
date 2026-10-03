/*
 * bsp_lcd.c - ST7789 2.4" TFT driver (hardware SPI1, write-only)
 *
 * Graphics primitives + color test pattern.
 * No font yet (added in a later step); this proves the panel works.
 *
 * Notes:
 *  - SPI1 full-duplex master, 8-bit, mode 0 (CPOL_Low / CPHA_1Edge).
 *    ST7789 samples MOSI on the rising SCK edge in both mode 0 and 3;
 *    mode 0 used here. MISO (PA6) is left free (not configured as AF)
 *    so PA6 stays available for the rotary encoder later.
 *  - Each byte drains RXNE so the shift register never stalls.
 *  - No DMA yet (the waveform renderer adds DMA); partial refresh
 *    is already fast enough at 10.5 MHz.
 */
#include "bsp_lcd.h"
#include "board.h"   /* delay_ms */

/* ===================================================================
 * low-level SPI byte transfer (blocking, drains RX)
 * =================================================================== */
static void lcd_spi_xfer(uint8_t d)
{
    /* wait TXE (tx buffer empty) */
    while (SPI_I2S_GetFlagStatus(LCD_SPI, SPI_I2S_FLAG_TXE) == RESET) { }
    SPI_I2S_SendData(LCD_SPI, d);
    /* wait RXNE (a byte clocked in -> transfer complete) */
    while (SPI_I2S_GetFlagStatus(LCD_SPI, SPI_I2S_FLAG_RXNE) == RESET) { }
    (void)SPI_I2S_ReceiveData(LCD_SPI);
}

/* send a command byte (DC=0) */
static void lcd_cmd(uint8_t c)
{
    LCD_CS_LO();
    LCD_DC_LO();
    lcd_spi_xfer(c);
    LCD_CS_HI();
}

/* send N data bytes (DC=1), CS held low across all of them */
static void lcd_data_buf(const uint8_t *p, uint32_t n)
{
    LCD_CS_LO();
    LCD_DC_HI();
    while (n--)
        lcd_spi_xfer(*p++);
    LCD_CS_HI();
}

static void lcd_data8(uint8_t d)
{
    lcd_data_buf(&d, 1);
}

/* ===================================================================
 * GPIO + SPI init
 * =================================================================== */
static void lcd_gpio_init(void)
{
    GPIO_InitTypeDef gi;

    RCC_AHB1PeriphClockCmd(LCD_GPIO_CLK, ENABLE);

    /* control pins: push-pull output, 50 MHz */
    GPIO_StructInit(&gi);
    gi.GPIO_Mode  = GPIO_Mode_OUT;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    gi.GPIO_Speed = GPIO_Speed_50MHz;

    gi.GPIO_Pin = LCD_CS_PIN;   GPIO_Init(LCD_CS_PORT,  &gi);
    gi.GPIO_Pin = LCD_DC_PIN;   GPIO_Init(LCD_DC_PORT,  &gi);
    gi.GPIO_Pin = LCD_RST_PIN;  GPIO_Init(LCD_RST_PORT, &gi);
    gi.GPIO_Pin = LCD_BLK_PIN;  GPIO_Init(LCD_BLK_PORT, &gi);

    /* SPI pins: AF5 (SPI1_SCK on PA5, SPI1_MOSI on PA7), 50 MHz */
    GPIO_PinAFConfig(LCD_SCK_PORT,  LCD_SCK_PIN_SRC,  GPIO_AF_SPI1);
    GPIO_PinAFConfig(LCD_MOSI_PORT, LCD_MOSI_PIN_SRC, GPIO_AF_SPI1);
    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    gi.GPIO_Pin   = LCD_SCK_PIN;   GPIO_Init(LCD_SCK_PORT,  &gi);
    gi.GPIO_Pin   = LCD_MOSI_PIN;  GPIO_Init(LCD_MOSI_PORT, &gi);

    /* idle states */
    LCD_CS_HI();
    LCD_DC_HI();
    LCD_RST_HI();
    LCD_BLK_OFF();
}

static void lcd_spi_init(void)
{
    SPI_InitTypeDef si;

    RCC_APB2PeriphClockCmd(LCD_SPI_CLK, ENABLE);

    SPI_StructInit(&si);
    si.SPI_Direction           = SPI_Direction_2Lines_FullDuplex;
    si.SPI_Mode                = SPI_Mode_Master;
    si.SPI_DataSize            = SPI_DataSize_8b;
    si.SPI_CPOL                = SPI_CPOL_Low;    /* mode 0 */
    si.SPI_CPHA                = SPI_CPHA_1Edge;  /* mode 0 */
    si.SPI_NSS                 = SPI_NSS_Soft;
    si.SPI_BaudRatePrescaler   = LCD_SPI_PRESCALER;
    si.SPI_FirstBit            = SPI_FirstBit_MSB;
    si.SPI_CRCPolynomial       = 7;
    SPI_Init(LCD_SPI, &si);
    SPI_CalculateCRC(LCD_SPI, DISABLE);
    SPI_Cmd(LCD_SPI, ENABLE);
}

/* ===================================================================
 * ST7789 init sequence (standard 2.4" module, 16bpp, BGR)
 * =================================================================== */
void lcd_init(void)
{
    lcd_gpio_init();
    lcd_spi_init();

    /* hardware reset */
    LCD_RST_LO();
    delay_ms(20);
    LCD_RST_HI();
    delay_ms(120);

    lcd_cmd(0x01);   /* SWRESET */
    delay_ms(120);

    lcd_cmd(0x11);   /* SLPOUT */
    delay_ms(120);

    lcd_cmd(0x13);   /* NORON (normal display mode) */

    lcd_cmd(0x36);   /* MADCTL: LANDSCAPE (MV|MX), BGR bit OFF (panel is RGB glass) */
    lcd_data8(LCD_MADCTL);

    lcd_cmd(0x3A);   /* COLMOD: 16 bpp RGB565 */
    lcd_data8(0x55);

    { uint8_t d[5] = {0x0C, 0x0C, 0x00, 0x33, 0x33}; lcd_cmd(0xB2); lcd_data_buf(d, 5); } /* PORCTRL */
    { lcd_cmd(0xB7); lcd_data8(0x35); }                          /* GCTRL   VGH/VGL */
    { lcd_cmd(0xBB); lcd_data8(0x19); }                          /* VCOMS   0.7V   */
    { lcd_cmd(0xC0); lcd_data8(0x2C); }                          /* LCMCTRL */
    { uint8_t d[2] = {0x01, 0xFF}; lcd_cmd(0xC2); lcd_data_buf(d, 2); } /* VDVVRHEN */
    { lcd_cmd(0xC3); lcd_data8(0x27); }                          /* VRHS    */
    { lcd_cmd(0xC4); lcd_data8(0x1F); }                          /* VDVSET  */
    { lcd_cmd(0xC6); lcd_data8(0x0A); }                          /* FRCTRL2 ~60Hz */
    { uint8_t d[2] = {0xA4, 0xA1}; lcd_cmd(0xD0); lcd_data_buf(d, 2); } /* PWCTRL1 */

    /* INVON (0x21) intentionally NOT sent: this RGB-glass panel renders
       colors correctly with inversion OFF. With 0x21 + BGR-bit both on,
       black showed white and red showed yellow (inversion + R/B swap stacked). */

    lcd_cmd(0x37);   /* VSCRSADD: vertical scroll start = 0 (kill leftover scroll) */
    { uint8_t d[2] = {0x00, 0x00}; lcd_data_buf(d, 2); }

    lcd_cmd(0x29);   /* DISPON */
    delay_ms(50);

    lcd_set_backlight(1);
}

void lcd_set_backlight(uint8_t on)
{
    if (on) LCD_BLK_ON(); else LCD_BLK_OFF();
}

/* ===================================================================
 * drawing primitives
 * =================================================================== */

/* set active write window then issue RAMWR */
static void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x2 = x + w - 1;
    uint16_t y2 = y + h - 1;
    uint8_t  b[4];

    if (x2 >= LCD_W) x2 = LCD_W - 1;
    if (y2 >= LCD_H) y2 = LCD_H - 1;

    lcd_cmd(0x2A);                       /* CASET */
    b[0] = (uint8_t)(x  >> 8); b[1] = (uint8_t)(x  & 0xFF);
    b[2] = (uint8_t)(x2 >> 8); b[3] = (uint8_t)(x2 & 0xFF);
    lcd_data_buf(b, 4);

    lcd_cmd(0x2B);                       /* RASET */
    b[0] = (uint8_t)(y  >> 8); b[1] = (uint8_t)(y  & 0xFF);
    b[2] = (uint8_t)(y2 >> 8); b[3] = (uint8_t)(y2 & 0xFF);
    lcd_data_buf(b, 4);

    lcd_cmd(0x2C);                       /* RAMWR */
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0 || h == 0) return;
    lcd_set_window(x, y, w, h);

    uint32_t  n  = (uint32_t)w * (uint32_t)h;
    uint8_t   hi = (uint8_t)(color >> 8);
    uint8_t   lo = (uint8_t)(color & 0xFF);

    LCD_CS_LO();
    LCD_DC_HI();
    while (n--)
    {
        lcd_spi_xfer(hi);
        lcd_spi_xfer(lo);
    }
    LCD_CS_HI();
}

void lcd_fill_screen(uint16_t color)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, color);
}

void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_set_window(x, y, 1, 1);
    uint8_t b[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
    lcd_data_buf(b, 2);
}

/* row-major RGB565 source -> one window, one continuous SPI burst.
   Much faster than w*h calls to lcd_draw_pixel (used for TFT text). */
void lcd_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *px)
{
    uint32_t n;
    if (w == 0 || h == 0) return;
    lcd_set_window(x, y, w, h);

    n = (uint32_t)w * (uint32_t)h;
    LCD_CS_LO();
    LCD_DC_HI();
    while (n--)
    {
        lcd_spi_xfer((uint8_t)(*px >> 8));
        lcd_spi_xfer((uint8_t)(*px & 0xFF));
        px++;
    }
    LCD_CS_HI();
}

void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    lcd_fill_rect(x, y, w, 1, color);
}

void lcd_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    lcd_fill_rect(x, y, 1, h, color);
}

void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    lcd_draw_hline(x,         y,         w, color);
    lcd_draw_hline(x,         y + h - 1, w, color);
    lcd_draw_vline(x,         y,         h, color);
    lcd_draw_vline(x + w - 1, y,         h, color);
}

/* ===================================================================
 * color + layout confirmation: black field with three primaries placed
 * at known positions. Expected after the MADCTL=0x00 / INVON-off fix:
 *   - black background
 *   - RED bar at the very top (full width)
 *   - GREEN bar at the very bottom (full width)
 *   - BLUE square centered
 * If all three primaries render correctly and the top bar is at the top,
 * Panel bring-up complete.
 * =================================================================== */
void lcd_test_pattern(void)
{
    lcd_fill_screen(COLOR_BLACK);
    lcd_fill_rect(0, 0, LCD_W, 40, COLOR_RED);              /* top bar    = red   */
    lcd_fill_rect(0, LCD_H - 40, LCD_W, 40, COLOR_GREEN);   /* bottom bar = green */
    lcd_fill_rect(LCD_W / 2 - 20, LCD_H / 2 - 20, 40, 40, COLOR_BLUE); /* center */
}
