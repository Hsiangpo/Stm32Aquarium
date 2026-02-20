/**
 * @file aquarium_app.h
 * @brief 智能水族箱应用层编排器
 *
 * 将 aquarium_logic + aquarium_iotda 组装成可驱动的主循环内核：
 * - 传感器数据更新
 * - 周期性时间推进与告警/执行器计算
 * - 定时属性上报
 * - MQTT 命令处理
 */

#ifndef AQUARIUM_APP_H
#define AQUARIUM_APP_H

#include "aquarium_iotda.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 默认配置
 * ============================================================================
 */

#define DEFAULT_REPORT_INTERVAL_SECONDS 30

/* 连续 N 次采集失败/异常 -> 触发传感器故障告警 */
#define AQUA_APP_SENSOR_FAIL_THRESHOLD 3

/* ============================================================================
 * 设备 ID 最大长度
 * ============================================================================
 */

#define DEVICE_ID_MAX_LEN 64

/* ============================================================================
 * 应用层上下文
 * ============================================================================
 */

typedef struct {
  /* 设备标识 */
  char device_id[DEVICE_ID_MAX_LEN + 1];

  /* 设备状态（包含属性、阈值、配置、运行态） */
  AquariumState state;

  /* 传感器容错：连续失败/异常计数（达到阈值后置安全值并触发告警） */
  uint8_t sensor_fail_count_temp;
  uint8_t sensor_fail_count_ph;
  uint8_t sensor_fail_count_tds;
  uint8_t sensor_fail_count_turbidity;
  uint8_t sensor_fail_count_water_level;

  /* 上报配置 */
  uint32_t report_interval; /* 上报间隔（秒） */
  uint32_t report_timer;    /* 上报倒计时 */
} AquariumApp;

/* ============================================================================
 * 初始化
 * ============================================================================
 */

/**
 * @brief 初始化应用层上下文
 *
 * @param app       应用上下文指针
 * @param device_id 设备 ID
 */
void aqua_app_init(AquariumApp *app, const char *device_id);

/* ============================================================================
 * 配置
 * ============================================================================
 */

/**
 * @brief 设置属性上报间隔
 *
 * @param app              应用上下文指针
 * @param interval_seconds 上报间隔（秒），默认 30
 */
void aqua_app_set_report_interval(AquariumApp *app, uint32_t interval_seconds);

/* ============================================================================
 * 传感器数据更新
 * ============================================================================
 */

/**
 * @brief 更新传感器数据
 *
 * 由硬件驱动层调用，将最新传感器读数写入状态
 *
 * @param app         应用上下文指针
 * @param temperature 水温 ℃
 * @param ph          pH 值
 * @param tds         TDS 值 ppm
 * @param turbidity   浊度 NTU
 * @param water_level 水位 %
 */
void aqua_app_update_sensors(AquariumApp *app, float temperature, float ph,
                             float tds, float turbidity, float water_level);

/* ============================================================================
 * 主循环步进
 * ============================================================================
 */

/**
 * @brief 执行一次主循环步进
 *
 * 内部流程：
 * 1. 调用 aqua_logic_tick 推进投喂倒计时
 * 2. 调用 aqua_logic_eval_alarm 计算告警等级
 * 3. 调用 aqua_logic_compute_actuators 计算期望执行器状态
 * 4. 自动模式下将期望执行器状态回写到 state.props
 * 5. 检查上报周期，到期时生成属性上报
 *
 * @param app             应用上下文指针
 * @param elapsed_seconds 自上次调用以来经过的秒数
 * @param out_actuators   [输出] 期望执行器状态
 * @param out_has_publish [输出] 是否需要发布消息
 * @param out_topic       [输出] 发布 Topic 缓冲区
 * @param topic_size      Topic 缓冲区大小
 * @param out_payload     [输出] 发布 Payload 缓冲区
 * @param payload_size    Payload 缓冲区大小
 * @return AquaError 错误码
 */
AquaError aqua_app_step(AquariumApp *app, uint32_t elapsed_seconds,
                        ActuatorDesired *out_actuators, bool *out_has_publish,
                        char *out_topic, size_t topic_size, char *out_payload,
                        size_t payload_size);

/* ============================================================================
 * MQTT 命令处理
 * ============================================================================
 */

/**
 * @brief 处理收到的 MQTT 命令
 *
 * @param app              应用上下文指针
 * @param in_topic         输入命令 Topic
 * @param in_payload       输入命令 Payload
 * @param payload_len      Payload 长度
 * @param out_has_response [输出] 是否需要发送响应
 * @param out_topic        [输出] 响应 Topic 缓冲区
 * @param topic_size       Topic 缓冲区大小
 * @param out_payload      [输出] 响应 Payload 缓冲区
 * @param payload_size     Payload 缓冲区大小
 * @return AquaError 错误码
 */
AquaError aqua_app_on_mqtt_command(AquariumApp *app, const char *in_topic,
                                   const char *in_payload, size_t payload_len,
                                   bool *out_has_response, char *out_topic,
                                   size_t topic_size, char *out_payload,
                                   size_t payload_size);

/* ============================================================================
 * 状态访问（供外部查询）
 * ============================================================================
 */

/**
 * @brief 获取当前设备状态的只读指针
 */
const AquariumState *aqua_app_get_state(const AquariumApp *app);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_APP_H */
