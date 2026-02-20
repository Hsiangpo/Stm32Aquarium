/**
 * @file test_aquarium_app.c
 * @brief 智能水族箱应用层编排器单元测试
 */

#include "aquarium_app.h"
#include <math.h>
#include <string.h>
#include <unity.h>

#define TEST_DEVICE_ID "690237639798273cc4fd09cb_MyAquarium_01"

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * 测试：初始化
 * ============================================================================
 */

void test_app_init_defaults(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);

  TEST_ASSERT_EQUAL_STRING(TEST_DEVICE_ID, app.device_id);
  TEST_ASSERT_EQUAL(DEFAULT_REPORT_INTERVAL_SECONDS, app.report_interval);
  TEST_ASSERT_EQUAL(DEFAULT_REPORT_INTERVAL_SECONDS, app.report_timer);
  TEST_ASSERT_TRUE(app.state.props.auto_mode);
}

/* ============================================================================
 * 测试：设置上报间隔
 * ============================================================================
 */

void test_set_report_interval(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);

  aqua_app_set_report_interval(&app, 60);

  TEST_ASSERT_EQUAL(60, app.report_interval);
  TEST_ASSERT_EQUAL(60, app.report_timer);
}

/* ============================================================================
 * 测试：传感器数据更新
 * ============================================================================
 */

void test_update_sensors(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);

  aqua_app_update_sensors(&app, 25.5f, 7.0f, 400.0f, 20.0f, 80.0f);

  const AquariumState *state = aqua_app_get_state(&app);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.5f, state->props.temperature);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 7.0f, state->props.ph);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 400.0f, state->props.tds);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, state->props.turbidity);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 80.0f, state->props.water_level);
}

/* ============================================================================
 * 测试：传感器校准系数应用
 * ============================================================================
 */

void test_sensor_calibration(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);

  /* 设置校准系数 */
  app.state.config.ph_offset = 0.5f;
  app.state.config.tds_factor = 1.2f;

  /* 输入原始值 */
  aqua_app_update_sensors(&app, 25.0f, 7.0f, 400.0f, 20.0f, 80.0f);

  const AquariumState *state = aqua_app_get_state(&app);
  /* pH 应加偏移: 7.0 + 0.5 = 7.5 */
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.5f, state->props.ph);
  /* TDS 应乘系数: 400 * 1.2 = 480 */
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 480.0f, state->props.tds);
  /* 其他值不受影响 */
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, state->props.temperature);
}

static bool test_is_finitef(float v) { return (v == v) && ((v - v) == 0.0f); }

/* ============================================================================
 * 测试：传感器连续失败阈值触发/恢复（含 pH）
 * ============================================================================
 */

void test_sensor_fault_threshold_trigger_and_recover(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);
  aqua_app_set_report_interval(&app, 1);

  char topic[256], payload[1024];
  ActuatorDesired actuators;
  bool has_publish;

  /* 先写入一个有效 pH（故意不等于安全默认值 7.0，方便验证阈值触发后会回退） */
  aqua_app_update_sensors(&app, 26.0f, 7.4f, 300.0f, 15.0f, 50.0f);
  AquaError err = aqua_app_step(&app, 1, &actuators, &has_publish, topic,
                                sizeof(topic), payload, sizeof(payload));
  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL(0, app.state.props.alarm_level);
  TEST_ASSERT_EQUAL(0u, app.state.sensor_fault_mask);

  /* 连续 N-1 次异常：不应触发故障告警 */
  for (int i = 0; i < AQUA_APP_SENSOR_FAIL_THRESHOLD - 1; i++) {
    aqua_app_update_sensors(&app, 26.0f, NAN, 300.0f, 15.0f, 50.0f);
    err = aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                        payload, sizeof(payload));
    TEST_ASSERT_EQUAL(AQUA_OK, err);
    TEST_ASSERT_EQUAL(0, app.state.props.alarm_level);
    TEST_ASSERT_EQUAL(0u, app.state.sensor_fault_mask);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.4f, app.state.props.ph);
  }

  /* 第 N 次异常：触发故障告警 + 使用安全默认值（默认阈值下为 7.0） */
  aqua_app_update_sensors(&app, 26.0f, NAN, 300.0f, 15.0f, 50.0f);
  err = aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                      payload, sizeof(payload));
  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL(1, app.state.props.alarm_level);
  TEST_ASSERT_TRUE(app.state.sensor_fault_mask != 0u);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, app.state.props.ph);

  /* 上报 JSON 不得出现 NaN/Inf */
  TEST_ASSERT_TRUE(has_publish);
  TEST_ASSERT_NULL(strstr(payload, "nan"));
  TEST_ASSERT_NULL(strstr(payload, "NaN"));
  TEST_ASSERT_NULL(strstr(payload, "inf"));
  TEST_ASSERT_NULL(strstr(payload, "Inf"));
  TEST_ASSERT_NULL(strstr(payload, "INF"));

  /* 状态里的传感器值也必须是有限数 */
  TEST_ASSERT_TRUE(test_is_finitef(app.state.props.temperature));
  TEST_ASSERT_TRUE(test_is_finitef(app.state.props.ph));
  TEST_ASSERT_TRUE(test_is_finitef(app.state.props.tds));
  TEST_ASSERT_TRUE(test_is_finitef(app.state.props.turbidity));
  TEST_ASSERT_TRUE(test_is_finitef(app.state.props.water_level));

  /* 恢复：下一次有效采集应清除故障告警 */
  aqua_app_update_sensors(&app, 26.0f, 7.4f, 300.0f, 15.0f, 50.0f);
  err = aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                      payload, sizeof(payload));
  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL(0, app.state.props.alarm_level);
  TEST_ASSERT_EQUAL(0u, app.state.sensor_fault_mask);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.4f, app.state.props.ph);
}

