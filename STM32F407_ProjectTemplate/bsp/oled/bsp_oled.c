/*
 * bsp_oled.c - SSD1306 0.96" 128x64 OLED, HARDWARE I2C1 on PB6(SCL)/PB7(SDA).
 *
 * Driver logic (framebuffer OLED_GRAM, draw primitives, multi-font ShowChar,
 * ShowString/ShowNum/ShowChinese/ShowPicture) ported from the
 * "0.96OLED程序源码 / STM32F407ZG_IIC例程" reference. Only the I2C transport
 * is swapped from software bit-bang to hardware I2C1: blocking master-write,
 * timeout-guarded so a missing display never hangs. 4-pin module (no RES),
 * so init is the SSD1306 software command sequence only.
 *
 * Fonts (asc2_0806/1206/1608/2412 + Hzk1-4) come from oledfont.h
 * (verbatim GB2312 copy of the reference; Keil compiles GB2312 comments fine).
 */
#include "bsp_oled.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_i2c.h"
#include "board.h"          /* delay_ms */
#include "oledfont.h"
#include <stdlib.h>

uint8_t OLED_GRAM[144][8];

/* ===================== hardware I2C1 transport ===================== */
static void OLED_I2C_Init(void)
{
    GPIO_InitTypeDef gi;
    I2C_InitTypeDef  ii;

    RCC_AHB1PeriphClockCmd(OLED_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(OLED_I2C_CLK, ENABLE);

    /* PB6 / PB7 AF4 open-drain + pull-up */
    gi.GPIO_Pin   = OLED_SCL_PIN | OLED_SDA_PIN;
    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_OD;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OLED_GPIO_PORT, &gi);
    GPIO_PinAFConfig(OLED_GPIO_PORT, OLED_SCL_SRC, OLED_GPIO_AF);
    GPIO_PinAFConfig(OLED_GPIO_PORT, OLED_SDA_SRC, OLED_GPIO_AF);

    I2C_DeInit(OLED_I2C);
    ii.I2C_Mode                = I2C_Mode_I2C;
    ii.I2C_DutyCycle           = I2C_DutyCycle_2;
    ii.I2C_OwnAddress1         = 0x00;
    ii.I2C_Ack                 = I2C_Ack_Enable;
    ii.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    ii.I2C_ClockSpeed          = OLED_I2C_SPEED;
    I2C_Init(OLED_I2C, &ii);
    I2C_Cmd(OLED_I2C, ENABLE);
    I2C_AcknowledgeConfig(OLED_I2C, ENABLE);
}

/* blocking master write: one transaction = 7-bit addr + buf[0..len-1] */
static void oled_i2c_write_buf(uint8_t *buf, uint16_t len)
{
    uint32_t to;

    to = 0x100000;
    while (I2C_GetFlagStatus(OLED_I2C, I2C_FLAG_BUSY) && to--) ;

    I2C_GenerateSTART(OLED_I2C, ENABLE);
    to = 0x100000;
    while (!I2C_CheckEvent(OLED_I2C, I2C_EVENT_MASTER_MODE_SELECT) && to--) ;

    I2C_Send7bitAddress(OLED_I2C, (uint8_t)(OLED_ADDR_7BIT << 1),
                        I2C_Direction_Transmitter);
    to = 0x100000;
    while (!I2C_CheckEvent(OLED_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) && to--) ;

    while (len--)
    {
        I2C_SendData(OLED_I2C, *buf++);
        to = 0x100000;
        while (!I2C_CheckEvent(OLED_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED) && to--) ;
    }
    I2C_GenerateSTOP(OLED_I2C, ENABLE);
}

/* mode: 0=command (control 0x00), 1=data (control 0x40) */
void OLED_WR_Byte(uint8_t dat, uint8_t mode)
{
    uint8_t b[2];
    b[0] = mode ? 0x40 : 0x00;
    b[1] = dat;
    oled_i2c_write_buf(b, 2);
}

/* ===================== framebuffer -> GDDRAM (bulk) ================ */
void OLED_Refresh(void)
{
    uint8_t i, n;
    uint8_t cbuf[4];
    uint8_t dbuf[129];

    for (i = 0; i < 8; i++)
    {
        /* page + column (commands) */
        cbuf[0] = 0x00; cbuf[1] = 0xb0 + i; cbuf[2] = 0x00; cbuf[3] = 0x10;
        oled_i2c_write_buf(cbuf, 4);
        /* data: control 0x40 + 128 GRAM bytes in one transaction */
        dbuf[0] = 0x40;
        for (n = 0; n < 128; n++)
            dbuf[1 + n] = OLED_GRAM[n][i];
        oled_i2c_write_buf(dbuf, 129);
    }
}

