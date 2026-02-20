/**
 * @file test_esp32_mqtt.c
 * @brief ESP32 MQTT 联通层单元测试
 */

#include "aquarium_esp32_mqtt.h"
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

static void reset_mocks(void) {
  g_tx_len = 0;
  g_mock_time_ms = 0;
  memset(g_tx_buffer, 0, sizeof(g_tx_buffer));
}

/* ============================================================================
 * 辅助函数
 * ============================================================================
 */

static void feed_ok(AtClient *at) {
  const char *rx = "OK\r\n";
  aqua_at_feed_rx(at, (const uint8_t *)rx, strlen(rx));
}

static void feed_error(AtClient *at) {
  const char *rx = "ERROR\r\n";
  aqua_at_feed_rx(at, (const uint8_t *)rx, strlen(rx));
}

static void feed_prompt(AtClient *at) {
  /* 真实 ESP-AT 响应：OK 后跟 > 提示符（可能无 CRLF） */
  const char *rx = "OK\r\n>";
  aqua_at_feed_rx(at, (const uint8_t *)rx, strlen(rx));
}

/* 测试裸 >（无 CRLF） */
static void feed_prompt_bare(AtClient *at) {
  const char *rx = ">";
  aqua_at_feed_rx(at, (const uint8_t *)rx, strlen(rx));
}

void setUp(void) { reset_mocks(); }
void tearDown(void) {}

/* ============================================================================
 * 测试：初始化
 * ============================================================================
 */

void test_mqtt_init(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test_device");
  aqua_mqtt_init(&mqtt, &at, &app);

  TEST_ASSERT_EQUAL(MQTT_STATE_IDLE, mqtt.state);
  TEST_ASSERT_EQUAL_PTR(&at, mqtt.at);
}

/* ============================================================================
 * 测试：完整建链流程
 * ============================================================================
 */

void test_mqtt_full_connect(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "device123");
  aqua_mqtt_init(&mqtt, &at, &app);

  MqttConfig cfg = {0};
  strcpy(cfg.wifi_ssid, "TestWiFi");
  strcpy(cfg.wifi_password, "12345678");
  strcpy(cfg.broker_host, "test.iot.cn");
  cfg.broker_port = 1883;
  strcpy(cfg.device_id, "device123");
  strcpy(cfg.device_secret, "secret");
  aqua_mqtt_set_config(&mqtt, &cfg);
  aqua_mqtt_set_timestamp(&mqtt, "2025121400");

  aqua_mqtt_start(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AT_TEST, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ATE0, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWMODE, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_SNTPCFG, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_SNTPTIME, mqtt.state);

  /* 喂入 SNTP 时间响应 */
  const char *sntp_resp = "+CIPSNTPTIME:Sat Dec 14 13:00:00 2024\r\nOK\r\n";
  aqua_at_feed_rx(&at, (const uint8_t *)sntp_resp, strlen(sntp_resp));
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_MQTTUSERCFG, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_MQTTCONN, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_MQTTSUB, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);
}

/* ============================================================================
 * 测试：WiFi 连接失败
 * ============================================================================
 */

void test_mqtt_wifi_connect_fail(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  MqttConfig cfg = {0};
  strcpy(cfg.wifi_ssid, "BadWiFi");
  aqua_mqtt_set_config(&mqtt, &cfg);

  aqua_mqtt_start(&mqtt);
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);

  /* 新逻辑：CWJAP 失败会重试，不直接进入 ERROR */
  /* 第一次失败，仍在 CWJAP 状态重试 */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);
  TEST_ASSERT_EQUAL(1, mqtt.cwjap_fail_count);

  /* 第二次失败 */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);
  TEST_ASSERT_EQUAL(2, mqtt.cwjap_fail_count);

  /* 第三次失败 -> 进入 AP 模式 */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_START, mqtt.state);
}

/* ============================================================================
 * 测试：属性上报
 * ============================================================================
 */

void test_mqtt_publish_online(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  bool ok = aqua_mqtt_publish(&mqtt, "test/t", "{\"a\":1}", 7);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);
}

/* ============================================================================
 * 测试：非 Online 状态下无法发布
 * ============================================================================
 */

