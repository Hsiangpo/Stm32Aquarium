/**
 * @file aquarium_oled.h
 * @brief SSD1306 OLED 显示驱动（I2C 128x64）
 */

#ifndef AQUARIUM_OLED_H
#define AQUARIUM_OLED_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OLED 尺寸 */
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)            /* 8 页 */
#define OLED_BUF_SIZE (OLED_WIDTH * OLED_PAGES) /* 1024 bytes */

/* 默认 I2C 地址 */
#define OLED_I2C_ADDR_DEFAULT 0x3C

/** I2C 硬件操作回调（硬件抽象） */
typedef struct {
  bool (*i2c_write)(uint8_t addr, const uint8_t *data, uint16_t len);
} OledHwOps;

/** OLED 上下文 */
typedef struct {
  uint8_t i2c_addr;
  uint8_t buffer[OLED_BUF_SIZE];
  const OledHwOps *hw;
} OledContext;

/* 初始化 */
void oled_init(OledContext *ctx, const OledHwOps *hw, uint8_t i2c_addr);

/* 帧缓冲操作 */
void oled_clear(OledContext *ctx);
void oled_set_pixel(OledContext *ctx, uint8_t x, uint8_t y, bool on);
void oled_draw_char(OledContext *ctx, uint8_t x, uint8_t y, char c);
void oled_draw_string(OledContext *ctx, uint8_t x, uint8_t y, const char *s);

/* 渲染到屏幕（分页发送） */
void oled_render(OledContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_OLED_H */
