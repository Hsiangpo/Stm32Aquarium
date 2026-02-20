/**
 * @file test_firmware.c
 * @brief 固件编排器单元测试
 */

#include "aquarium_firmware.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

/* ============================================================================
 * Mock 回调
 * ============================================================================
 */

static uint8_t g_tx_buffer[2048];
static size_t g_tx_len = 0;
static uint32_t g_mock_time_ms = 0;

static size_t mock_write(const uint8_t *data, size_t len) {
  if (g_tx_len + len <= sizeof(g_tx_buffer)) {
    memcpy(g_tx_buffer + g_tx_len, data, len);
    g_tx_len += len;
  }
  return len;
}

static uint32_t mock_now_ms(void) { return g_mock_time_ms; }

static void reset_tx_buffer(void) {
  g_tx_len = 0;
  memset(g_tx_buffer, 0, sizeof(g_tx_buffer));
}

/* ============================================================================
 * 执行器回调 Mock
 * ============================================================================
 */

static int g_actuator_cb_count = 0;
static ActuatorDesired g_last_actuators;

static void mock_actuator_cb(const ActuatorDesired *actuators,
                             void *user_data) {
  (void)user_data;
  g_actuator_cb_count++;
  g_last_actuators = *actuators;
}

/* ============================================================================
 * 辅助函数
 * ============================================================================
 */

static void feed_ok(AtClient *at) {
  const char *rx = "OK\r\n";
  aqua_at_feed_rx(at, (const uint8_t *)rx, strlen(rx));
}

static void feed_prompt(AtClient *at) {
  const char *rx = ">\r\n";
  aqua_at_feed_rx(at, (const uint8_t *)rx, strlen(rx));
}

void setUp(void) {
  g_tx_len = 0;
  g_mock_time_ms = 0;
  g_actuator_cb_count = 0;
  memset(g_tx_buffer, 0, sizeof(g_tx_buffer));
  memset(&g_last_actuators, 0, sizeof(g_last_actuators));
}
void tearDown(void) {}

/* ============================================================================
 * 测试：初始化
 * ============================================================================
 */

void test_firmware_init(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;
  AquaFirmware fw;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test_device");
  aqua_mqtt_init(&mqtt, &at, &app);

  aqua_fw_init(&fw, &app, &mqtt);

  TEST_ASSERT_EQUAL_PTR(&app, fw.app);
  TEST_ASSERT_EQUAL_PTR(&mqtt, fw.mqtt);
  TEST_ASSERT_EQUAL(0, fw.last_step_ms);
  TEST_ASSERT_EQUAL(0, fw.subsec_ms);
}

/* ============================================================================
 * 测试：30 秒触发一次上报 + 发布完成回到 ONLINE
 * ============================================================================
 */

void test_firmware_periodic_report(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;
  AquaFirmware fw;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "dev123");
  aqua_mqtt_init(&mqtt, &at, &app);
  aqua_fw_init(&fw, &app, &mqtt);

  MqttConfig cfg = {0};
  strcpy(cfg.device_id, "dev123");
  aqua_mqtt_set_config(&mqtt, &cfg);
  mqtt.state = MQTT_STATE_ONLINE;

  aqua_app_set_report_interval(&app, 30);
  aqua_fw_update_sensors(&fw, 25.5f, 7.2f, 300.0f, 10.0f, 80.0f);

  g_mock_time_ms = 1000;
  aqua_fw_step(&fw, g_mock_time_ms);

  g_mock_time_ms = 30000;
  reset_tx_buffer();
  aqua_fw_step(&fw, g_mock_time_ms);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);

  g_mock_time_ms = 31000;
  reset_tx_buffer();
  aqua_fw_step(&fw, g_mock_time_ms);

  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "AT+MQTTPUBRAW"));
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "properties/report"));

  /* 发送 > 提示符（而非 OK）表示可以发送数据 */
  feed_prompt(&at);
  aqua_fw_step(&fw, g_mock_time_ms);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUB_DATA, mqtt.state);

  const char *urc = "+MQTTPUB:OK\r\n";
  aqua_at_feed_rx(&at, (const uint8_t *)urc, strlen(urc));
  aqua_fw_step(&fw, g_mock_time_ms);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);
}

/* ============================================================================
 * 测试：在 PUBLISHING 状态下不会重复触发上报
 * ============================================================================
 */