void test_mqtt_publish_not_online(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_CWJAP;

  bool ok = aqua_mqtt_publish(&mqtt, "topic", "data", 4);
  TEST_ASSERT_FALSE(ok);
}

/* ============================================================================
 * 测试：发布后收到 +MQTTPUB:OK 恢复 ONLINE
 * ============================================================================
 */

void test_mqtt_publish_completes(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  aqua_mqtt_publish(&mqtt, "t", "{}", 2);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);

  /* 发送 > 提示符（而非 OK）表示可以发送数据 */
  feed_prompt(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUB_DATA, mqtt.state);

  const char *urc = "+MQTTPUB:OK\r\n";
  aqua_at_feed_rx(&at, (const uint8_t *)urc, strlen(urc));
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);
}

/* ============================================================================
 * 测试：PUB_DATA 超时进入 ERROR
 * ============================================================================
 */

void test_mqtt_publish_timeout(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  g_mock_time_ms = 1000;
  aqua_mqtt_publish(&mqtt, "t", "{}", 2);

  /* 发送 > 提示符 */
  feed_prompt(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUB_DATA, mqtt.state);

  g_mock_time_ms = 6001;
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ERROR, mqtt.state);
}

/* ============================================================================
 * 测试：截断的 +MQTTSUBRECV 被拒绝
 * ============================================================================
 */

void test_mqtt_truncated_subrecv_rejected(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  const char *urc = "+MQTTSUBRECV:0,\"topic\",100,short\r\n";
  aqua_at_feed_rx(&at, (const uint8_t *)urc, strlen(urc));

  bool handled = aqua_mqtt_poll_commands(&mqtt);
  TEST_ASSERT_FALSE(handled);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);
}

/* ============================================================================
 * 测试：下行命令闭环（验证发出的响应）
 * ============================================================================
 */

void test_mqtt_command_response_closed_loop(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "dev123");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  const char *payload =
      "{\"service_id\":\"Aquarium\","
      "\"command_name\":\"control\",\"paras\":{\"heater\":true}}";
  char urc[512];
  snprintf(urc, sizeof(urc),
           "+MQTTSUBRECV:0,\"$oc/devices/dev123/sys/commands/"
           "request_id=r1\",%zu,%s\r\n",
           strlen(payload), payload);

  reset_mocks();
  aqua_at_feed_rx(&at, (const uint8_t *)urc, strlen(urc));

  bool handled = aqua_mqtt_poll_commands(&mqtt);
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "AT+MQTTPUBRAW"));
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "request_id=r1"));
}

/* ============================================================================
 * 测试：SNTP 时间解析
 * ============================================================================
 */

void test_mqtt_parse_sntp_time_valid(void) {
  char ts[12];
  /* 标准格式 */
  bool ok =
      aqua_mqtt_parse_sntp_time("+CIPSNTPTIME:Mon Oct 18 20:12:27 2021", ts);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("2021101820", ts);
}

void test_mqtt_parse_sntp_time_january(void) {
  char ts[12];
  bool ok =
      aqua_mqtt_parse_sntp_time("+CIPSNTPTIME:Wed Jan 01 00:00:00 2025", ts);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("2025010100", ts);
}

void test_mqtt_parse_sntp_time_december(void) {
  char ts[12];
  bool ok =
      aqua_mqtt_parse_sntp_time("+CIPSNTPTIME:Tue Dec 31 23:59:59 2024", ts);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("2024123123", ts);
}

void test_mqtt_parse_sntp_time_invalid_null(void) {
  char ts[12];
  TEST_ASSERT_FALSE(aqua_mqtt_parse_sntp_time(NULL, ts));
  TEST_ASSERT_FALSE(aqua_mqtt_parse_sntp_time("+CIPSNTPTIME:Mon Oct 18", NULL));
}

void test_mqtt_parse_sntp_time_invalid_format(void) {
  char ts[12];
  /* 没有前缀 */
  TEST_ASSERT_FALSE(aqua_mqtt_parse_sntp_time("Mon Oct 18 20:12:27 2021", ts));
  /* 无效月份 */
  TEST_ASSERT_FALSE(
      aqua_mqtt_parse_sntp_time("+CIPSNTPTIME:Mon Xyz 18 20:12:27 2021", ts));
}

