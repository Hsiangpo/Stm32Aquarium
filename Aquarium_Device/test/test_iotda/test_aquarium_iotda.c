/**
 * @file test_aquarium_iotda.c
 * @brief 华为云 IoTDA 适配层单元测试
 */

#include "aquarium_iotda.h"
#include <string.h>
#include <unity.h>


/* 测试用设备 ID（不含真实密钥） */
#define TEST_DEVICE_ID "690237639798273cc4fd09cb_MyAquarium_01"

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * 测试：属性上报生成
 * ============================================================================
 */

void test_build_report_basic(void) {
  AquariumProperties props = {.temperature = 26.5f,
                              .ph = 7.2f,
                              .tds = 350.0f,
                              .turbidity = 15.0f,
                              .water_level = 85.0f,
                              .heater = true,
                              .pump_in = false,
                              .pump_out = false,
                              .auto_mode = true,
                              .feed_countdown = 3600,
                              .feeding_in_progress = false,
                              .alarm_level = 0,
                              .alarm_muted = false};

  char topic[256];
  char payload[1024];
  size_t topic_len, payload_len;

  AquaError err = aqua_iotda_build_report(
      TEST_DEVICE_ID, &props, topic, sizeof(topic), payload, sizeof(payload),
      &topic_len, &payload_len);

  TEST_ASSERT_EQUAL(AQUA_OK, err);

  /* 验证 Topic */
  TEST_ASSERT_EQUAL_STRING("$oc/devices/690237639798273cc4fd09cb_MyAquarium_01/"
                           "sys/properties/report",
                           topic);

  /* 验证 Payload 包含关键字段 */
  TEST_ASSERT_NOT_NULL(strstr(payload, "\"service_id\":\"Aquarium\""));
  TEST_ASSERT_NOT_NULL(strstr(payload, "\"temperature\":"));
  TEST_ASSERT_NOT_NULL(strstr(payload, "\"heater\":true"));
}

void test_build_report_null_ptr(void) {
  AquariumProperties props = {0};
  char topic[256], payload[1024];
  size_t topic_len, payload_len;

  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_iotda_build_report(NULL, &props, topic, 256, payload,
                                            1024, &topic_len, &payload_len));
  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_iotda_build_report(TEST_DEVICE_ID, NULL, topic, 256,
                                            payload, 1024, &topic_len,
                                            &payload_len));
}

/* ============================================================================
 * 测试：命令处理 - control 成功
 * ============================================================================
 */

void test_handle_control_command_success(void) {
  AquariumState state;
  aqua_logic_init(&state);

  const char *topic =
      "$oc/devices/" TEST_DEVICE_ID "/sys/commands/request_id=req123";
  const char *payload = "{"
                        "\"service_id\":\"aquarium_control\","
                        "\"command_name\":\"control\","
                        "\"paras\":{\"heater\":true,\"auto_mode\":false}"
                        "}";

  IoTDACommandResult result;
  AquaError err = aqua_iotda_handle_command(TEST_DEVICE_ID, topic, payload,
                                            strlen(payload), &state, &result);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_TRUE(result.has_response);

  /* 验证响应 Topic */
  TEST_ASSERT_NOT_NULL(strstr(result.response_topic, "request_id=req123"));
  TEST_ASSERT_NOT_NULL(strstr(result.response_topic, "/response/"));

  /* 验证响应 Payload */
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload, "\"result_code\":0"));
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload,
                              "\"response_name\":\"control_response\""));
  TEST_ASSERT_NOT_NULL(
      strstr(result.response_payload, "\"result\":\"success\""));

  /* 验证状态已更新 */
  TEST_ASSERT_TRUE(state.props.heater);
  TEST_ASSERT_FALSE(state.props.auto_mode);
}

/* ============================================================================
 * 测试：命令处理 - threshold 成功
 * ============================================================================
 */