/* ===================== display control ============================= */
void OLED_ColorTurn(uint8_t i)
{
    if (i == 0) OLED_WR_Byte(0xA6, OLED_CMD);   /* normal  */
    if (i == 1) OLED_WR_Byte(0xA7, OLED_CMD);   /* invert  */
}

void OLED_DisplayTurn(uint8_t i)
{
    if (i == 0) { OLED_WR_Byte(0xC8, OLED_CMD); OLED_WR_Byte(0xA1, OLED_CMD); }
    if (i == 1) { OLED_WR_Byte(0xC0, OLED_CMD); OLED_WR_Byte(0xA0, OLED_CMD); }
}

void OLED_DisPlay_On(void)
{
    OLED_WR_Byte(0x8D, OLED_CMD);
    OLED_WR_Byte(0x14, OLED_CMD);
    OLED_WR_Byte(0xAF, OLED_CMD);
}

void OLED_DisPlay_Off(void)
{
    OLED_WR_Byte(0x8D, OLED_CMD);
    OLED_WR_Byte(0x10, OLED_CMD);
    OLED_WR_Byte(0xAE, OLED_CMD);
}

void OLED_Clear(void)
{
    uint8_t i, n;
    for (i = 0; i < 8; i++)
        for (n = 0; n < 128; n++)
            OLED_GRAM[n][i] = 0;
    OLED_Refresh();
}

/* ===================== draw primitives ============================ */
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t)
{
    uint8_t i, m, n;
    i = y / 8;
    m = y % 8;
    n = 1 << m;
    if (t) { OLED_GRAM[x][i] |= n; }
    else
    {
        OLED_GRAM[x][i] = ~OLED_GRAM[x][i];
        OLED_GRAM[x][i] |= n;
        OLED_GRAM[x][i] = ~OLED_GRAM[x][i];
    }
}

void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t mode)
{
    uint16_t t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, uRow, uCol;

    delta_x = x2 - x1;
    delta_y = y2 - y1;
    uRow = x1;
    uCol = y1;
    if (delta_x > 0) incx = 1;
    else if (delta_x == 0) incx = 0;
    else { incx = -1; delta_x = -delta_x; }
    if (delta_y > 0) incy = 1;
    else if (delta_y == 0) incy = 0;
    else { incy = -1; delta_y = -delta_x; }
    if (delta_x > delta_y) distance = delta_x;
    else distance = delta_y;
    for (t = 0; t < distance + 1; t++)
    {
        OLED_DrawPoint(uRow, uCol, mode);
        xerr += delta_x;
        yerr += delta_y;
        if (xerr > distance) { xerr -= distance; uRow += incx; }
        if (yerr > distance) { yerr -= distance; uCol += incy; }
    }
}

void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r)
{
    int a, b, num;
    a = 0;
    b = r;
    while (2 * b * b >= r * r)
    {
        OLED_DrawPoint(x + a, y - b, 1);
        OLED_DrawPoint(x - a, y - b, 1);
        OLED_DrawPoint(x - a, y + b, 1);
        OLED_DrawPoint(x + a, y + b, 1);
        OLED_DrawPoint(x + b, y + a, 1);
        OLED_DrawPoint(x + b, y - a, 1);
        OLED_DrawPoint(x - b, y - a, 1);
        OLED_DrawPoint(x - b, y + a, 1);
        a++;
        num = (a * a + b * b) - r * r;
        if (num > 0) { b--; a--; }
    }
}

/* ===================== text (4 ASCII fonts) ======================= */
/* when set, OFF pixels are not drawn (used by the bold second pass) */
static uint8_t OLED_NoClear = 0;

void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size1, uint8_t mode)
{
    uint8_t i, m, temp, size2, chr1;
    uint8_t x0 = x, y0 = y;

    if (size1 == 8) size2 = 6;
    else size2 = (size1 / 8 + ((size1 % 8) ? 1 : 0)) * (size1 / 2);
    chr1 = chr - ' ';
    for (i = 0; i < size2; i++)
    {
        if      (size1 == 8)  temp = asc2_0806[chr1][i];
        else if (size1 == 12) temp = asc2_1206[chr1][i];
        else if (size1 == 16) temp = asc2_1608[chr1][i];
        else if (size1 == 24) temp = asc2_2412[chr1][i];
        else return;
        for (m = 0; m < 8; m++)
        {
            if (temp & 0x01) OLED_DrawPoint(x, y, mode);
            else if (!OLED_NoClear) OLED_DrawPoint(x, y, !mode);
            temp >>= 1;
            y++;
        }
        x++;
        if ((size1 != 8) && ((x - x0) == size1 / 2)) { x = x0; y0 = y0 + 8; }
        y = y0;
    }
}