/* ============================================================================
 * AP 配网测试
 * ============================================================================
 */

void test_mqtt_cwjap_fail_enters_ap_mode(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  reset_mocks();
  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test_device");
  aqua_mqtt_init(&mqtt, &at, &app);

  MqttConfig cfg = {.wifi_ssid = "TestSSID",
                    .wifi_password = "TestPass",
                    .broker_host = "iot.example.com",
                    .broker_port = 1883,
                    .device_id = "device_001",
                    .device_secret = "secret123"};
  aqua_mqtt_set_config(&mqtt, &cfg);
  aqua_mqtt_start(&mqtt);

  /* 跳到 CWJAP 状态 */
  feed_ok(&at); /* AT */
  aqua_mqtt_step(&mqtt);
  feed_ok(&at); /* ATE0 */
  aqua_mqtt_step(&mqtt);
  feed_ok(&at); /* CWMODE */
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);

  /* 第一次 CWJAP 失败 */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(1, mqtt.cwjap_fail_count);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state); /* 仍在重试 */

  /* 第二次 CWJAP 失败 */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(2, mqtt.cwjap_fail_count);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);

  /* 第三次 CWJAP 失败 -> 进入 AP 模式 */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(3, mqtt.cwjap_fail_count);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_START, mqtt.state);
}

void test_mqtt_parse_ap_request_home(void) {
  char ssid[33], pwd[65];
  /* 请求首页 */
  int result = aqua_mqtt_parse_ap_request("GET / HTTP/1.1\r\n", ssid, pwd);
  TEST_ASSERT_EQUAL(1, result);
}

void test_mqtt_parse_ap_request_config(void) {
  char ssid[33], pwd[65];
  /* 请求配置 */
  int result = aqua_mqtt_parse_ap_request(
      "GET /config?ssid=MyWiFi&pwd=MyPassword HTTP/1.1\r\n", ssid, pwd);
  TEST_ASSERT_EQUAL(2, result);
  TEST_ASSERT_EQUAL_STRING("MyWiFi", ssid);
  TEST_ASSERT_EQUAL_STRING("MyPassword", pwd);
}

void test_mqtt_parse_ap_request_url_encoded(void) {
  char ssid[33], pwd[65];
  /* URL 编码的配置 */
  int result = aqua_mqtt_parse_ap_request(
      "GET /config?ssid=My%20WiFi&pwd=Pass%2B123 HTTP/1.1\r\n", ssid, pwd);
  TEST_ASSERT_EQUAL(2, result);
  TEST_ASSERT_EQUAL_STRING("My WiFi", ssid);
  TEST_ASSERT_EQUAL_STRING("Pass+123", pwd);
}

void test_mqtt_parse_ap_request_invalid(void) {
  char ssid[33], pwd[65];
  /* 非 GET 请求 */
  TEST_ASSERT_EQUAL(
      0, aqua_mqtt_parse_ap_request("POST / HTTP/1.1\r\n", ssid, pwd));
  /* 空字符串 */
  TEST_ASSERT_EQUAL(0, aqua_mqtt_parse_ap_request(NULL, ssid, pwd));
}