void test_handle_threshold_command_success(void) {
  AquariumState state;
  aqua_logic_init(&state);

  const char *topic =
      "$oc/devices/" TEST_DEVICE_ID "/sys/commands/request_id=req456";
  const char *payload = "{"
                        "\"service_id\":\"aquarium_threshold\","
                        "\"command_name\":\"set_thresholds\","
                        "\"paras\":{\"temp_min\":22.0,\"temp_max\":30.0}"
                        "}";

  IoTDACommandResult result;
  AquaError err = aqua_iotda_handle_command(TEST_DEVICE_ID, topic, payload,
                                            strlen(payload), &state, &result);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_TRUE(result.has_response);

  /* 验证响应 */
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload,
                              "\"response_name\":\"set_thresholds_response\""));
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload, "\"result_code\":0"));

  /* 验证状态已更新 */
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 22.0f, state.thresholds.temp_min);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 30.0f, state.thresholds.temp_max);
}

/* ============================================================================
 * 测试：命令处理 - config 成功
 * ============================================================================
 */

void test_handle_config_command_success(void) {
  AquariumState state;
  aqua_logic_init(&state);

  const char *topic =
      "$oc/devices/" TEST_DEVICE_ID "/sys/commands/request_id=req789";
  const char *payload =
      "{"
      "\"service_id\":\"aquariumConfig\","
      "\"command_name\":\"set_config\","
      "\"paras\":{\"wifi_ssid\":\"TestSSID\",\"ph_offset\":0.25}"
      "}";

  IoTDACommandResult result;
  AquaError err = aqua_iotda_handle_command(TEST_DEVICE_ID, topic, payload,
                                            strlen(payload), &state, &result);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_TRUE(result.has_response);

  /* 验证响应 */
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload,
                              "\"response_name\":\"set_config_response\""));

  /* 验证状态已更新 */
  TEST_ASSERT_EQUAL_STRING("TestSSID", state.config.wifi_ssid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.25f, state.config.ph_offset);
}

/* ============================================================================
 * 测试：命令处理 - JSON 解析错误
 * ============================================================================
 */

void test_handle_command_json_parse_error(void) {
  AquariumState state;
  aqua_logic_init(&state);

  const char *topic =
      "$oc/devices/" TEST_DEVICE_ID "/sys/commands/request_id=reqErr";
  const char *payload = "{invalid json}";

  IoTDACommandResult result;
  AquaError err = aqua_iotda_handle_command(TEST_DEVICE_ID, topic, payload,
                                            strlen(payload), &state, &result);

  TEST_ASSERT_EQUAL(AQUA_OK, err); /* 函数执行成功 */
  TEST_ASSERT_TRUE(result.has_response);

  /* 验证错误响应 */
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload, "\"result_code\":2"));
  TEST_ASSERT_NOT_NULL(
      strstr(result.response_payload, "\"result\":\"failed\""));
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload, "\"error\":"));
}

/* ============================================================================
 * 测试：命令处理 - 未知命令
 * ============================================================================
 */

void test_handle_command_unknown_command(void) {
  AquariumState state;
  aqua_logic_init(&state);

  const char *topic =
      "$oc/devices/" TEST_DEVICE_ID "/sys/commands/request_id=reqUnk";
  const char *payload = "{"
                        "\"service_id\":\"unknown_service\","
                        "\"command_name\":\"unknown_cmd\","
                        "\"paras\":{}"
                        "}";

  IoTDACommandResult result;
  AquaError err = aqua_iotda_handle_command(TEST_DEVICE_ID, topic, payload,
                                            strlen(payload), &state, &result);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_TRUE(result.has_response);

  /* 验证错误响应 */
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload, "\"result_code\":2"));
  TEST_ASSERT_NOT_NULL(
      strstr(result.response_payload, "\"response_name\":\"unknown_cmd_response\""));
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload, "unknown command"));
}

/* ============================================================================
 * 测试：命令处理 - Topic 解析失败
 * ============================================================================
 */

