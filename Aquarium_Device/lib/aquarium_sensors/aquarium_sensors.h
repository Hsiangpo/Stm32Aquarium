/**
 * @file aquarium_sensors.h
 * @brief 智能水族箱传感器换算库（硬件无关）
 *
 * 提供 ADC 原始值到物理量的换算函数：
 * - ADC 到电压转换
 * - 电压到 pH 值转换
 * - 电压到 TDS 值转换
 * - 电压到浊度值转换
 * - ADC/电压到水位百分比转换
 *
 * 所有函数为纯函数，不依赖 HAL，可在任意平台上单测。
 * 换算系数可通过 ph_offset/tds_factor 在应用层进行校准。
 */

#ifndef AQUARIUM_SENSORS_H
#define AQUARIUM_SENSORS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * ADC 参数常量（STM32F103 12位ADC，参考电压3.3V）
 * ============================================================================
 */

#define ADC_RESOLUTION 4096 /* 12-bit ADC: 0~4095 */
#define ADC_VREF_MV 3300    /* 参考电压 3.3V = 3300mV */

/* ============================================================================
 * 传感器换算系数（默认值，可通过校准调整）
 * ============================================================================
 */

/*
 * pH 传感器：
 * - 典型模块输出电压范围 0~3V 对应 pH 0~14
 * - 中性点 pH=7 对应约 2.5V（部分模块） 或 1.5V（部分模块）
 * - 采用线性公式：pH = SLOPE * voltage + INTERCEPT
 * - 默认参数基于常见模块（中性点2.5V，斜率约 -5.25 pH/V）
 *
 * 注意：实际使用时需通过标准液校准确定 ph_offset
 */
#define PH_SLOPE (-5.70f)   /* pH / V, 需校准 */
#define PH_INTERCEPT 21.34f /* pH @ 0V 截距，需校准 */

/*
 * TDS 传感器：
 * - 典型模块输出电压范围 0~2.3V
 * - 电压与 TDS 近似线性关系（低浓度时）
 * - 公式：TDS = K * voltage * 1000 (ppm)
 * - K 系数约 0.5~0.7，默认 0.5（需校准）
 *
 * 注意：实际使用时需通过 tds_factor 校准
 */
#define TDS_COEFF 0.5f /* TDS / (mV / 1000), 需校准 */

/*
 * 浊度传感器：
 * - 典型模块输出电压范围 0~4.5V（实际受 ADC 参考电压限制）
 * - 浊度与电压成反比：透光度越高，电压越高，浊度越低
 * - 公式：turbidity = MAX_NTU * (1 - voltage / V_CLEAR)
 * - V_CLEAR 为清水时的电压（约 4.0V），MAX_NTU 为满量程（约 3000 NTU）
 *
 * 简化公式：turbidity = TURB_SLOPE * voltage + TURB_INTERCEPT
 */
#define TURB_CLEAR_VOLTAGE 4.0f /* 清水时电压 V */
#define TURB_MAX_NTU 3000.0f    /* 满量程 NTU */

/*
 * 水位传感器：
 * - 典型模块为电阻式或电容式，输出电压与水位正相关
 * - 公式：level = (voltage - V_MIN) / (V_MAX - V_MIN) * 100%
 * - V_MIN 为空时电压（约 0.5V），V_MAX 为满时电压（约 3.0V）
 */
#define WATER_LEVEL_V_MIN 0.5f /* 空时电压 V */
#define WATER_LEVEL_V_MAX 3.0f /* 满时电压 V */

/* ============================================================================
 * 范围限制（物理量约束）
 * ============================================================================
 */

#define PH_MIN 0.0f
#define PH_MAX 14.0f
#define TDS_MIN 0.0f
#define TDS_MAX 5000.0f /* 一般淡水养殖不超过 2000 ppm */
#define TURBIDITY_MIN 0.0f
#define TURBIDITY_MAX 3000.0f
#define WATER_LEVEL_MIN 0.0f
#define WATER_LEVEL_MAX 100.0f

/* ============================================================================
 * ADC 到电压转换
 * ============================================================================
 */

/**
 * @brief 将 ADC 原始值转换为电压（伏特）
 *
 * @param adc_value ADC 原始值（0~4095）
 * @return 电压值（0.0~3.3V）
 */
float aqua_sensor_adc_to_voltage(uint16_t adc_value);

/* ============================================================================
 * pH 传感器换算
 * ============================================================================
 */

/**
 * @brief 从电压计算 pH 值
 *
 * 使用默认换算公式，结果被 clamp 到 [0, 14] 范围
 * 校准偏移量应在调用后由应用层加上
 *
 * @param voltage 传感器输出电压（V）
 * @return pH 值（0.0~14.0）
 */
float aqua_sensor_ph_from_voltage(float voltage);

/* ============================================================================
 * TDS 传感器换算
 * ============================================================================
 */

/**
 * @brief 从电压计算 TDS 值
 *
 * 使用默认换算公式，结果被 clamp 到 [0, 5000] ppm 范围
 * 校准系数应在调用后由应用层乘上
 *
 * @param voltage 传感器输出电压（V）
 * @return TDS 值（ppm，≥0）
 */
float aqua_sensor_tds_from_voltage(float voltage);

/* ============================================================================
 * 浊度传感器换算
 * ============================================================================
 */

/**
 * @brief 从电压计算浊度值
 *
 * 使用默认换算公式，结果被 clamp 到 [0, 3000] NTU 范围
 *
 * @param voltage 传感器输出电压（V）
 * @return 浊度值（NTU，≥0）
 */
float aqua_sensor_turbidity_from_voltage(float voltage);

/* ============================================================================
 * 水位传感器换算
 * ============================================================================
 */

/**
 * @brief 从电压计算水位百分比
 *
 * 结果被 clamp 到 [0, 100]% 范围
 *
 * @param voltage 传感器输出电压（V）
 * @return 水位百分比（0~100%）
 */
float aqua_sensor_water_level_from_voltage(float voltage);

/**
 * @brief 从 ADC 原始值计算水位百分比
 *
 * 便捷函数，内部先转换为电压再计算
 *
 * @param adc_value ADC 原始值（0~4095）
 * @return 水位百分比（0~100%）
 */
float aqua_sensor_water_level_from_adc(uint16_t adc_value);

/* ============================================================================
 * 辅助函数
 * ============================================================================
 */

/**
 * @brief 将浮点值限制在指定范围内
 *
 * @param value 输入值
 * @param min_val 最小值
 * @param max_val 最大值
 * @return 限制后的值
 */
float aqua_sensor_clamp(float value, float min_val, float max_val);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_SENSORS_H */
