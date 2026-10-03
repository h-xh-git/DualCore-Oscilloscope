#ifndef __BSP_OLED_H
#define __BSP_OLED_H
#include <stdint.h>

/*
 * bsp_oled.h - SSD1306 0.96" 128x64 OLED, HARDWARE I2C1.
 *
 * Driver logic (framebuffer OLED_GRAM, draw primitives, multi-font ShowChar,
 * ShowString/ShowNum/ShowChinese/ShowPicture) ported from the
 * "0.96OLED程序源码 / STM32F407ZG_IIC例程" reference; only the I2C transport
 * is hardware I2C1 (blocking, timeout-guarded) instead of software bit-bang.
 *
 * Pin map: I2C1 SCL=PB6, SDA=PB7 (AF4). 7-bit slave addr 0x3C (=0x78 write).
 * 4-pin module (no RES): software init only (no HW reset pin).
 * Fonts: asc2_0806 / asc2_1206 / asc2_1608 / asc2_2412 + Hzk1-4 in oledfont.h.
 *
 * Usage: draw into the framebuffer (ShowString/DrawPoint/...), then call
 * OLED_Refresh() to flush GRAM -> GDDRAM.  size1 in ShowChar/ShowString:
 * 8->6x8, 12->12x6, 16->8x16, 24->12x24.
 */

#define OLED_I2C              I2C1
#define OLED_I2C_CLK          RCC_APB1Periph_I2C1
#define OLED_GPIO_CLK         RCC_AHB1Periph_GPIOB
#define OLED_GPIO_PORT        GPIOB

#define OLED_SCL_PIN          GPIO_Pin_6
#define OLED_SCL_SRC          GPIO_PinSource6
#define OLED_SDA_PIN          GPIO_Pin_7
#define OLED_SDA_SRC          GPIO_PinSource7
#define OLED_GPIO_AF          GPIO_AF_I2C1     /* AF4 */
#define OLED_I2C_SPEED        400000U          /* fast mode */
#define OLED_ADDR_7BIT        0x3C             /* 8-bit write addr = 0x78 */

#define OLED_CMD   0
#define OLED_DATA  1

void OLED_WR_Byte(uint8_t dat, uint8_t mode);
void OLED_Init(void);
void OLED_InitEx(uint8_t mux, uint8_t com_cfg, uint8_t vcomh,
                 uint8_t scan_dir, uint8_t pump);
void OLED_InitBulk(void);
void OLED_Refresh(void);
void OLED_FillScreen(uint8_t pattern);
void OLED_Clear(void);
void OLED_ColorTurn(uint8_t i);
void OLED_DisplayTurn(uint8_t i);
void OLED_DisPlay_On(void);
void OLED_DisPlay_Off(void);
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t);
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t mode);
void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size1, uint8_t mode);
void OLED_ShowString(uint8_t x, uint8_t y, const char *chr, uint8_t size1, uint8_t mode);
void OLED_ShowStringBold(uint8_t x, uint8_t y, const char *chr, uint8_t size1, uint8_t mode);
uint32_t OLED_Pow(uint8_t m, uint8_t n);
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size1, uint8_t mode);
void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t num, uint8_t size1, uint8_t mode);
void OLED_ShowPicture(uint8_t x, uint8_t y, uint8_t sizex, uint8_t sizey, const uint8_t BMP[], uint8_t mode);

#endif /* __BSP_OLED_H */