void test_handle_command_invalid_topic(void) {
  AquariumState state;
  aqua_logic_init(&state);

  const char *topic = "$oc/devices/" TEST_DEVICE_ID
                      "/sys/properties/report"; /* 无 request_id */
  const char *payload = "{\"service_id\":\"aquarium_control\",\"command_name\":"
                        "\"control\",\"paras\":{}}";

  IoTDACommandResult result;
  AquaError err = aqua_iotda_handle_command(TEST_DEVICE_ID, topic, payload,
                                            strlen(payload), &state, &result);

  TEST_ASSERT_EQUAL(AQUA_ERR_TOPIC_PARSE, err);
  TEST_ASSERT_FALSE(result.has_response); /* 无法生成响应 */
}

/* ============================================================================
 * 测试：命令处理 - 立即投喂
 * ============================================================================
 */

void test_handle_feed_command(void) {
  AquariumState state;
  aqua_logic_init(&state);

  const char *topic =
      "$oc/devices/" TEST_DEVICE_ID "/sys/commands/request_id=reqFeed";
  const char *payload = "{"
                        "\"service_id\":\"aquarium_control\","
                        "\"command_name\":\"control\","
                        "\"paras\":{\"feed\":true}"
                        "}";

  IoTDACommandResult result;
  AquaError err = aqua_iotda_handle_command(TEST_DEVICE_ID, topic, payload,
                                            strlen(payload), &state, &result);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_TRUE(result.has_response);
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload, "\"result_code\":0"));

  /* 验证投喂已触发 */
  TEST_ASSERT_TRUE(state.props.feeding_in_progress);
}

/* ============================================================================ */
/* 测试：命令处理 - 一次性倒计时投喂 */
/* ============================================================================ */

void test_handle_feed_once_delay_command(void) {
  AquariumState state;
  aqua_logic_init(&state);

  const char *topic =
      "$oc/devices/" TEST_DEVICE_ID "/sys/commands/request_id=reqFeedOnce";
  const char *payload = "{"
                        "\"service_id\":\"aquarium_control\","
                        "\"command_name\":\"control\","
                        "\"paras\":{\"feed_once_delay\":120}"
                        "}";

  IoTDACommandResult result;
  AquaError err = aqua_iotda_handle_command(TEST_DEVICE_ID, topic, payload,
                                            strlen(payload), &state, &result);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_TRUE(result.has_response);
  TEST_ASSERT_NOT_NULL(strstr(result.response_payload, "\"result_code\":0"));

  /* 验证倒计时已设置 */
  TEST_ASSERT_EQUAL(120, state.feed_once_timer);
}

/* ============================================================================
 * 测试：空指针
 * ============================================================================
 */

void test_handle_command_null_ptr(void) {
  AquariumState state;
  IoTDACommandResult result;

  TEST_ASSERT_EQUAL(
      AQUA_ERR_NULL_PTR,
      aqua_iotda_handle_command(NULL, "topic", "payload", 7, &state, &result));
  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_iotda_handle_command(TEST_DEVICE_ID, NULL, "payload",
                                              7, &state, &result));
  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_iotda_handle_command(TEST_DEVICE_ID, "topic", NULL, 0,
                                              &state, &result));
  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_iotda_handle_command(TEST_DEVICE_ID, "topic",
                                              "payload", 7, NULL, &result));
  TEST_ASSERT_EQUAL(AQUA_ERR_NULL_PTR,
                    aqua_iotda_handle_command(TEST_DEVICE_ID, "topic",
                                              "payload", 7, &state, NULL));
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  /* 属性上报测试 */
  RUN_TEST(test_build_report_basic);
  RUN_TEST(test_build_report_null_ptr);

  /* 命令处理成功测试 */
  RUN_TEST(test_handle_control_command_success);
  RUN_TEST(test_handle_threshold_command_success);
  RUN_TEST(test_handle_config_command_success);
  RUN_TEST(test_handle_feed_command);
  RUN_TEST(test_handle_feed_once_delay_command);

  /* 命令处理错误测试 */
  RUN_TEST(test_handle_command_json_parse_error);
  RUN_TEST(test_handle_command_unknown_command);
  RUN_TEST(test_handle_command_invalid_topic);
  RUN_TEST(test_handle_command_null_ptr);

  return UNITY_END();
}
