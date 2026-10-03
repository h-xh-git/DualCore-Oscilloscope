/*
 * bsp_lcd.h - ST7789 2.4" TFT driver (240x320, RGB565, BGR panel)
 *
 * Hardware SPI1 master (write-only: SCK + MOSI, no MISO read needed).
 * Pin map is ALL macros below -> change one line if your board conflicts.
 *
 * Default wiring (嘉立创天空星 STM32F407VET6):
 *   SCK  = PA5   (SPI1_SCK, AF5)
 *   MOSI = PA7   (SPI1_MOSI, AF5)
 *   CS   = PA4   (GPIO out)
 *   DC   = PB0   (GPIO out)
 *   RST  = PB1   (GPIO out)
 *   BLK  = PB10  (GPIO out, backlight)
 */
#ifndef __BSP_LCD_H
#define __BSP_LCD_H

#include "stm32f4xx.h"

/* ===================== pin map (edit here on conflict) ===================== */
#define LCD_SPI             SPI1
#define LCD_SPI_CLK         RCC_APB2Periph_SPI1

#define LCD_GPIO_CLK        (RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB)

#define LCD_SCK_PORT         GPIOA
#define LCD_SCK_PIN          GPIO_Pin_5
#define LCD_SCK_PIN_SRC      GPIO_PinSource5

#define LCD_MOSI_PORT        GPIOA
#define LCD_MOSI_PIN         GPIO_Pin_7
#define LCD_MOSI_PIN_SRC     GPIO_PinSource7

#define LCD_CS_PORT          GPIOA
#define LCD_CS_PIN           GPIO_Pin_4

#define LCD_DC_PORT          GPIOB
#define LCD_DC_PIN           GPIO_Pin_0

#define LCD_RST_PORT         GPIOB
#define LCD_RST_PIN          GPIO_Pin_1

#define LCD_BLK_PORT         GPIOB
#define LCD_BLK_PIN          GPIO_Pin_10

/* SPI baudrate prescaler (APB2 = 84 MHz).
   _8  -> 10.5 MHz  (safe on long jumper wires, breadboard)
   _4  -> 21 MHz    (use if your wires are short and the panel is stable)
   _2  -> 42 MHz    (only on a real PCB with short traces) */
#ifndef LCD_SPI_PRESCALER
#define LCD_SPI_PRESCALER    SPI_BaudRatePrescaler_8
#endif

/* ---------------- control pin helpers ---------------- */
#define LCD_CS_HI()    GPIO_SetBits(LCD_CS_PORT,   LCD_CS_PIN)
#define LCD_CS_LO()    GPIO_ResetBits(LCD_CS_PORT, LCD_CS_PIN)
#define LCD_DC_HI()    GPIO_SetBits(LCD_DC_PORT,   LCD_DC_PIN)
#define LCD_DC_LO()    GPIO_ResetBits(LCD_DC_PORT, LCD_DC_PIN)
#define LCD_RST_HI()   GPIO_SetBits(LCD_RST_PORT,  LCD_RST_PIN)
#define LCD_RST_LO()   GPIO_ResetBits(LCD_RST_PORT,LCD_RST_PIN)
#define LCD_BLK_ON()   GPIO_SetBits(LCD_BLK_PORT,  LCD_BLK_PIN)
#define LCD_BLK_OFF()  GPIO_ResetBits(LCD_BLK_PORT,LCD_BLK_PIN)

/* ---------------- panel geometry (2.4" ST7789) ----------------
 * The panel is 240x320 natively; MADCTL=0x60 (MV|MX) rotates the frame
 * memory so we drive it as 320x240 LANDSCAPE. */
#define LCD_W   320
#define LCD_H   240

/* MADCTL 0x36 value.  0x60 = landscape (rotation 1), BGR bit OFF.
 * If the picture comes out upside-down change this to 0xA0 (MY|MV). */
#ifndef LCD_MADCTL
#define LCD_MADCTL  0xA0    //0x60
#endif

/* ---------------- RGB565 color macros ---------------- */
#define RGB565(r,g,b)  ((uint16_t)((((r)&0xF8)<<8) | (((g)&0xFC)<<3) | ((b)>>3)))
#define COLOR_BLACK    0x0000
#define COLOR_WHITE    0xFFFF
#define COLOR_RED      0xF800
#define COLOR_GREEN    0x07E0
#define COLOR_BLUE     0x001F
#define COLOR_YELLOW   0xFFE0
#define COLOR_CYAN     0x07FF
#define COLOR_MAGENTA  0xF81F
#define COLOR_ORANGE   0xFD20
#define COLOR_GRAY     0x8410

/* ---------------- public API ---------------- */
void lcd_init(void);
void lcd_set_backlight(uint8_t on);

void lcd_fill_screen(uint16_t color);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/* blit a row-major RGB565 bitmap: px[0]=top-left, then left->right, top->bottom */
void lcd_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *px);
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void lcd_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

void lcd_test_pattern(void);   /* bring-up: color bands + border + center box */

#endif /* __BSP_LCD_H */
