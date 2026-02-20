/**
 * @file test_aquarium_protocol.c
 * @brief 智能水族箱协议核心库单元测试
 *
 * 使用 Unity 测试框架验证 JSON 编解码与字段一致性
 */

#include "aquarium_protocol.h"
#include <math.h>
#include <string.h>
#include <unity.h>


void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * 测试：属性上报 JSON 生成
 * ============================================================================
 */

void test_build_properties_json_basic(void) {
  AquariumProperties props = {.temperature = 26.5f,
                              .ph = 7.2f,
                              .tds = 350.0f,
                              .turbidity = 15.0f,
                              .water_level = 85.0f,
                              .heater = true,
                              .pump_in = false,
                              .pump_out = false,
                              .auto_mode = true,
                              .feed_countdown = 0,
                              .feeding_in_progress = false,
                              .alarm_level = 0,
                              .alarm_muted = false};

  char buffer[1024];
  size_t len = 0;

  AquaError err =
      aqua_build_properties_json(&props, buffer, sizeof(buffer), &len);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_GREATER_THAN(0, len);

  /* 验证 service_id */
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"service_id\":\"Aquarium\""));

  /* 验证 13 个属性字段存在 */
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"temperature\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"ph\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"tds\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"turbidity\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"water_level\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"heater\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"pump_in\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"pump_out\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"auto_mode\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"feed_countdown\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"feeding_in_progress\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"alarm_level\":"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"alarm_muted\":"));
}

void test_build_properties_json_no_nan_inf(void) {
  AquariumProperties props = {.temperature = NAN,
                              .ph = INFINITY,
                              .tds = -INFINITY,
                              .turbidity = NAN,
                              .water_level = INFINITY,
                              .heater = true,
                              .pump_in = false,
                              .pump_out = false,
                              .auto_mode = true,
                              .feed_countdown = 0,
                              .feeding_in_progress = false,
                              .alarm_level = 0,
                              .alarm_muted = false};

  char buffer[1024];
  size_t len = 0;

  AquaError err =
      aqua_build_properties_json(&props, buffer, sizeof(buffer), &len);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_GREATER_THAN(0, len);

  /* JSON 中不得出现 NaN/Inf */
  TEST_ASSERT_NULL(strstr(buffer, "nan"));
  TEST_ASSERT_NULL(strstr(buffer, "NaN"));
  TEST_ASSERT_NULL(strstr(buffer, "inf"));
  TEST_ASSERT_NULL(strstr(buffer, "Inf"));
  TEST_ASSERT_NULL(strstr(buffer, "INF"));
}

void test_build_properties_json_null_ptr(void) {
  char buffer[256];
  size_t len;
  AquariumProperties props = {0};

  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_build_properties_json(NULL, buffer, 256, &len));
  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_build_properties_json(&props, NULL, 256, &len));
  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_build_properties_json(&props, buffer, 256, NULL));
}

void test_build_properties_json_buffer_small(void) {
  AquariumProperties props = {0};
  char buffer[16];
  size_t len;

  AquaError err =
      aqua_build_properties_json(&props, buffer, sizeof(buffer), &len);
  TEST_ASSERT_EQUAL(AQUA_ERR_BUFFER_TOO_SMALL, err);
}

/* ============================================================================
 * 测试：命令响应 JSON 生成
 * ============================================================================
 */

void test_build_response_json_success(void) {
  CommandResponse resp = {.result_code = 0,
                          .response_name = "control_response",
                          .result = "success",
                          .has_error = false};

  char buffer[256];
  size_t len;

  AquaError err = aqua_build_response_json(&resp, buffer, sizeof(buffer), &len);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"result_code\":0"));
  TEST_ASSERT_NOT_NULL(
      strstr(buffer, "\"response_name\":\"control_response\""));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"result\":\"success\""));
  TEST_ASSERT_NULL(strstr(buffer, "\"error\":"));
}

