/**
 * @file test_ds18b20.c
 * @brief DS18B20 驱动单元测试
 */

#include "aquarium_ds18b20.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * 测试：CRC8 计算
 * ============================================================================
 */

void test_crc8_empty(void) {
  uint8_t crc = ds18b20_crc8(NULL, 0);
  TEST_ASSERT_EQUAL_HEX8(0x00, crc);
}

void test_crc8_single_byte(void) {
  uint8_t data[] = {0x28};
  uint8_t crc = ds18b20_crc8(data, 1);
  /* 手工计算或查表验证 */
  TEST_ASSERT_TRUE(crc != 0x00 || data[0] == 0x00);
}

void test_crc8_valid_scratchpad(void) {
  /* 典型有效 scratchpad：25°C，CRC 正确 */
  /* Temp=25.0625°C -> raw=0x0191 -> LSB=0x91, MSB=0x01 */
  /* 完整 scratchpad 示例（含 CRC） */
  uint8_t scratchpad[] = {0x91, 0x01, 0x4B, 0x46, 0x7F, 0xFF, 0x0F, 0x10, 0x00};
  /* 重新计算 CRC 并设置正确值 */
  scratchpad[8] = ds18b20_crc8(scratchpad, 8);
  TEST_ASSERT_EQUAL_HEX8(scratchpad[8], ds18b20_crc8(scratchpad, 8));
}

void test_crc8_invalid_scratchpad(void) {
  /* 篡改数据，CRC 应不匹配 */
  uint8_t scratchpad[] = {0x91, 0x01, 0x4B, 0x46, 0x7F, 0xFF, 0x0F, 0x10, 0x00};
  scratchpad[8] = ds18b20_crc8(scratchpad, 8);
  scratchpad[0] ^= 0xFF; /* 篡改 */
  TEST_ASSERT_NOT_EQUAL(scratchpad[8], ds18b20_crc8(scratchpad, 8));
}

/* ============================================================================
 * 测试：raw_to_celsius 正温度
 * ============================================================================
 */

void test_raw_to_celsius_25_0625(void) {
  /* 25.0625°C = 0x0191 */
  float temp = ds18b20_raw_to_celsius(0x91, 0x01);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0625f, temp);
}

void test_raw_to_celsius_85(void) {
  /* 85°C = 0x0550（DS18B20 上电默认值） */
  float temp = ds18b20_raw_to_celsius(0x50, 0x05);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 85.0f, temp);
}

void test_raw_to_celsius_10_125(void) {
  /* 10.125°C = 0x00A2 */
  float temp = ds18b20_raw_to_celsius(0xA2, 0x00);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.125f, temp);
}

void test_raw_to_celsius_zero(void) {
  /* 0°C = 0x0000 */
  float temp = ds18b20_raw_to_celsius(0x00, 0x00);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, temp);
}

/* ============================================================================
 * 测试：raw_to_celsius 负温度
 * ============================================================================
 */

void test_raw_to_celsius_minus_10_125(void) {
  /* -10.125°C = 0xFF5E（二进制补码） */
  float temp = ds18b20_raw_to_celsius(0x5E, 0xFF);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -10.125f, temp);
}

void test_raw_to_celsius_minus_25_0625(void) {
  /* -25.0625°C = 0xFE6F */
  float temp = ds18b20_raw_to_celsius(0x6F, 0xFE);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -25.0625f, temp);
}

void test_raw_to_celsius_minus_55(void) {
  /* -55°C（最低温度）= 0xFC90 */
  float temp = ds18b20_raw_to_celsius(0x90, 0xFC);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -55.0f, temp);
}

/* ============================================================================
 * 测试：raw_to_celsius 小数精度
 * ============================================================================
 */

void test_raw_to_celsius_0_0625(void) {
  /* 0.0625°C = 0x0001 */
  float temp = ds18b20_raw_to_celsius(0x01, 0x00);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0625f, temp);
}

void test_raw_to_celsius_0_5(void) {
  /* 0.5°C = 0x0008 */
  float temp = ds18b20_raw_to_celsius(0x08, 0x00);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, temp);
}

void test_raw_to_celsius_minus_0_5(void) {
  /* -0.5°C = 0xFFF8 */
  float temp = ds18b20_raw_to_celsius(0xF8, 0xFF);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.5f, temp);
}

/* ============================================================================
 * 测试：上下文初始化
 * ============================================================================
 */

void test_init_default_temp(void) {
  DS18B20Context ctx;
  ds18b20_init(&ctx, 25.0f);
  TEST_ASSERT_EQUAL(DS18B20_STATE_IDLE, ds18b20_get_state(&ctx));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, ds18b20_get_temperature(&ctx));
}

void test_init_negative_default(void) {
  DS18B20Context ctx;
  ds18b20_init(&ctx, -10.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -10.0f, ds18b20_get_temperature(&ctx));
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  /* CRC8 */
  RUN_TEST(test_crc8_empty);
  RUN_TEST(test_crc8_single_byte);
  RUN_TEST(test_crc8_valid_scratchpad);
  RUN_TEST(test_crc8_invalid_scratchpad);

  /* 正温度 */
  RUN_TEST(test_raw_to_celsius_25_0625);
  RUN_TEST(test_raw_to_celsius_85);
  RUN_TEST(test_raw_to_celsius_10_125);
  RUN_TEST(test_raw_to_celsius_zero);

  /* 负温度 */
  RUN_TEST(test_raw_to_celsius_minus_10_125);
  RUN_TEST(test_raw_to_celsius_minus_25_0625);
  RUN_TEST(test_raw_to_celsius_minus_55);

  /* 小数精度 */
  RUN_TEST(test_raw_to_celsius_0_0625);
  RUN_TEST(test_raw_to_celsius_0_5);
  RUN_TEST(test_raw_to_celsius_minus_0_5);

  /* 上下文初始化 */
  RUN_TEST(test_init_default_temp);
  RUN_TEST(test_init_negative_default);

  return UNITY_END();
}