void OLED_ShowString(uint8_t x, uint8_t y, const char *chr, uint8_t size1, uint8_t mode)
{
    while ((*chr >= ' ') && (*chr <= '~'))
    {
        OLED_ShowChar(x, y, (uint8_t)*chr, size1, mode);
        if (size1 == 8) x += 6;
        else x += size1 / 2;
        chr++;
    }
}

/* Bold text: draw normally, then a second SET-ONLY pass shifted 1px right.
 * Thickens 1px strokes so 8x16 text no longer looks thin/hollow. Each char
 * is one pixel wider, so ~14 chars still fit on a 128px line. */
void OLED_ShowStringBold(uint8_t x, uint8_t y, const char *chr, uint8_t size1, uint8_t mode)
{
    const char *p;
    uint8_t adv = (size1 == 8) ? 7 : (uint8_t)(size1 / 2 + 1);
    uint8_t xx;

    OLED_NoClear = 0;                      /* pass 1: normal (sets + clears) */
    for (p = chr, xx = x; (*p >= ' ') && (*p <= '~'); p++, xx += adv)
        OLED_ShowChar(xx, y, (uint8_t)*p, size1, mode);

    OLED_NoClear = 1;                      /* pass 2: set-only, shifted right */
    for (p = chr, xx = (uint8_t)(x + 1); (*p >= ' ') && (*p <= '~'); p++, xx += adv)
        OLED_ShowChar(xx, y, (uint8_t)*p, size1, mode);

    OLED_NoClear = 0;
}

uint32_t OLED_Pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while (n--) result *= m;
    return result;
}

void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size1, uint8_t mode)
{
    uint8_t t, temp, m = 0;
    if (size1 == 8) m = 2;
    for (t = 0; t < len; t++)
    {
        temp = (num / OLED_Pow(10, len - t - 1)) % 10;
        if (temp == 0) OLED_ShowChar(x + (size1 / 2 + m) * t, y, '0', size1, mode);
        else           OLED_ShowChar(x + (size1 / 2 + m) * t, y, temp + '0', size1, mode);
    }
}

/* ===================== Chinese (Hzk1-4) =========================== */
void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t num, uint8_t size1, uint8_t mode)
{
    uint8_t m, temp;
    uint8_t x0 = x, y0 = y;
    uint16_t i, size3 = (size1 / 8 + ((size1 % 8) ? 1 : 0)) * size1;

    for (i = 0; i < size3; i++)
    {
        if      (size1 == 16) temp = Hzk1[num][i];
        else if (size1 == 24) temp = Hzk2[num][i];
        else if (size1 == 32) temp = Hzk3[num][i];
        else if (size1 == 64) temp = Hzk4[num][i];
        else return;
        for (m = 0; m < 8; m++)
        {
            if (temp & 0x01) OLED_DrawPoint(x, y, mode);
            else             OLED_DrawPoint(x, y, !mode);
            temp >>= 1;
            y++;
        }
        x++;
        if ((x - x0) == size1) { x = x0; y0 = y0 + 8; }
        y = y0;
    }
}

/* ===================== picture ==================================== */
void OLED_ShowPicture(uint8_t x, uint8_t y, uint8_t sizex, uint8_t sizey,
                      const uint8_t BMP[], uint8_t mode)
{
    uint16_t j = 0;
    uint8_t i, n, temp, m;
    uint8_t x0 = x, y0 = y;

    sizey = sizey / 8 + ((sizey % 8) ? 1 : 0);
    for (n = 0; n < sizey; n++)
    {
        for (i = 0; i < sizex; i++)
        {
            temp = BMP[j];
            j++;
            for (m = 0; m < 8; m++)
            {
                if (temp & 0x01) OLED_DrawPoint(x, y, mode);
                else             OLED_DrawPoint(x, y, !mode);
                temp >>= 1;
                y++;
            }
            x++;
            if ((x - x0) == sizex) { x = x0; y0 = y0 + 8; }
            y = y0;
        }
    }
}

/* ===================== init (no HW reset, 4-pin) ==================
 * OLED_InitEx() is the parameterised init used by the init-config auto-scan
 * in main.c, so every candidate row/COM setting can be tried without a
 * recompile.  Parameters:
 *   mux      : 0xA8 value  (0x3F = 64 rows, 0x1F = 32 rows)
 *   com_cfg  : 0xDA value  (0x12 alt, 0x02 seq, 0x22 seq+remap, 0x32 alt+remap)
 *   vcomh    : 0xDB value  (0x00 / 0x20 / 0x30 documented, 0x40 = ref value)
 *   scan_dir : 0xC8 (COM remapped) or 0xC0 (normal)
 *   pump     : 0 = SSD1306 charge pump 0x8D/0x14
 *              1 = SH1106 internal DC-DC 0xAD/0x8B
 *              2 = no pump command at all
 */
