/**
 * @file aquarium_sensors.c
 * @brief 智能水族箱传感器换算库实现
 */

#include "aquarium_sensors.h"

/* ============================================================================
 * 辅助函数
 * ============================================================================
 */

float aqua_sensor_clamp(float value, float min_val, float max_val) {
  if (value < min_val) {
    return min_val;
  }
  if (value > max_val) {
    return max_val;
  }
  return value;
}

/* ============================================================================
 * ADC 到电压转换
 * ============================================================================
 */

float aqua_sensor_adc_to_voltage(uint16_t adc_value) {
  /* 限制 ADC 值范围 */
  if (adc_value > ADC_RESOLUTION - 1) {
    adc_value = ADC_RESOLUTION - 1;
  }
  /* voltage = adc_value / 4096 * 3.3V */
  return (float)adc_value * ((float)ADC_VREF_MV / 1000.0f) /
         (float)ADC_RESOLUTION;
}

/* ============================================================================
 * pH 传感器换算
 * ============================================================================
 */

float aqua_sensor_ph_from_voltage(float voltage) {
  /*
   * 线性公式：pH = SLOPE * voltage + INTERCEPT
   *
   * 典型 pH 模块工作原理：
   * - 输出电压随 pH 变化呈线性关系
   * - pH 7（中性）通常对应 ~2.5V
   * - pH 增大时电压降低，pH 减小时电压升高
   *
   * 默认参数推导（基于常见模块）：
   * - pH 7 @ 2.5V: 7 = -5.70 * 2.5 + 21.25 ≈ 7
   * - pH 4 @ 3.0V: 4 = -5.70 * 3.0 + 21.25 ≈ 4.15
   * - pH 10 @ 2.0V: 10 = -5.70 * 2.0 + 21.25 ≈ 9.85
   */
  float ph = PH_SLOPE * voltage + PH_INTERCEPT;

  /* Clamp 到有效范围 */
  return aqua_sensor_clamp(ph, PH_MIN, PH_MAX);
}

/* ============================================================================
 * TDS 传感器换算
 * ============================================================================
 */

float aqua_sensor_tds_from_voltage(float voltage) {
  /*
   * TDS（溶解性固体总量）换算：
   * - 典型 TDS 模块输出 0~2.3V 对应 0~1000+ ppm
   * - 简化线性公式：TDS = 电压(mV) * 系数
   *
   * 公式：TDS (ppm) = voltage(V) * 1000 * TDS_COEFF
   */
  if (voltage < 0.0f) {
    voltage = 0.0f;
  }

  float tds = voltage * 1000.0f * TDS_COEFF;

  /* Clamp 到有效范围 */
  return aqua_sensor_clamp(tds, TDS_MIN, TDS_MAX);
}

/* ============================================================================
 * 浊度传感器换算
 * ============================================================================
 */

float aqua_sensor_turbidity_from_voltage(float voltage) {
  /*
   * 浊度（Turbidity）换算：
   * - 浊度与透光度成反比
   * - 清水时电压高（约 4V），浑浊时电压低
   * - 公式：turbidity = MAX_NTU * (1 - voltage / V_CLEAR)
   *
   * 当 voltage >= V_CLEAR 时，turbidity = 0（清水）
   * 当 voltage <= 0 时，turbidity = MAX_NTU（极浑浊）
   */
  if (voltage < 0.0f) {
    voltage = 0.0f;
  }

  float turbidity = TURB_MAX_NTU * (1.0f - voltage / TURB_CLEAR_VOLTAGE);

  /* Clamp 到有效范围 */
  return aqua_sensor_clamp(turbidity, TURBIDITY_MIN, TURBIDITY_MAX);
}

/* ============================================================================
 * 水位传感器换算
 * ============================================================================
 */

float aqua_sensor_water_level_from_voltage(float voltage) {
  /*
   * 水位百分比换算：
   * - 电阻式/电容式传感器，电压与水位正相关
   * - 公式：level = (voltage - V_MIN) / (V_MAX - V_MIN) * 100
   *
   * V_MIN = 0.5V（空）, V_MAX = 3.0V（满）
   */
  float range = WATER_LEVEL_V_MAX - WATER_LEVEL_V_MIN;
  if (range <= 0.0f) {
    return 0.0f;
  }

  float level = (voltage - WATER_LEVEL_V_MIN) / range * 100.0f;

  /* Clamp 到有效范围 */
  return aqua_sensor_clamp(level, WATER_LEVEL_MIN, WATER_LEVEL_MAX);
}

float aqua_sensor_water_level_from_adc(uint16_t adc_value) {
  float voltage = aqua_sensor_adc_to_voltage(adc_value);
  return aqua_sensor_water_level_from_voltage(voltage);
}