void test_build_response_json_failure(void) {
  CommandResponse resp = {.result_code = 1,
                          .response_name = "control_response",
                          .result = "failed",
                          .error = "heater malfunction",
                          .has_error = true};

  char buffer[256];
  size_t len;

  AquaError err = aqua_build_response_json(&resp, buffer, sizeof(buffer), &len);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"result_code\":1"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"result\":\"failed\""));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"error\":\"heater malfunction\""));
}

/* ============================================================================
 * 测试：命令解析 - control
 * ============================================================================
 */

void test_parse_control_command(void) {
  const char *json =
      "{"
      "\"object_device_id\":\"690237639798273cc4fd09cb_MyAquarium_01\","
      "\"service_id\":\"aquarium_control\","
      "\"command_name\":\"control\","
      "\"paras\":{"
      "\"heater\":true,"
      "\"pump_in\":false,"
      "\"pump_out\":false,"
      "\"mute\":false,"
      "\"auto_mode\":true,"
      "\"feed\":false,"
      "\"feed_once_delay\":600,"
      "\"target_temp\":26.0"
      "}"
      "}";

  ParsedCommand cmd;
  AquaError err = aqua_parse_command_json(json, strlen(json), &cmd);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL(COMMAND_TYPE_CONTROL, cmd.type);
  TEST_ASSERT_EQUAL_STRING("aquarium_control", cmd.service_id);
  TEST_ASSERT_EQUAL_STRING("control", cmd.command_name);

  TEST_ASSERT_TRUE(cmd.params.control.has_heater);
  TEST_ASSERT_TRUE(cmd.params.control.heater);
  TEST_ASSERT_TRUE(cmd.params.control.has_pump_in);
  TEST_ASSERT_FALSE(cmd.params.control.pump_in);
  TEST_ASSERT_TRUE(cmd.params.control.has_auto_mode);
  TEST_ASSERT_TRUE(cmd.params.control.auto_mode);
  TEST_ASSERT_TRUE(cmd.params.control.has_feed_once_delay);
  TEST_ASSERT_EQUAL(600, cmd.params.control.feed_once_delay);
  TEST_ASSERT_TRUE(cmd.params.control.has_target_temp);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 26.0f, cmd.params.control.target_temp);        
}

/* ============================================================================
 * 测试：命令解析 - set_thresholds
 * ============================================================================
 */

void test_parse_threshold_command(void) {
  const char *json = "{"
                     "\"service_id\":\"aquarium_threshold\","
                     "\"command_name\":\"set_thresholds\","
                     "\"paras\":{"
                     "\"temp_min\":24.0,"
                     "\"temp_max\":28.0,"
                     "\"ph_min\":6.5,"
                     "\"ph_max\":7.5,"
                     "\"tds_warn\":500,"
                     "\"tds_critical\":800,"
                     "\"turbidity_warn\":30,"
                     "\"turbidity_critical\":50,"
                     "\"level_min\":20,"
                     "\"level_max\":95,"
                     "\"feed_interval\":12,"
                     "\"feed_amount\":2"
                     "}"
                     "}";

  ParsedCommand cmd;
  AquaError err = aqua_parse_command_json(json, strlen(json), &cmd);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL(COMMAND_TYPE_SET_THRESHOLDS, cmd.type);
  TEST_ASSERT_EQUAL_STRING("aquarium_threshold", cmd.service_id);
  TEST_ASSERT_EQUAL_STRING("set_thresholds", cmd.command_name);

  TEST_ASSERT_TRUE(cmd.params.threshold.has_temp_min);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 24.0f, cmd.params.threshold.temp_min);
  TEST_ASSERT_TRUE(cmd.params.threshold.has_tds_warn);
  TEST_ASSERT_EQUAL(500, cmd.params.threshold.tds_warn);
  TEST_ASSERT_TRUE(cmd.params.threshold.has_feed_interval);
  TEST_ASSERT_EQUAL(12, cmd.params.threshold.feed_interval);
}