void OLED_InitEx(uint8_t mux, uint8_t com_cfg, uint8_t vcomh,
                 uint8_t scan_dir, uint8_t pump)
{
    delay_ms(100);               /* power-on settle (no RES pin) */
    OLED_I2C_Init();

    OLED_WR_Byte(0xAE, OLED_CMD);   /* display off */
    OLED_WR_Byte(0x00, OLED_CMD);   /* low column addr */
    OLED_WR_Byte(0x10, OLED_CMD);   /* high column addr */
    OLED_WR_Byte(0x40, OLED_CMD);   /* start line 0 */
    OLED_WR_Byte(0x81, OLED_CMD);   /* contrast */
    OLED_WR_Byte(0xFF, OLED_CMD);
    OLED_WR_Byte(0xA1, OLED_CMD);   /* segment remap */
    OLED_WR_Byte(scan_dir, OLED_CMD);   /* 0xC8 remapped / 0xC0 normal */
    OLED_WR_Byte(0xA6, OLED_CMD);   /* normal display */
    OLED_WR_Byte(0xA8, OLED_CMD);   /* multiplex ratio */
    OLED_WR_Byte(mux, OLED_CMD);
    OLED_WR_Byte(0xD3, OLED_CMD);   /* display offset 0 */
    OLED_WR_Byte(0x00, OLED_CMD);
    OLED_WR_Byte(0xD5, OLED_CMD);   /* clock divide / osc freq */
    OLED_WR_Byte(0x80, OLED_CMD);
    OLED_WR_Byte(0xD9, OLED_CMD);   /* pre-charge period */
    OLED_WR_Byte(0xF1, OLED_CMD);
    OLED_WR_Byte(0xDA, OLED_CMD);   /* COM pins hardware config */
    OLED_WR_Byte(com_cfg, OLED_CMD);
    OLED_WR_Byte(0xDB, OLED_CMD);   /* VCOMH deselect level */
    OLED_WR_Byte(vcomh, OLED_CMD);
    OLED_WR_Byte(0x20, OLED_CMD);   /* page addressing mode */
    OLED_WR_Byte(0x02, OLED_CMD);
    if (pump == 0)
    {
        OLED_WR_Byte(0x8D, OLED_CMD);   /* SSD1306 charge pump on */
        OLED_WR_Byte(0x14, OLED_CMD);
    }
    else if (pump == 1)
    {
        OLED_WR_Byte(0xAD, OLED_CMD);   /* SH1106 internal DC-DC on */
        OLED_WR_Byte(0x8B, OLED_CMD);
    }
    OLED_WR_Byte(0xA4, OLED_CMD);   /* display follows GDDRAM */
    OLED_WR_Byte(0xA6, OLED_CMD);   /* normal (non-inverted) */
    OLED_Clear();
    OLED_WR_Byte(0xAF, OLED_CMD);   /* display on */
}

/* default: standard 128x64 SSD1306 configuration */
void OLED_Init(void)
{
    OLED_InitEx(0x3F, 0x12, 0x30, 0xC8, 0);
}

/* Same 128x64 init, but the WHOLE command list is sent in ONE I2C
 * transaction (like the bit-bang reference does). Tests whether sending a
 * command parameter in its own transaction upsets this module's decoder,
 * which would silently leave every parameter at its power-on default. */
void OLED_InitBulk(void)
{
    static const uint8_t cmd[] = {
        0xAE, 0x00, 0x10, 0x40, 0x81, 0xFF, 0xA1, 0xC8, 0xA6,
        0xA8, 0x3F, 0xD3, 0x00, 0xD5, 0x80, 0xD9, 0xF1,
        0xDA, 0x12, 0xDB, 0x30, 0x20, 0x02, 0x8D, 0x14,
        0xA4, 0xA6
    };
    uint8_t buf[1 + sizeof(cmd)];
    uint8_t i;

    delay_ms(100);
    OLED_I2C_Init();

    buf[0] = 0x00;                      /* command stream */
    for (i = 0; i < (uint8_t)sizeof(cmd); i++)
        buf[1 + i] = cmd[i];
    oled_i2c_write_buf(buf, (uint16_t)(1 + sizeof(cmd)));

    OLED_Clear();
    OLED_WR_Byte(0xAF, OLED_CMD);       /* display on */
}

/* fill the whole framebuffer with one byte pattern (0xFF = every pixel ON) */
void OLED_FillScreen(uint8_t pattern)
{
    uint8_t x, p;
    for (x = 0; x < 128; x++)
        for (p = 0; p < 8; p++)
            OLED_GRAM[x][p] = pattern;
}