/* ============================================================================
 * 测试：30 秒周期上报触发
 * ============================================================================
 */

void test_report_triggered_after_interval(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);
  aqua_app_set_report_interval(&app, 30);

  /* 模拟正常传感器值 */
  aqua_app_update_sensors(&app, 26.0f, 7.0f, 300.0f, 15.0f, 50.0f);

  char topic[256], payload[1024];
  ActuatorDesired actuators;
  bool has_publish;

  /* 第一次步进：29 秒，不应触发上报 */
  AquaError err = aqua_app_step(&app, 29, &actuators, &has_publish, topic,
                                sizeof(topic), payload, sizeof(payload));
  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_FALSE(has_publish);
  TEST_ASSERT_EQUAL(1, app.report_timer);

  /* 第二次步进：1 秒，应触发上报 */
  err = aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                      payload, sizeof(payload));
  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_TRUE(has_publish);

  /* 验证上报内容 */
  TEST_ASSERT_NOT_NULL(strstr(topic, "/sys/properties/report"));
  TEST_ASSERT_NOT_NULL(strstr(payload, "\"service_id\":\"Aquarium\""));

  /* 计时器应已重置 */
  TEST_ASSERT_EQUAL(30, app.report_timer);
}

/* ============================================================================
 * 测试：命令响应生成
 * ============================================================================
 */

void test_command_response_generated(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);

  const char *cmd_topic =
      "$oc/devices/" TEST_DEVICE_ID "/sys/commands/request_id=cmd001";
  const char *cmd_payload = "{\"service_id\":\"aquarium_control\","
                            "\"command_name\":\"control\","
                            "\"paras\":{\"heater\":true}}";

  char resp_topic[256], resp_payload[1024];
  bool has_response;

  AquaError err = aqua_app_on_mqtt_command(
      &app, cmd_topic, cmd_payload, strlen(cmd_payload), &has_response,
      resp_topic, sizeof(resp_topic), resp_payload, sizeof(resp_payload));

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_TRUE(has_response);

  /* 验证响应 Topic */
  TEST_ASSERT_NOT_NULL(strstr(resp_topic, "request_id=cmd001"));
  TEST_ASSERT_NOT_NULL(strstr(resp_topic, "/response/"));

  /* 验证响应 Payload */
  TEST_ASSERT_NOT_NULL(strstr(resp_payload, "\"result_code\":0"));
  TEST_ASSERT_NOT_NULL(strstr(resp_payload, "\"result\":\"success\""));

  /* 验证状态已更新 */
  TEST_ASSERT_TRUE(app.state.props.heater);
}

/* ============================================================================
 * 测试：告警导致 buzzer/led 输出变化
 * ============================================================================
 */

void test_alarm_affects_buzzer_led(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);

  char topic[256], payload[1024];
  ActuatorDesired actuators;
  bool has_publish;

  /* 正常情况：无告警 */
  aqua_app_update_sensors(&app, 26.0f, 7.0f, 300.0f, 15.0f, 50.0f);
  aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                payload, sizeof(payload));

  TEST_ASSERT_FALSE(actuators.buzzer);
  TEST_ASSERT_FALSE(actuators.led);
  TEST_ASSERT_EQUAL(0, app.state.props.alarm_level);

  /* 触发警告（TDS >= tds_warn） */
  aqua_app_update_sensors(&app, 26.0f, 7.0f, 500.0f, 15.0f, 50.0f);
  aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                payload, sizeof(payload));

  TEST_ASSERT_TRUE(actuators.buzzer);
  TEST_ASSERT_TRUE(actuators.led);
  TEST_ASSERT_EQUAL(1, app.state.props.alarm_level);

  /* 静音后蜂鸣器关闭，LED 仍亮 */
  app.state.props.alarm_muted = true;
  aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                payload, sizeof(payload));

  TEST_ASSERT_FALSE(actuators.buzzer);
  TEST_ASSERT_TRUE(actuators.led);
  TEST_ASSERT_EQUAL(1, app.state.props.alarm_level); /* 等级不变 */
}

