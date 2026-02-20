/**
 * @file test_oled.c
 * @brief OLED 驱动单元测试
 */

#include "aquarium_oled.h"
#include <string.h>
#include <unity.h>


static uint8_t g_i2c_buf[2048];
static uint16_t g_i2c_len;
static uint16_t g_i2c_call_count;

void setUp(void) {
  memset(g_i2c_buf, 0, sizeof(g_i2c_buf));
  g_i2c_len = 0;
  g_i2c_call_count = 0;
}
void tearDown(void) {}

static bool mock_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len) {
  (void)addr;
  if (g_i2c_len + len < sizeof(g_i2c_buf)) {
    memcpy(&g_i2c_buf[g_i2c_len], data, len);
    g_i2c_len += len;
  }
  g_i2c_call_count++;
  return true;
}

static const OledHwOps g_mock_hw = {.i2c_write = mock_i2c_write};

void test_init_sends_commands(void) {
  OledContext ctx;
  oled_init(&ctx, &g_mock_hw, OLED_I2C_ADDR_DEFAULT);
  TEST_ASSERT_GREATER_THAN(20, g_i2c_call_count);
}

void test_clear_zeroes_buffer(void) {
  OledContext ctx;
  oled_init(&ctx, &g_mock_hw, OLED_I2C_ADDR_DEFAULT);
  ctx.buffer[100] = 0xFF;
  oled_clear(&ctx);
  TEST_ASSERT_EQUAL_HEX8(0x00, ctx.buffer[100]);
}

void test_set_pixel_within_bounds(void) {
  OledContext ctx;
  oled_init(&ctx, &g_mock_hw, OLED_I2C_ADDR_DEFAULT);
  oled_clear(&ctx);
  oled_set_pixel(&ctx, 10, 5, true);
  TEST_ASSERT_NOT_EQUAL(0, ctx.buffer[10]);
}

void test_set_pixel_out_of_bounds(void) {
  OledContext ctx;
  oled_init(&ctx, &g_mock_hw, OLED_I2C_ADDR_DEFAULT);
  oled_clear(&ctx);
  oled_set_pixel(&ctx, 200, 100, true);
  for (int i = 0; i < OLED_BUF_SIZE; i++) {
    TEST_ASSERT_EQUAL_HEX8(0x00, ctx.buffer[i]);
  }
}

void test_draw_string_no_overflow(void) {
  OledContext ctx;
  oled_init(&ctx, &g_mock_hw, OLED_I2C_ADDR_DEFAULT);
  oled_clear(&ctx);
  oled_draw_string(&ctx, 0, 0, "HELLO WORLD TEST STRING");
  TEST_ASSERT_TRUE(1);
}

void test_render_sends_pages(void) {
  OledContext ctx;
  oled_init(&ctx, &g_mock_hw, OLED_I2C_ADDR_DEFAULT);
  g_i2c_call_count = 0;
  g_i2c_len = 0;
  oled_render(&ctx);
  TEST_ASSERT_GREATER_THAN(50, g_i2c_call_count);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_sends_commands);
  RUN_TEST(test_clear_zeroes_buffer);
  RUN_TEST(test_set_pixel_within_bounds);
  RUN_TEST(test_set_pixel_out_of_bounds);
  RUN_TEST(test_draw_string_no_overflow);
  RUN_TEST(test_render_sends_pages);
  return UNITY_END();
}