/* ============================================================================
 * 测试：命令解析 - set_config
 * ============================================================================
 */

void test_parse_config_command(void) {
  const char *json = "{"
                     "\"service_id\":\"aquariumConfig\","
                     "\"command_name\":\"set_config\","
                     "\"paras\":{"
                     "\"wifi_ssid\":\"MyWiFi\","
                     "\"wifi_password\":\"password123\","
                     "\"ph_offset\":0.15,"
                     "\"tds_factor\":1.02"
                     "}"
                     "}";

  ParsedCommand cmd;
  AquaError err = aqua_parse_command_json(json, strlen(json), &cmd);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL(COMMAND_TYPE_SET_CONFIG, cmd.type);
  TEST_ASSERT_EQUAL_STRING("aquariumConfig", cmd.service_id);
  TEST_ASSERT_EQUAL_STRING("set_config", cmd.command_name);

  TEST_ASSERT_TRUE(cmd.params.config.has_wifi_ssid);
  TEST_ASSERT_EQUAL_STRING("MyWiFi", cmd.params.config.wifi_ssid);
  TEST_ASSERT_TRUE(cmd.params.config.has_wifi_password);
  TEST_ASSERT_EQUAL_STRING("password123", cmd.params.config.wifi_password);
  TEST_ASSERT_TRUE(cmd.params.config.has_ph_offset);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.15f, cmd.params.config.ph_offset);
}

/* ============================================================================
 * 测试：Topic 解析
 * ============================================================================
 */

void test_extract_request_id(void) {
  const char *topic = "$oc/devices/690237639798273cc4fd09cb_MyAquarium_01/sys/"
                      "commands/request_id=abc123";
  char request_id[64];

  AquaError err =
      aqua_extract_request_id(topic, request_id, sizeof(request_id));

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL_STRING("abc123", request_id);
}

void test_extract_request_id_invalid(void) {
  const char *topic = "$oc/devices/xxx/sys/properties/report";
  char request_id[64];

  AquaError err =
      aqua_extract_request_id(topic, request_id, sizeof(request_id));
  TEST_ASSERT_EQUAL(AQUA_ERR_TOPIC_PARSE, err);
}

/* ============================================================================
 * 测试：Topic 构建
 * ============================================================================
 */

void test_build_response_topic(void) {
  char buffer[256];
  size_t len;

  AquaError err =
      aqua_build_response_topic("690237639798273cc4fd09cb_MyAquarium_01",
                                "req123", buffer, sizeof(buffer), &len);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL_STRING("$oc/devices/690237639798273cc4fd09cb_MyAquarium_01/"
                           "sys/commands/response/request_id=req123",
                           buffer);
}

void test_build_report_topic(void) {
  char buffer[256];
  size_t len;

  AquaError err = aqua_build_report_topic(
      "690237639798273cc4fd09cb_MyAquarium_01", buffer, sizeof(buffer), &len);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL_STRING("$oc/devices/690237639798273cc4fd09cb_MyAquarium_01/"
                           "sys/properties/report",
                           buffer);
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  /* 属性上报测试 */
  RUN_TEST(test_build_properties_json_basic);
  RUN_TEST(test_build_properties_json_no_nan_inf);
  RUN_TEST(test_build_properties_json_null_ptr);
  RUN_TEST(test_build_properties_json_buffer_small);

  /* 命令响应测试 */
  RUN_TEST(test_build_response_json_success);
  RUN_TEST(test_build_response_json_failure);

  /* 命令解析测试 */
  RUN_TEST(test_parse_control_command);
  RUN_TEST(test_parse_threshold_command);
  RUN_TEST(test_parse_config_command);

  /* Topic 测试 */
  RUN_TEST(test_extract_request_id);
  RUN_TEST(test_extract_request_id_invalid);
  RUN_TEST(test_build_response_topic);
  RUN_TEST(test_build_report_topic);

  return UNITY_END();
}