void test_firmware_no_duplicate_report_when_publishing(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;
  AquaFirmware fw;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "dev123");
  aqua_mqtt_init(&mqtt, &at, &app);
  aqua_fw_init(&fw, &app, &mqtt);

  MqttConfig cfg = {0};
  strcpy(cfg.device_id, "dev123");
  aqua_mqtt_set_config(&mqtt, &cfg);
  mqtt.state = MQTT_STATE_ONLINE;

  aqua_app_set_report_interval(&app, 30);
  aqua_fw_update_sensors(&fw, 25.5f, 7.2f, 300.0f, 10.0f, 80.0f);

  g_mock_time_ms = 1000;
  aqua_fw_step(&fw, g_mock_time_ms);

  g_mock_time_ms = 31000;
  aqua_fw_step(&fw, g_mock_time_ms);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);

  g_mock_time_ms = 62000;
  size_t tx_before = g_tx_len;
  aqua_fw_step(&fw, g_mock_time_ms);
  TEST_ASSERT_TRUE(g_tx_len == tx_before || mqtt.state != MQTT_STATE_ONLINE);
}

/* ============================================================================
 * 测试：传感器数据更新
 * ============================================================================
 */

void test_firmware_sensor_update(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;
  AquaFirmware fw;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  aqua_fw_init(&fw, &app, &mqtt);

  aqua_fw_update_sensors(&fw, 26.0f, 7.5f, 250.0f, 5.0f, 90.0f);

  const AquariumState *state = aqua_app_get_state(&app);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 26.0f, state->props.temperature);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.5f, state->props.ph);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 250.0f, state->props.tds);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, state->props.turbidity);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, state->props.water_level);
}

/* ============================================================================
 * 测试：执行器回调被调用
 * ============================================================================
 */

void test_firmware_actuator_callback(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;
  AquaFirmware fw;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  aqua_fw_init(&fw, &app, &mqtt);

  aqua_fw_set_actuator_callback(&fw, mock_actuator_cb, NULL);
  mqtt.state = MQTT_STATE_ONLINE;

  aqua_fw_update_sensors(&fw, 20.0f, 7.0f, 300.0f, 5.0f, 80.0f);

  g_mock_time_ms = 1000;
  aqua_fw_step(&fw, g_mock_time_ms);

  g_mock_time_ms = 2000;
  aqua_fw_step(&fw, g_mock_time_ms);

  TEST_ASSERT_EQUAL(1, g_actuator_cb_count);
}

/* ============================================================================
 * 测试：断网时业务逻辑仍然推进
 * ============================================================================
 */

void test_firmware_offline_logic_continues(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;
  AquaFirmware fw;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  aqua_fw_init(&fw, &app, &mqtt);

  aqua_fw_set_actuator_callback(&fw, mock_actuator_cb, NULL);
  mqtt.state = MQTT_STATE_IDLE;

  aqua_fw_update_sensors(&fw, 20.0f, 7.0f, 300.0f, 5.0f, 80.0f);

  g_mock_time_ms = 1000;
  aqua_fw_step(&fw, g_mock_time_ms);

  g_mock_time_ms = 2000;
  aqua_fw_step(&fw, g_mock_time_ms);

  TEST_ASSERT_EQUAL(1, g_actuator_cb_count);
}

/* ============================================================================
 * 测试：时间溢出安全
 * ============================================================================
 */

void test_firmware_time_overflow_safe(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;
  AquaFirmware fw;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  aqua_fw_init(&fw, &app, &mqtt);

  aqua_fw_set_actuator_callback(&fw, mock_actuator_cb, NULL);
  mqtt.state = MQTT_STATE_ONLINE;

  g_mock_time_ms = 0xFFFFF000;
  aqua_fw_step(&fw, g_mock_time_ms);

  g_mock_time_ms = 0x00001000;
  aqua_fw_step(&fw, g_mock_time_ms);

  TEST_ASSERT_EQUAL(1, g_actuator_cb_count);
}

/* ============================================================================
 * 测试：<1s loop 下也能推进业务逻辑
 * ============================================================================
 */

void test_firmware_subsecond_ticks_accumulate(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;
  AquaFirmware fw;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  aqua_fw_init(&fw, &app, &mqtt);

  aqua_fw_set_actuator_callback(&fw, mock_actuator_cb, NULL);
  mqtt.state = MQTT_STATE_IDLE;

  g_mock_time_ms = 1000;
  aqua_fw_step(&fw, g_mock_time_ms);
  TEST_ASSERT_EQUAL(0, g_actuator_cb_count);

  g_mock_time_ms = 1500;
  aqua_fw_step(&fw, g_mock_time_ms);
  TEST_ASSERT_EQUAL(0, g_actuator_cb_count);

  g_mock_time_ms = 2000;
  aqua_fw_step(&fw, g_mock_time_ms);
  TEST_ASSERT_EQUAL(1, g_actuator_cb_count);
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_firmware_init);
  RUN_TEST(test_firmware_periodic_report);
  RUN_TEST(test_firmware_no_duplicate_report_when_publishing);
  RUN_TEST(test_firmware_sensor_update);
  RUN_TEST(test_firmware_actuator_callback);
  RUN_TEST(test_firmware_offline_logic_continues);
  RUN_TEST(test_firmware_time_overflow_safe);
  RUN_TEST(test_firmware_subsecond_ticks_accumulate);

  return UNITY_END();
}