void test_mqtt_ap_full_flow(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  reset_mocks();
  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test_device");
  aqua_mqtt_init(&mqtt, &at, &app);

  MqttConfig cfg = {.wifi_ssid = "WrongSSID",
                    .wifi_password = "WrongPass",
                    .broker_host = "iot.example.com",
                    .broker_port = 1883};
  aqua_mqtt_set_config(&mqtt, &cfg);

  /* 模拟通过正常流程进入 AP 模式（3次 CWJAP 失败） */
  aqua_mqtt_start(&mqtt);
  feed_ok(&at); /* AT */
  aqua_mqtt_step(&mqtt);
  feed_ok(&at); /* ATE0 */
  aqua_mqtt_step(&mqtt);
  feed_ok(&at); /* CWMODE=1 */
  aqua_mqtt_step(&mqtt);
  /* 3次 CWJAP 失败 */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_START, mqtt.state);

  /* AP_START -> AP_CIPMUX (收到 CWMODE=3 的 OK) */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_CIPMUX, mqtt.state);

  /* AP_CIPMUX -> AP_CIPDINFO (收到 CWSAP 的 OK) */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_CIPDINFO, mqtt.state);

  /* AP_CIPDINFO -> AP_SERVER (收到 CIPMUX 的 OK) */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_SERVER, mqtt.state);

  /* AP_SERVER -> AP_WAIT (收到 CIPDINFO 的 OK，然后 CIPSERVER 的 OK) */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_WAIT, mqtt.state);

  /* 模拟收到配置请求 */
  const char *ipd =
      "+IPD,0,50:GET /config?ssid=NewSSID&pwd=NewPass HTTP/1.1\r\n";
  aqua_at_feed_rx(&at, (const uint8_t *)ipd, strlen(ipd));

  /* 轮询处理 - 现在会转到 AP_SENDING（非阻塞） */
  bool got_config = aqua_mqtt_poll_ap_config(&mqtt);
  TEST_ASSERT_TRUE(got_config);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_SENDING, mqtt.state);

  /* 验证配置被写入 */
  TEST_ASSERT_EQUAL_STRING("NewSSID", mqtt.config.wifi_ssid);
  TEST_ASSERT_EQUAL_STRING("NewPass", mqtt.config.wifi_password);

  /* 验证 app 层 config_dirty 被设置 */
  TEST_ASSERT_TRUE(app.state.config_dirty);
  TEST_ASSERT_EQUAL_STRING("NewSSID", app.state.config.wifi_ssid);
}

void test_mqtt_is_ap_mode(void) {
  MqttClient mqtt = {0};

  mqtt.state = MQTT_STATE_IDLE;
  TEST_ASSERT_FALSE(aqua_mqtt_is_ap_mode(&mqtt));

  mqtt.state = MQTT_STATE_ONLINE;
  TEST_ASSERT_FALSE(aqua_mqtt_is_ap_mode(&mqtt));

  mqtt.state = MQTT_STATE_AP_WAIT;
  TEST_ASSERT_TRUE(aqua_mqtt_is_ap_mode(&mqtt));

  mqtt.state = MQTT_STATE_AP_START;
  TEST_ASSERT_TRUE(aqua_mqtt_is_ap_mode(&mqtt));
}

/* ============================================================================
 * 任务 14 测试：AT 提示符检测
 * ============================================================================
 */

void test_at_prompt_detection(void) {
  AtClient at;
  aqua_at_init(&at, mock_write, mock_now_ms);

  /* 使用 begin_with_prompt 发送命令 */
  aqua_at_begin_with_prompt(&at, "AT+CIPSEND=0,100", 5000);
  TEST_ASSERT_EQUAL(AT_STATE_WAITING, at.state);
  TEST_ASSERT_TRUE(at.expect_prompt);

  /* 模拟真实 ESP-AT 响应：OK 后跟 > 提示符（裸 >） */
  const char *rx = "OK\r\n>";
  aqua_at_feed_rx(&at, (const uint8_t *)rx, strlen(rx));
  aqua_at_step(&at);

  /* 应该进入 GOT_PROMPT 状态 */
  TEST_ASSERT_EQUAL(AT_STATE_GOT_PROMPT, at.state);
}

/* 任务 15 测试：裸 >（无 CRLF）也能检测 */
void test_at_prompt_bare(void) {
  AtClient at;
  aqua_at_init(&at, mock_write, mock_now_ms);

  aqua_at_begin_with_prompt(&at, "AT+CIPSEND=0,50", 5000);

  /* 先收到 OK */
  feed_ok(&at);
  aqua_at_step(&at);
  TEST_ASSERT_EQUAL(AT_STATE_WAITING, at.state); /* 还在等待 > */
  TEST_ASSERT_TRUE(at.got_ok);

  /* 再收到裸 >（无 CRLF） */
  feed_prompt_bare(&at);
  aqua_at_step(&at);
  TEST_ASSERT_EQUAL(AT_STATE_GOT_PROMPT, at.state);
}

/* ============================================================================
 * 任务 14 测试：ERROR 后自动重连（退避）
 * ============================================================================
 */

