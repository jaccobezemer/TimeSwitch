#pragma once

#define LCD_H_RES          240
#define LCD_V_RES          320
#define LCD_BITS_PIXEL     16
#define LCD_BUF_LINES      30
#define LCD_DOUBLE_BUFFER  1
#define LCD_DRAWBUF_SIZE   (LCD_H_RES * LCD_BUF_LINES)
#define LCD_MIRROR_X       (false)
#define LCD_MIRROR_Y       (false)

#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_CMD_BITS       (8)
#define LCD_PARAM_BITS     (8)
#define LCD_SPI_HOST       SPI2_HOST
#define LCD_SPI_CLK        (gpio_num_t) GPIO_NUM_14
#define LCD_SPI_MOSI       (gpio_num_t) GPIO_NUM_13
#define LCD_SPI_MISO       (gpio_num_t) GPIO_NUM_12
#define LCD_DC             (gpio_num_t) GPIO_NUM_2
#define LCD_CS             (gpio_num_t) GPIO_NUM_15
#define LCD_RESET          (gpio_num_t) GPIO_NUM_4
#define LCD_BUSY           (gpio_num_t) GPIO_NUM_NC


#define LCD_BACKLIGHT      (gpio_num_t) GPIO_NUM_21
#define LCD_BACKLIGHT_LEDC_CH  (1)

#define TOUCH_X_RES_MIN 0
#define TOUCH_X_RES_MAX 240
#define TOUCH_Y_RES_MIN 0
#define TOUCH_Y_RES_MAX 320

// Gemeten kalibratiepunten (raw XPT2046 waarden aan de schermranden)
#define TOUCH_X_RAW_LEFT   294
#define TOUCH_X_RAW_RIGHT  10
#define TOUCH_Y_RAW_TOP    229
#define TOUCH_Y_RAW_BOTTOM 11

// Touch orientatie (onafhankelijk van LCD-spiegeling)
// Bij LV_DISPLAY_ROTATION_90: Y-as van XPT2046 is gespiegeld t.o.v. het scherm
// Alle orientatie-correctie wordt gedaan door de kalibratie in process_coordinates.
// Driver flags moeten false zijn anders wordt er dubbel gecorrigeerd.
#define TOUCH_SWAP_XY   (false)
#define TOUCH_MIRROR_X  (true)      // Omdat het scherm 90 graden geroteerd is, is Y-as de X-as geworden vandaar dat X gemirrord wordt
#define TOUCH_MIRROR_Y  (false)

#define TOUCH_CLOCK_HZ ESP_LCD_TOUCH_SPI_CLOCK_HZ
#define TOUCH_SPI      SPI3_HOST
#define TOUCH_SPI_CLK  (gpio_num_t) GPIO_NUM_25
#define TOUCH_SPI_MOSI (gpio_num_t) GPIO_NUM_32
#define TOUCH_SPI_MISO (gpio_num_t) GPIO_NUM_39
#define TOUCH_CS       (gpio_num_t) GPIO_NUM_33
#define TOUCH_DC       (gpio_num_t) GPIO_NUM_NC
#define TOUCH_RST      (gpio_num_t) GPIO_NUM_NC
#define TOUCH_IRQ      (gpio_num_t) GPIO_NUM_36

#define RELAY_GPIO     (gpio_num_t) GPIO_NUM_3  // RXD0 marked on the board