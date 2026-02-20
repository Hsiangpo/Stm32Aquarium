/**
 * @file test_sensors.c
 * @brief 传感器换算库单元测试
 */

#include "aquarium_sensors.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * 测试：ADC 到电压转换
 * ============================================================================
 */

void test_adc_to_voltage_zero(void) {
  float v = aqua_sensor_adc_to_voltage(0);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, v);
}

void test_adc_to_voltage_max(void) {
  float v = aqua_sensor_adc_to_voltage(4095);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.3f, v);
}

void test_adc_to_voltage_mid(void) {
  float v = aqua_sensor_adc_to_voltage(2048);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.65f, v);
}

void test_adc_to_voltage_overflow(void) {
  /* 超出范围应被限制 */
  float v = aqua_sensor_adc_to_voltage(5000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.3f, v);
}

/* ============================================================================
 * 测试：pH 换算
 * ============================================================================
 */

void test_ph_clamp_min(void) {
  /* 极高电压应返回 pH 0 */
  float ph = aqua_sensor_ph_from_voltage(10.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, ph);
}

void test_ph_clamp_max(void) {
  /* 极低电压应返回 pH 14 */
  float ph = aqua_sensor_ph_from_voltage(-5.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 14.0f, ph);
}

void test_ph_typical_neutral(void) {
  /* 约 2.5V 应接近 pH 7 */
  float ph = aqua_sensor_ph_from_voltage(2.5f);
  TEST_ASSERT_TRUE(ph >= 6.0f && ph <= 8.0f);
}

void test_ph_monotonic(void) {
  /* 电压增加，pH 应减少 */
  float ph1 = aqua_sensor_ph_from_voltage(2.0f);
  float ph2 = aqua_sensor_ph_from_voltage(2.5f);
  float ph3 = aqua_sensor_ph_from_voltage(3.0f);
  TEST_ASSERT_TRUE(ph1 > ph2);
  TEST_ASSERT_TRUE(ph2 > ph3);
}

/* ============================================================================
 * 测试：TDS 换算
 * ============================================================================
 */

void test_tds_zero_voltage(void) {
  float tds = aqua_sensor_tds_from_voltage(0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tds);
}

void test_tds_negative_voltage(void) {
  /* 负电压应返回 0 */
  float tds = aqua_sensor_tds_from_voltage(-1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tds);
}

void test_tds_typical(void) {
  /* 1V 应返回 500 ppm (1000 * 0.5) */
  float tds = aqua_sensor_tds_from_voltage(1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 500.0f, tds);
}

void test_tds_monotonic(void) {
  /* 电压增加，TDS 应增加 */
  float tds1 = aqua_sensor_tds_from_voltage(0.5f);
  float tds2 = aqua_sensor_tds_from_voltage(1.0f);
  float tds3 = aqua_sensor_tds_from_voltage(1.5f);
  TEST_ASSERT_TRUE(tds1 < tds2);
  TEST_ASSERT_TRUE(tds2 < tds3);
}

/* ============================================================================
 * 测试：浊度换算
 * ============================================================================
 */

void test_turbidity_clear_water(void) {
  /* 4V（清水）应返回 0 NTU */
  float turb = aqua_sensor_turbidity_from_voltage(4.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, turb);
}

void test_turbidity_zero_voltage(void) {
  /* 0V 应返回最大浊度 3000 NTU */
  float turb = aqua_sensor_turbidity_from_voltage(0.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 3000.0f, turb);
}

void test_turbidity_negative(void) {
  /* 负电压应被限制 */
  float turb = aqua_sensor_turbidity_from_voltage(-1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 3000.0f, turb);
}

void test_turbidity_monotonic(void) {
  /* 电压增加，浊度应减少 */
  float t1 = aqua_sensor_turbidity_from_voltage(1.0f);
  float t2 = aqua_sensor_turbidity_from_voltage(2.0f);
  float t3 = aqua_sensor_turbidity_from_voltage(3.0f);
  TEST_ASSERT_TRUE(t1 > t2);
  TEST_ASSERT_TRUE(t2 > t3);
}

/* ============================================================================
 * 测试：水位换算
 * ============================================================================
 */

void test_water_level_empty(void) {
  /* 0.5V（空）应返回 0% */
  float level = aqua_sensor_water_level_from_voltage(0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, level);
}

void test_water_level_full(void) {
  /* 3.0V（满）应返回 100% */
  float level = aqua_sensor_water_level_from_voltage(3.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, level);
}

void test_water_level_half(void) {
  /* 1.75V 应返回 50% */
  float level = aqua_sensor_water_level_from_voltage(1.75f);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 50.0f, level);
}

void test_water_level_clamp_low(void) {
  /* 低于 0.5V 应返回 0% */
  float level = aqua_sensor_water_level_from_voltage(0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, level);
}

void test_water_level_clamp_high(void) {
  /* 高于 3.0V 应返回 100% */
  float level = aqua_sensor_water_level_from_voltage(5.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, level);
}

void test_water_level_from_adc(void) {
  /* 测试便捷函数 */
  float level = aqua_sensor_water_level_from_adc(2048);
  TEST_ASSERT_TRUE(level >= 0.0f && level <= 100.0f);
}

/* ============================================================================
 * 测试：clamp 辅助函数
 * ============================================================================
 */

void test_clamp_within_range(void) {
  float v = aqua_sensor_clamp(5.0f, 0.0f, 10.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, v);
}

void test_clamp_below_min(void) {
  float v = aqua_sensor_clamp(-5.0f, 0.0f, 10.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, v);
}

void test_clamp_above_max(void) {
  float v = aqua_sensor_clamp(15.0f, 0.0f, 10.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, v);
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  /* ADC 到电压 */
  RUN_TEST(test_adc_to_voltage_zero);
  RUN_TEST(test_adc_to_voltage_max);
  RUN_TEST(test_adc_to_voltage_mid);
  RUN_TEST(test_adc_to_voltage_overflow);

  /* pH */
  RUN_TEST(test_ph_clamp_min);
  RUN_TEST(test_ph_clamp_max);
  RUN_TEST(test_ph_typical_neutral);
  RUN_TEST(test_ph_monotonic);

  /* TDS */
  RUN_TEST(test_tds_zero_voltage);
  RUN_TEST(test_tds_negative_voltage);
  RUN_TEST(test_tds_typical);
  RUN_TEST(test_tds_monotonic);

  /* 浊度 */
  RUN_TEST(test_turbidity_clear_water);
  RUN_TEST(test_turbidity_zero_voltage);
  RUN_TEST(test_turbidity_negative);
  RUN_TEST(test_turbidity_monotonic);

  /* 水位 */
  RUN_TEST(test_water_level_empty);
  RUN_TEST(test_water_level_full);
  RUN_TEST(test_water_level_half);
  RUN_TEST(test_water_level_clamp_low);
  RUN_TEST(test_water_level_clamp_high);
  RUN_TEST(test_water_level_from_adc);

  /* clamp */
  RUN_TEST(test_clamp_within_range);
  RUN_TEST(test_clamp_below_min);
  RUN_TEST(test_clamp_above_max);

  return UNITY_END();
}