void test_mqtt_auto_reconnect_backoff(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  /* 直接设置为 ERROR 状态 */
  mqtt.state = MQTT_STATE_ERROR;
  mqtt.error_time_ms = 0;
  mqtt.reconnect_delay_ms = 0;

  g_mock_time_ms = 1000;
  aqua_mqtt_step(&mqtt);
  /* 首次进入，记录时间，还没到退避时间 */
  TEST_ASSERT_EQUAL(MQTT_STATE_ERROR, mqtt.state);
  TEST_ASSERT_EQUAL(1000, mqtt.error_time_ms);
  TEST_ASSERT_EQUAL(RECONNECT_DELAY_INIT_MS, mqtt.reconnect_delay_ms);

  /* 等待退避时间到达 */
  g_mock_time_ms = 1000 + RECONNECT_DELAY_INIT_MS;
  aqua_mqtt_step(&mqtt);
  /* 应该触发重连，状态变为 AT_TEST */
  TEST_ASSERT_EQUAL(MQTT_STATE_AT_TEST, mqtt.state);
  /* 下次退避时间翻倍 */
  TEST_ASSERT_EQUAL(RECONNECT_DELAY_INIT_MS * 2, mqtt.reconnect_delay_ms);
}

/* ============================================================================
 * 任务 14 测试：WiFi 变更后自动重连
 * ============================================================================
 */

void test_mqtt_wifi_change_triggers_reconnect(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  /* 通知 WiFi 配置已变更 */
  aqua_mqtt_notify_wifi_changed(&mqtt);
  TEST_ASSERT_TRUE(mqtt.wifi_changed);

  /* 推进状态机，应该触发重连 */
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AT_TEST, mqtt.state);
  TEST_ASSERT_FALSE(mqtt.wifi_changed);
}

/* ============================================================================
 * 任务 14 测试：网络状态查询
 * ============================================================================
 */

void test_mqtt_get_net_status(void) {
  MqttClient mqtt = {0};

  mqtt.state = MQTT_STATE_ONLINE;
  TEST_ASSERT_EQUAL(2, aqua_mqtt_get_net_status(&mqtt));

  mqtt.state = MQTT_STATE_ERROR;
  TEST_ASSERT_EQUAL(0, aqua_mqtt_get_net_status(&mqtt));

  mqtt.state = MQTT_STATE_AP_WAIT;
  TEST_ASSERT_EQUAL(3, aqua_mqtt_get_net_status(&mqtt));

  mqtt.state = MQTT_STATE_CWJAP;
  TEST_ASSERT_EQUAL(1, aqua_mqtt_get_net_status(&mqtt));
}

/* ============================================================================
 * 任务 20 测试：云端 set_config 修改 WiFi 后自动重连
 * ============================================================================
 */

void test_mqtt_set_config_with_wifi_triggers_reconnect(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  reset_mocks();
  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "dev123");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  /* 构造 set_config 命令（包含 WiFi 字段） */
  const char *payload =
      "{\"service_id\":\"aquariumConfig\","
      "\"command_name\":\"set_config\","
      "\"paras\":{\"wifi_ssid\":\"NewWiFi\",\"wifi_password\":\"NewPass123\"}}";
  char urc[512];
  snprintf(urc, sizeof(urc),
           "+MQTTSUBRECV:0,\"$oc/devices/dev123/sys/commands/"
           "request_id=r2\",%zu,%s\r\n",
           strlen(payload), payload);

  aqua_at_feed_rx(&at, (const uint8_t *)urc, strlen(urc));

  bool handled = aqua_mqtt_poll_commands(&mqtt);
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);
  /* 验证 wifi_changed 被设置 */
  TEST_ASSERT_TRUE(mqtt.wifi_changed);
  /* 验证配置已同步 */
  TEST_ASSERT_EQUAL_STRING("NewWiFi", mqtt.config.wifi_ssid);
  TEST_ASSERT_EQUAL_STRING("NewPass123", mqtt.config.wifi_password);

  /* 模拟发布完成后进入 ONLINE，然后触发重连 */
  feed_prompt(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUB_DATA, mqtt.state);

  const char *pub_ok = "+MQTTPUB:OK\r\n";
  aqua_at_feed_rx(&at, (const uint8_t *)pub_ok, strlen(pub_ok));
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);

  /* ONLINE 状态下推进，应触发重连 */
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AT_TEST, mqtt.state);
  TEST_ASSERT_FALSE(mqtt.wifi_changed);
}

