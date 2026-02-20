/**
 * @file aquarium_firmware.h
 * @brief 智能水族箱固件编排器
 *
 * 整合 AquariumApp 和 MqttClient，实现：
 * - 周期性属性上报
 * - 下行命令处理及响应
 * - 传感器数据更新驱动
 * - 执行器状态输出
 */

#ifndef AQUARIUM_FIRMWARE_H
#define AQUARIUM_FIRMWARE_H

#include "aquarium_app.h"
#include "aquarium_esp32_mqtt.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 执行器回调
 * ============================================================================
 */

/**
 * @brief 执行器控制回调函数类型
 *
 * 当业务逻辑计算出新的执行器状态时调用
 *
 * @param actuators 期望的执行器状态
 * @param user_data 用户自定义数据
 */
typedef void (*ActuatorCallback)(const ActuatorDesired *actuators,
                                 void *user_data);

/* ============================================================================
 * 固件上下文
 * ============================================================================
 */

typedef struct {
  AquariumApp *app;      /* 应用层指针 */
  MqttClient *mqtt;      /* MQTT 客户端指针 */
  uint32_t last_step_ms; /* 上次 step 的时间戳 */
  uint32_t subsec_ms;    /* 毫秒累计，用于在 <1s 的 loop 中也能推进 elapsed_seconds */

  /* 执行器回调 */
  ActuatorCallback actuator_cb;
  void *actuator_cb_data;
} AquaFirmware;

/* ============================================================================
 * 初始化
 * ============================================================================
 */

/**
 * @brief 初始化固件编排器
 *
 * @param fw   固件上下文指针
 * @param app  已初始化的应用层指针
 * @param mqtt 已初始化的 MQTT 客户端指针
 */
void aqua_fw_init(AquaFirmware *fw, AquariumApp *app, MqttClient *mqtt);

/**
 * @brief 设置执行器控制回调
 *
 * @param fw        固件上下文指针
 * @param cb        回调函数
 * @param user_data 用户自定义数据（传给回调）
 */
void aqua_fw_set_actuator_callback(AquaFirmware *fw, ActuatorCallback cb,
                                   void *user_data);

/* ============================================================================
 * 主循环
 * ============================================================================
 */

/**
 * @brief 执行一次固件主循环
 *
 * 流程：
 * 1. 推进 MQTT 状态机
 * 2. 如果 ONLINE，处理下行命令
 * 3. 无论网络状态如何，始终调用 app_step 推进业务逻辑
 * 4. 调用执行器回调输出期望状态
 * 5. 如果 ONLINE 且有上报数据，调用 mqtt_publish
 *
 * @param fw     固件上下文指针
 * @param now_ms 当前时间（毫秒），支持 32 位溢出
 */
void aqua_fw_step(AquaFirmware *fw, uint32_t now_ms);

/**
 * @brief 更新传感器数据
 *
 * 透传到 AquariumApp
 */
void aqua_fw_update_sensors(AquaFirmware *fw, float temperature, float ph,
                            float tds, float turbidity, float water_level);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_FIRMWARE_H */
