/**
 * @file aquarium_firmware.c
 * @brief 智能水族箱固件编排器实现
 */

#include "aquarium_firmware.h"
#include <string.h>

/* ============================================================================
 * 初始化
 * ============================================================================
 */

void aqua_fw_init(AquaFirmware *fw, AquariumApp *app, MqttClient *mqtt) {
  if (!fw)
    return;
  memset(fw, 0, sizeof(AquaFirmware));
  fw->app = app;
  fw->mqtt = mqtt;
  fw->last_step_ms = 0;
  fw->subsec_ms = 0;
  fw->actuator_cb = NULL;
  fw->actuator_cb_data = NULL;
}

void aqua_fw_set_actuator_callback(AquaFirmware *fw, ActuatorCallback cb,
                                   void *user_data) {
  if (!fw)
    return;
  fw->actuator_cb = cb;
  fw->actuator_cb_data = user_data;
}

/* ============================================================================
 * 主循环
 * ============================================================================
 */

void aqua_fw_step(AquaFirmware *fw, uint32_t now_ms) {
  if (!fw || !fw->app || !fw->mqtt)
    return;

  /* 1. 推进 MQTT 状态机 */
  aqua_mqtt_step(fw->mqtt);

  MqttConnState mqtt_state = aqua_mqtt_get_state(fw->mqtt);

  /* 2. 如果 ONLINE，处理下行命令 */
  if (mqtt_state == MQTT_STATE_ONLINE) {
    aqua_mqtt_poll_commands(fw->mqtt);
    /* poll_commands 可能触发 publish，重新获取状态 */
    mqtt_state = aqua_mqtt_get_state(fw->mqtt);
  }

  /* 3. 如果处于 AP 配网等待状态，处理 HTTP 请求 */
  if (mqtt_state == MQTT_STATE_AP_WAIT) {
    aqua_mqtt_poll_ap_config(fw->mqtt);
    mqtt_state = aqua_mqtt_get_state(fw->mqtt);
  }

  /* 3. 计算经过的秒数（溢出安全：无符号减法自动处理 32 位回绕） */
  uint32_t elapsed_ms = 0;
  if (fw->last_step_ms > 0) {
    /* 无符号减法：即使 now_ms < last_step_ms（溢出），结果也是正确的差值 */
    elapsed_ms = now_ms - fw->last_step_ms;
  }
  fw->last_step_ms = now_ms;

  /* 累计毫秒，避免 <1s 的 loop 永远无法推进 elapsed_seconds */
  uint64_t total_ms = (uint64_t)fw->subsec_ms + elapsed_ms;
  uint32_t elapsed_seconds = (uint32_t)(total_ms / 1000);
  fw->subsec_ms = (uint32_t)(total_ms % 1000);

  /* 4. 无论网络状态如何，始终推进业务逻辑（投喂倒计时、告警、执行器计算） */
  if (elapsed_seconds > 0) {
    ActuatorDesired actuators;
    bool has_publish = false;
    char topic[MQTT_TOPIC_MAX_LEN];
    char payload[MQTT_PAYLOAD_MAX_LEN];

    AquaError err =
        aqua_app_step(fw->app, elapsed_seconds, &actuators, &has_publish, topic,
                      sizeof(topic), payload, sizeof(payload));

    /* 5. 输出执行器状态到硬件（通过回调） */
    if (err == AQUA_OK && fw->actuator_cb) {
      fw->actuator_cb(&actuators, fw->actuator_cb_data);
    }

    /* 6. 如果 ONLINE 且有上报数据，调用 mqtt_publish */
    if (err == AQUA_OK && has_publish && mqtt_state == MQTT_STATE_ONLINE) {
      aqua_mqtt_publish(fw->mqtt, topic, payload, strlen(payload));
    }
  }
}

/* ============================================================================
 * 传感器更新
 * ============================================================================
 */

void aqua_fw_update_sensors(AquaFirmware *fw, float temperature, float ph,
                            float tds, float turbidity, float water_level) {
  if (!fw || !fw->app)
    return;
  aqua_app_update_sensors(fw->app, temperature, ph, tds, turbidity,
                          water_level);
}