void test_mqtt_set_config_without_wifi_no_reconnect(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  reset_mocks();
  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "dev123");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  /* 构造 set_config 命令（仅校准参数，无 WiFi） */
  const char *payload = "{\"service_id\":\"aquariumConfig\","
                        "\"command_name\":\"set_config\","
                        "\"paras\":{\"ph_offset\":0.1,\"tds_factor\":1.05}}";
  char urc[512];
  snprintf(urc, sizeof(urc),
           "+MQTTSUBRECV:0,\"$oc/devices/dev123/sys/commands/"
           "request_id=r3\",%zu,%s\r\n",
           strlen(payload), payload);

  aqua_at_feed_rx(&at, (const uint8_t *)urc, strlen(urc));

  bool handled = aqua_mqtt_poll_commands(&mqtt);
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);
  /* WiFi 未变更，不应触发 */
  TEST_ASSERT_FALSE(mqtt.wifi_changed);
}

void test_mqtt_set_config_empty_ssid_no_reconnect(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  reset_mocks();
  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "dev123");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  /* 构造 set_config 命令（WiFi SSID 为空） */
  const char *payload =
      "{\"service_id\":\"aquariumConfig\","
      "\"command_name\":\"set_config\","
      "\"paras\":{\"wifi_ssid\":\"\",\"wifi_password\":\"SomePass\"}}";
  char urc[512];
  snprintf(urc, sizeof(urc),
           "+MQTTSUBRECV:0,\"$oc/devices/dev123/sys/commands/"
           "request_id=r4\",%zu,%s\r\n",
           strlen(payload), payload);

  aqua_at_feed_rx(&at, (const uint8_t *)urc, strlen(urc));

  bool handled = aqua_mqtt_poll_commands(&mqtt);
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);
  /* SSID 为空，不应触发重连 */
  TEST_ASSERT_FALSE(mqtt.wifi_changed);
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_mqtt_init);
  RUN_TEST(test_mqtt_full_connect);
  RUN_TEST(test_mqtt_wifi_connect_fail);
  RUN_TEST(test_mqtt_publish_online);
  RUN_TEST(test_mqtt_publish_not_online);
  RUN_TEST(test_mqtt_publish_completes);
  RUN_TEST(test_mqtt_publish_timeout);
  RUN_TEST(test_mqtt_truncated_subrecv_rejected);
  RUN_TEST(test_mqtt_command_response_closed_loop);

  /* SNTP 时间解析测试 */
  RUN_TEST(test_mqtt_parse_sntp_time_valid);
  RUN_TEST(test_mqtt_parse_sntp_time_january);
  RUN_TEST(test_mqtt_parse_sntp_time_december);
  RUN_TEST(test_mqtt_parse_sntp_time_invalid_null);
  RUN_TEST(test_mqtt_parse_sntp_time_invalid_format);

  /* AP 配网测试 */
  RUN_TEST(test_mqtt_cwjap_fail_enters_ap_mode);
  RUN_TEST(test_mqtt_parse_ap_request_home);
  RUN_TEST(test_mqtt_parse_ap_request_config);
  RUN_TEST(test_mqtt_parse_ap_request_url_encoded);
  RUN_TEST(test_mqtt_parse_ap_request_invalid);
  RUN_TEST(test_mqtt_ap_full_flow);
  RUN_TEST(test_mqtt_is_ap_mode);

  /* 任务 14：稳定性与自动恢复测试 */
  RUN_TEST(test_at_prompt_detection);
  RUN_TEST(test_mqtt_auto_reconnect_backoff);
  RUN_TEST(test_mqtt_wifi_change_triggers_reconnect);
  RUN_TEST(test_mqtt_get_net_status);

  /* 任务 15：> 提示符真实行为测试 */
  RUN_TEST(test_at_prompt_bare);

  /* 任务 20：云端 set_config 修改 WiFi 后自动重连 */
  RUN_TEST(test_mqtt_set_config_with_wifi_triggers_reconnect);
  RUN_TEST(test_mqtt_set_config_without_wifi_no_reconnect);
  RUN_TEST(test_mqtt_set_config_empty_ssid_no_reconnect);

  return UNITY_END();
}
