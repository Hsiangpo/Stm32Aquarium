/**
 * @file aquarium_types.h
 * @brief 智能水族箱数据结构定义
 *
 * 定义与华为云 IoTDA 物模型对齐的数据结构：
 * - Aquarium 属性服务（13 个字段）
 * - aquarium_control 命令参数
 * - aquarium_threshold 命令参数
 * - aquariumConfig 命令参数
 *
 * 参考文档：docs/HuaweiCloud.MD
 */

#ifndef AQUARIUM_TYPES_H
#define AQUARIUM_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 服务 ID 常量（与 HuaweiCloud.MD 物模型一致）
 * ============================================================================
 */

#define SERVICE_ID_AQUARIUM "Aquarium"
#define SERVICE_ID_AQUARIUM_CONTROL "aquarium_control"
#define SERVICE_ID_AQUARIUM_THRESHOLD "aquarium_threshold"
#define SERVICE_ID_AQUARIUM_CONFIG "aquariumConfig"

/* ============================================================================
 * 命令名称常量
 * ============================================================================
 */

#define COMMAND_NAME_CONTROL "control"
#define COMMAND_NAME_SET_THRESHOLDS "set_thresholds"
#define COMMAND_NAME_SET_CONFIG "set_config"

/* ============================================================================
 * Aquarium 属性服务（13 个字段）
 * 用于周期性上报设备状态
 * ============================================================================
 */

typedef struct {
  /* 传感器数据 */
  float temperature; /* 水温 ℃ */
  float ph;          /* pH 值 */
  float tds;         /* TDS 值 ppm */
  float turbidity;   /* 浊度 NTU */
  float water_level; /* 水位 % */

  /* 执行器状态 */
  bool heater;   /* 加热棒状态 */
  bool pump_in;  /* 进水泵状态 */
  bool pump_out; /* 出水泵状态 */

  /* 运行模式 */
  bool auto_mode; /* 自动模式 */

  /* 投喂相关 */
  int32_t feed_countdown;   /* 距离下一次投喂的倒计时（秒） */
  bool feeding_in_progress; /* 设备是否正在执行投喂 */

  /* 告警相关 */
  int32_t alarm_level; /* 当前告警级别（0=正常, 1=警告, 2=严重） */
  bool alarm_muted;    /* 告警是否被静音 */
} AquariumProperties;

/* ============================================================================
 * aquarium_control 命令参数（control）
 * 用于远程控制设备执行器和模式
 * ============================================================================
 */

typedef struct {
  bool heater;       /* 开启/关闭加热棒 */
  bool pump_in;      /* 开启/关闭进水泵 */
  bool pump_out;     /* 开启/关闭排水泵 */
  bool mute;         /* 静音告警 */
  bool auto_mode;    /* 切换自动/手动模式 */
  bool feed;         /* 立即投喂一次 */
  int32_t feed_once_delay; /* 一次性投喂倒计时（秒） */
  float target_temp; /* 目标温度 ℃ */

  /* 字段存在标志（用于部分更新） */
  bool has_heater;
  bool has_pump_in;
  bool has_pump_out;
  bool has_mute;
  bool has_auto_mode;
  bool has_feed;
  bool has_feed_once_delay;
  bool has_target_temp;
} ControlCommandParams;

/* ============================================================================
 * aquarium_threshold 命令参数（set_thresholds）
 * 用于远程配置阈值参数
 * ============================================================================
 */

typedef struct {
  float temp_min;             /* 温度下限 ℃ */
  float temp_max;             /* 温度上限 ℃ */
  float ph_min;               /* pH 下限 */
  float ph_max;               /* pH 上限 */
  int32_t tds_warn;           /* TDS 警告阈值 ppm */
  int32_t tds_critical;       /* TDS 严重阈值 ppm */
  int32_t turbidity_warn;     /* 浊度警告阈值 NTU */
  int32_t turbidity_critical; /* 浊度严重阈值 NTU */
  int32_t level_min;          /* 水位下限 % */
  int32_t level_max;          /* 水位上限 % */
  int32_t feed_interval;      /* 自动投喂间隔（小时） */
  int32_t feed_amount;        /* 投喂量（档位） */

  /* 字段存在标志（用于部分更新） */
  bool has_temp_min;
  bool has_temp_max;
  bool has_ph_min;
  bool has_ph_max;
  bool has_tds_warn;
  bool has_tds_critical;
  bool has_turbidity_warn;
  bool has_turbidity_critical;
  bool has_level_min;
  bool has_level_max;
  bool has_feed_interval;
  bool has_feed_amount;
} ThresholdCommandParams;

/* ============================================================================
 * aquariumConfig 命令参数（set_config）
 * 用于远程配置 Wi-Fi 和传感器校准
 * ============================================================================
 */

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64

typedef struct {
  char wifi_ssid[WIFI_SSID_MAX_LEN + 1];         /* Wi-Fi 名称 */
  char wifi_password[WIFI_PASSWORD_MAX_LEN + 1]; /* Wi-Fi 密码 */
  float ph_offset;                               /* pH 校准偏移量 */
  float tds_factor;                              /* TDS 校准系数 */

  /* 字段存在标志（用于部分更新） */
  bool has_wifi_ssid;
  bool has_wifi_password;
  bool has_ph_offset;
  bool has_tds_factor;
} ConfigCommandParams;

/* ============================================================================
 * 命令类型枚举
 * ============================================================================
 */

typedef enum {
  COMMAND_TYPE_UNKNOWN = 0,
  COMMAND_TYPE_CONTROL,
  COMMAND_TYPE_SET_THRESHOLDS,
  COMMAND_TYPE_SET_CONFIG
} CommandType;

/* ============================================================================
 * 解析后的命令结构
 * ============================================================================
 */

typedef struct {
  CommandType type;
  char service_id[32];
  char command_name[32];

  union {
    ControlCommandParams control;
    ThresholdCommandParams threshold;
    ConfigCommandParams config;
  } params;
} ParsedCommand;

/* ============================================================================
 * 命令响应结构
 * ============================================================================
 */

typedef struct {
  int32_t result_code; /* 0=成功, 1=设备执行失败, 2=参数错误, 3=设备离线,
                          4=命令超时 */
  char response_name[32]; /* 响应名称，如 "control_response" */
  char result[16];        /* "success" 或 "failed" */
  char error[64];         /* 错误描述（仅失败时有效） */
  bool has_error;
} CommandResponse;

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_TYPES_H */