/* ============================================================================
 * 测试：严重告警（温度过低）
 * ============================================================================
 */

void test_critical_alarm_temp_low(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);

  char topic[256], payload[1024];
  ActuatorDesired actuators;
  bool has_publish;

  /* 温度过低触发严重告警 */
  aqua_app_update_sensors(&app, 20.0f, 7.0f, 300.0f, 15.0f, 50.0f);
  aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                payload, sizeof(payload));

  TEST_ASSERT_TRUE(actuators.buzzer);
  TEST_ASSERT_TRUE(actuators.led);
  TEST_ASSERT_EQUAL(2, app.state.props.alarm_level);

  /* 自动模式下加热棒应开启 */
  TEST_ASSERT_TRUE(actuators.heater);
}

/* ============================================================================
 * 测试：自动模式下泵互斥
 * ============================================================================
 */

void test_pump_mutual_exclusion_in_auto_mode(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);

  char topic[256], payload[1024];
  ActuatorDesired actuators;
  bool has_publish;

  /* 水位过低，pump_in 应开启 */
  aqua_app_update_sensors(&app, 26.0f, 7.0f, 300.0f, 15.0f, 10.0f);
  aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                payload, sizeof(payload));

  TEST_ASSERT_TRUE(actuators.pump_in);
  TEST_ASSERT_FALSE(actuators.pump_out);

  /* 验证 props 也被同步（自动模式下回写） */
  TEST_ASSERT_TRUE(app.state.props.pump_in);
  TEST_ASSERT_FALSE(app.state.props.pump_out);

  /* 水位过高，pump_out 应开启 */
  aqua_app_update_sensors(&app, 26.0f, 7.0f, 300.0f, 15.0f, 98.0f);
  aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                payload, sizeof(payload));

  TEST_ASSERT_FALSE(actuators.pump_in);
  TEST_ASSERT_TRUE(actuators.pump_out);

  /* 水位正常 */
  aqua_app_update_sensors(&app, 26.0f, 7.0f, 300.0f, 15.0f, 50.0f);
  aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                payload, sizeof(payload));

  TEST_ASSERT_FALSE(actuators.pump_in);
  TEST_ASSERT_FALSE(actuators.pump_out);
}

/* ============================================================================
 * 测试：手动模式下不自动回写 props
 * ============================================================================
 */

void test_manual_mode_no_auto_writeback(void) {
  AquariumApp app;
  aqua_app_init(&app, TEST_DEVICE_ID);

  /* 切换到手动模式 */
  app.state.props.auto_mode = false;
  app.state.props.heater = false;
  app.state.props.pump_in = true;
  app.state.props.pump_out = true;

  char topic[256], payload[1024];
  ActuatorDesired actuators;
  bool has_publish;

  aqua_app_update_sensors(&app, 20.0f, 7.0f, 300.0f, 15.0f, 50.0f);
  aqua_app_step(&app, 1, &actuators, &has_publish, topic, sizeof(topic),
                payload, sizeof(payload));

  /* 手动模式下执行器输出应与 props 一致 */
  TEST_ASSERT_FALSE(actuators.heater);
  TEST_ASSERT_TRUE(actuators.pump_in);
  TEST_ASSERT_TRUE(actuators.pump_out);

  /* props 不应被自动修改 */
  TEST_ASSERT_FALSE(app.state.props.heater);
}

/* ============================================================================
 * 测试：空指针处理
 * ============================================================================
 */

void test_step_null_ptr(void) {
  AquariumApp app;
  char topic[256], payload[1024];
  ActuatorDesired actuators;
  bool has_publish;

  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_app_step(NULL, 1, &actuators, &has_publish, topic, 256,
                                  payload, 1024));
}

void test_command_null_ptr(void) {
  AquariumApp app;
  char topic[256], payload[1024];
  bool has_response;

  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_app_on_mqtt_command(NULL, "topic", "payload", 7,
                                             &has_response, topic, 256, payload,
                                             1024));
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  /* 初始化测试 */
  RUN_TEST(test_app_init_defaults);
  RUN_TEST(test_set_report_interval);
  RUN_TEST(test_update_sensors);
  RUN_TEST(test_sensor_calibration);
  RUN_TEST(test_sensor_fault_threshold_trigger_and_recover);

  /* 周期上报测试 */
  RUN_TEST(test_report_triggered_after_interval);

  /* 命令响应测试 */
  RUN_TEST(test_command_response_generated);

  /* 告警测试 */
  RUN_TEST(test_alarm_affects_buzzer_led);
  RUN_TEST(test_critical_alarm_temp_low);

  /* 泵互斥测试 */
  RUN_TEST(test_pump_mutual_exclusion_in_auto_mode);

  /* 手动模式测试 */
  RUN_TEST(test_manual_mode_no_auto_writeback);

  /* 空指针测试 */
  RUN_TEST(test_step_null_ptr);
  RUN_TEST(test_command_null_ptr);

  return UNITY_END();
}
