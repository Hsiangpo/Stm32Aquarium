/**
 * @file aquarium_logic.h
 * @brief 智能水族箱核心业务逻辑模块
 *
 * 设备侧业务逻辑（不依赖硬件）：
 * - 状态管理与命令应用
 * - 告警等级计算
 * - 投喂倒计时与状态机
 * - 执行器期望状态计算
 */

#ifndef AQUARIUM_LOGIC_H
#define AQUARIUM_LOGIC_H

#include "aquarium_protocol.h"
#include "aquarium_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 默认阈值常量
 * ============================================================================
 */

#define DEFAULT_TEMP_MIN 24.0f
#define DEFAULT_TEMP_MAX 28.0f
#define DEFAULT_PH_MIN 6.5f
#define DEFAULT_PH_MAX 7.5f
#define DEFAULT_TDS_WARN 500
#define DEFAULT_TDS_CRITICAL 800
#define DEFAULT_TURBIDITY_WARN 30
#define DEFAULT_TURBIDITY_CRITICAL 50
#define DEFAULT_LEVEL_MIN 20
#define DEFAULT_LEVEL_MAX 95
#define DEFAULT_FEED_INTERVAL 12 /* 小时 */
#define DEFAULT_FEED_AMOUNT 2    /* 档位 */
#define DEFAULT_TARGET_TEMP 26.0f

/* 投喂持续时间（秒） */
#define FEEDING_DURATION_SECONDS 5
/* 一次性倒计时投喂最大等待秒数（24h） */
#define FEED_ONCE_DELAY_MAX_SECONDS (24 * 3600)
/* 一次性投喂倒计时禁用值 */
#define FEED_ONCE_TIMER_INACTIVE (-1)

/* ============================================================================
 * 阈值配置结构（从 ThresholdCommandParams 持久化）
 * ============================================================================
 */

typedef struct {
  float temp_min;
  float temp_max;
  float ph_min;
  float ph_max;
  int32_t tds_warn;
  int32_t tds_critical;
  int32_t turbidity_warn;
  int32_t turbidity_critical;
  int32_t level_min;
  int32_t level_max;
  int32_t feed_interval; /* 小时 */
  int32_t feed_amount;   /* 档位 */
} ThresholdConfig;

/* ============================================================================
 * 设备配置结构（从 ConfigCommandParams 持久化）
 * ============================================================================
 */

typedef struct {
  char wifi_ssid[WIFI_SSID_MAX_LEN + 1];
  char wifi_password[WIFI_PASSWORD_MAX_LEN + 1];
  float ph_offset;
  float tds_factor;
} DeviceConfig;

/* ============================================================================
 * 设备完整状态
 * ============================================================================
 */

typedef struct {
  /* 实时属性（传感器读数 + 执行器状态） */
  AquariumProperties props;

  /* 阈值配置 */
  ThresholdConfig thresholds;

  /* 设备配置 */
  DeviceConfig config;

  /* 运行态 */
  float target_temp;     /* 目标温度 */
  int32_t feed_timer;    /* 投喂倒计时剩余秒数 */
  int32_t feed_once_timer; /* 一次性投喂倒计时（秒，-1 表示未预约） */
  int32_t feeding_timer; /* 投喂进行中剩余秒数 */
  bool config_dirty;     /* 配置已更改，需要持久化 */
  uint32_t sensor_fault_mask; /* 传感器故障位（内部使用） */
} AquariumState;

/* ============================================================================
 * 执行器期望状态
 * ============================================================================
 */

typedef struct {
  bool heater;   /* 期望加热棒状态 */
  bool pump_in;  /* 期望进水泵状态 */
  bool pump_out; /* 期望出水泵状态 */
  bool buzzer;   /* 期望蜂鸣器状态（受 alarm_muted 影响） */
  bool led;      /* 期望 LED 状态 */
} ActuatorDesired;

/* ============================================================================
 * 初始化
 * ============================================================================
 */

/**
 * @brief 初始化设备状态，装载默认值
 * @param state 设备状态指针
 */
void aqua_logic_init(AquariumState *state);

/* ============================================================================
 * 命令应用
 * ============================================================================
 */

/**
 * @brief 应用解析后的命令到设备状态
 *
 * 支持三类命令：control / set_thresholds / set_config
 * 仅更新命令中 has_* 为 true 的字段
 *
 * @param state 设备状态指针
 * @param cmd   解析后的命令
 * @return AquaError 错误码
 */
AquaError aqua_logic_apply_command(AquariumState *state,
                                   const ParsedCommand *cmd);

/* ============================================================================
 * 时间推进
 * ============================================================================
 */

/**
 * @brief 推进时间，更新倒计时和投喂状态
 *
 * - 减少 feed_timer，到 0 时触发投喂
 * - 投喂进行中时减少 feeding_timer，到 0 时结束投喂并重置 feed_timer
 * - 更新 props.feed_countdown 和 props.feeding_in_progress
 *
 * @param state           设备状态指针
 * @param elapsed_seconds 自上次调用以来经过的秒数
 */
void aqua_logic_tick(AquariumState *state, uint32_t elapsed_seconds);

/* ============================================================================
 * 告警等级计算
 * ============================================================================
 */

/**
 * @brief 根据当前传感器值和阈值计算告警等级
 *
 * 规则：
 * - 等级 0：正常
 * - 等级 1（警告）：tds >= tds_warn 或 turbidity >= turbidity_warn 或传感器故障（sensor_fault_mask != 0）
 * - 等级 2（严重）：temp < temp_min 或 temp > temp_max 或
 *                   ph < ph_min 或 ph > ph_max 或
 *                   water_level < level_min 或 water_level > level_max 或
 *                   tds >= tds_critical 或 turbidity >= turbidity_critical
 *
 * 注意：alarm_muted 只影响蜂鸣器输出，不影响 alarm_level
 *
 * @param state 设备状态指针
 * @return 计算后的告警等级（0/1/2），同时更新 state->props.alarm_level
 */
int32_t aqua_logic_eval_alarm(AquariumState *state);

/* ============================================================================
 * 执行器期望状态计算
 * ============================================================================
 */

/**
 * @brief 根据设备状态计算执行器期望状态
 *
 * 规则：
 * - auto_mode 为 true 时：
 *   - 温度 < target_temp - 0.5 时开启 heater，> target_temp + 0.5 时关闭
 *   - 水位 < level_min 时开启 pump_in，> level_max 时开启 pump_out
 *   - pump_in 和 pump_out 互斥（同时需要时 pump_in 优先）
 * - auto_mode 为 false 时：直接使用 props 中的当前状态
 * - buzzer 在 alarm_level > 0 且 !alarm_muted 时开启
 * - led 在 alarm_level > 0 时开启
 * - alarm_level >= 2 时进入紧急策略：停止换水泵；温度过高则关闭加热，否则保持加热
 *
 * @param state   设备状态指针
 * @param desired [输出] 期望执行器状态
 */
void aqua_logic_compute_actuators(const AquariumState *state,
                                  ActuatorDesired *desired);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_LOGIC_H */
