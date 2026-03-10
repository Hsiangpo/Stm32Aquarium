/**
 * @file test_esp32_mqtt.c
 * @brief ESP32 MQTT 
 */

#include "aquarium_esp32_mqtt.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

/* ============================================================================
 * Mock 
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
 * 
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
 /* ESP-AT K > CRLF */
  const char *rx = "OK\r\n>";
  aqua_at_feed_rx(at, (const uint8_t *)rx, strlen(rx));
}

/* > CRLF */
static void feed_prompt_bare(AtClient *at) {
  const char *rx = ">";
  aqua_at_feed_rx(at, (const uint8_t *)rx, strlen(rx));
}

void setUp(void) { reset_mocks(); }
void tearDown(void) {}

/* ============================================================================
 * 
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
 * 
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

 /* SNTP */
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
 * iFi 
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
  strcpy(cfg.wifi_password, "BadPass123");
  aqua_mqtt_set_config(&mqtt, &cfg);

  aqua_mqtt_start(&mqtt);
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);

 /* WJAP ERROR */
 /* CWJAP */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);
  TEST_ASSERT_EQUAL(1, mqtt.cwjap_fail_count);

 /* */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);
  TEST_ASSERT_EQUAL(2, mqtt.cwjap_fail_count);

 /* -> AP */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_START, mqtt.state);
}

void test_mqtt_placeholder_wifi_enters_ap_mode_directly(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  MqttConfig cfg = {0};
  strcpy(cfg.wifi_ssid, "YourWiFiSSID");
  strcpy(cfg.wifi_password, "YourWiFiPassword");
  aqua_mqtt_set_config(&mqtt, &cfg);

  aqua_mqtt_start(&mqtt);
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ATE0, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWMODE, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_START, mqtt.state);
}

void test_mqtt_cwjap_command_escapes_credentials(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  reset_mocks();
  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  MqttConfig cfg = {0};
  strcpy(cfg.wifi_ssid, "A\"B\\C");
  strcpy(cfg.wifi_password, "P\\\"Q");
  aqua_mqtt_set_config(&mqtt, &cfg);

  aqua_mqtt_start(&mqtt);
  feed_ok(&at);
  aqua_mqtt_step(&mqtt); /* AT -> ATE0 */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt); /* ATE0 -> CWMODE */

  reset_mocks();
  feed_ok(&at);
  aqua_mqtt_step(&mqtt); /* CWMODE -> CWJAP */

  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);
  TEST_ASSERT_NOT_NULL(
      strstr((char *)g_tx_buffer, "AT+CWJAP=\"A\\\"B\\\\C\",\"P\\\\\\\"Q\""));
}

void test_mqtt_sntpcfg_uses_multi_servers(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  reset_mocks();
  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  MqttConfig cfg = {0};
  strcpy(cfg.wifi_ssid, "TestWiFi");
  strcpy(cfg.wifi_password, "12345678");
  aqua_mqtt_set_config(&mqtt, &cfg);

  aqua_mqtt_start(&mqtt);
  feed_ok(&at);
  aqua_mqtt_step(&mqtt); /* AT -> ATE0 */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt); /* ATE0 -> CWMODE */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt); /* CWMODE -> CWJAP */

  reset_mocks();
  feed_ok(&at);
  aqua_mqtt_step(&mqtt); /* CWJAP -> SNTPCFG */

  TEST_ASSERT_EQUAL(MQTT_STATE_SNTPCFG, mqtt.state);
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "AT+CIPSNTPCFG=1,0"));
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "ntp.aliyun.com"));
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "ntp.ntsc.ac.cn"));
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "time.cloudflare.com"));
}

/* ============================================================================
 * 
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
 * Online 
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
 * +MQTTPUB:OK ONLINE
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

 /* > OK */
  feed_prompt(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUB_DATA, mqtt.state);

  const char *urc = "+MQTTPUB:OK\r\n";
  aqua_at_feed_rx(&at, (const uint8_t *)urc, strlen(urc));
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);
}

void test_mqtt_publish_completes_with_plain_ok_only(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  aqua_mqtt_publish(&mqtt, "t", "{}", 2);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);

  feed_prompt(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUB_DATA, mqtt.state);

  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);
}

/* ============================================================================
 * UB_DATA ERROR
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

 /* > */
  feed_prompt(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUB_DATA, mqtt.state);

  g_mock_time_ms = 16001;
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);
}

void test_mqtt_pub_data_preserves_subrecv_for_next_poll(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  reset_mocks();
  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "dev123");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  /* Start an in-flight publish so state enters PUB_DATA. */
  TEST_ASSERT_TRUE(aqua_mqtt_publish(&mqtt, "test/topic", "{\"v\":1}", 7));
  feed_prompt(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUB_DATA, mqtt.state);

  const char *cmd_payload =
      "{\"service_id\":\"Aquarium\",\"command_name\":\"control\","
      "\"paras\":{\"heater\":true}}";
  char subrecv_urc[512];
  snprintf(subrecv_urc, sizeof(subrecv_urc),
           "+MQTTSUBRECV:0,\"$oc/devices/dev123/sys/commands/"
           "request_id=r_pub\",%zu,%s\r\n",
           strlen(cmd_payload), cmd_payload);

  const char *pub_ok = "+MQTTPUB:OK\r\n";
  char rx[768];
  snprintf(rx, sizeof(rx), "%s%s", subrecv_urc, pub_ok);
  aqua_at_feed_rx(&at, (const uint8_t *)rx, strlen(rx));

  /* PUB_DATA should complete without losing the command URC. */
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);

  reset_mocks();
  bool handled = aqua_mqtt_poll_commands(&mqtt);
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "request_id=r_pub"));
}

/* ============================================================================
 * +MQTTSUBRECV 
 * ============================================================================
 */

void test_mqtt_truncated_subrecv_still_handled(void) {
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
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);
}

void test_mqtt_truncated_subrecv_with_request_id_generates_error_response(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  reset_mocks();
  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "dev123");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  const char *urc =
      "+MQTTSUBRECV:0,\"$oc/devices/dev123/sys/commands/request_id=r_trunc\","
      "120,{\"service_id\":\"aquarium_control\"\r\n";
  aqua_at_feed_rx(&at, (const uint8_t *)urc, strlen(urc));

  bool handled = aqua_mqtt_poll_commands(&mqtt);
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "AT+MQTTPUBRAW"));
  TEST_ASSERT_NOT_NULL(strstr((char *)g_tx_buffer, "request_id=r_trunc"));
}

/* ============================================================================
 * 
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
 * NTP 
 * ============================================================================
 */

void test_mqtt_parse_sntp_time_valid(void) {
  char ts[12];
 /* */
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

void test_mqtt_parse_sntp_time_single_digit_day_with_double_spaces(void) {
  char ts[12];
  bool ok =
      aqua_mqtt_parse_sntp_time("+CIPSNTPTIME:Mon Mar  2 16:51:42 2026", ts);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("2026030216", ts);
}

void test_mqtt_parse_sntp_time_invalid_null(void) {
  char ts[12];
  TEST_ASSERT_FALSE(aqua_mqtt_parse_sntp_time(NULL, ts));
  TEST_ASSERT_FALSE(aqua_mqtt_parse_sntp_time("+CIPSNTPTIME:Mon Oct 18", NULL));
}

void test_mqtt_parse_sntp_time_invalid_format(void) {
  char ts[12];
 /* */
  TEST_ASSERT_FALSE(aqua_mqtt_parse_sntp_time("Mon Oct 18 20:12:27 2021", ts));
 /* */
  TEST_ASSERT_FALSE(
      aqua_mqtt_parse_sntp_time("+CIPSNTPTIME:Mon Xyz 18 20:12:27 2021", ts));
}

/* ============================================================================
 * AP 
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

 /* CWJAP */
  feed_ok(&at); /* AT */
  aqua_mqtt_step(&mqtt);
  feed_ok(&at); /* ATE0 */
  aqua_mqtt_step(&mqtt);
  feed_ok(&at); /* CWMODE */
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);

 /* CWJAP */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(1, mqtt.cwjap_fail_count);
 TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state); /* */

 /* CWJAP */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(2, mqtt.cwjap_fail_count);
  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);

 /* CWJAP -> AP */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(3, mqtt.cwjap_fail_count);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_START, mqtt.state);
}

void test_mqtt_parse_ap_request_home(void) {
  char ssid[33], pwd[65];
 /* */
  int result = aqua_mqtt_parse_ap_request("GET / HTTP/1.1\r\n", ssid, pwd);
  TEST_ASSERT_EQUAL(1, result);
}

void test_mqtt_parse_ap_request_config(void) {
  char ssid[33], pwd[65];
 /* */
  int result = aqua_mqtt_parse_ap_request(
      "GET /config?ssid=MyWiFi&pwd=MyPassword HTTP/1.1\r\n", ssid, pwd);
  TEST_ASSERT_EQUAL(2, result);
  TEST_ASSERT_EQUAL_STRING("MyWiFi", ssid);
  TEST_ASSERT_EQUAL_STRING("MyPassword", pwd);
}

void test_mqtt_parse_ap_request_url_encoded(void) {
  char ssid[33], pwd[65];
 /* URL */
  int result = aqua_mqtt_parse_ap_request(
      "GET /config?ssid=My%20WiFi&pwd=Pass%2B123 HTTP/1.1\r\n", ssid, pwd);
  TEST_ASSERT_EQUAL(2, result);
  TEST_ASSERT_EQUAL_STRING("My WiFi", ssid);
  TEST_ASSERT_EQUAL_STRING("Pass+123", pwd);
}

void test_mqtt_parse_ap_request_invalid(void) {
  char ssid[33], pwd[65];
 /* GET */
  TEST_ASSERT_EQUAL(
      0, aqua_mqtt_parse_ap_request("POST / HTTP/1.1\r\n", ssid, pwd));
 /* */
  TEST_ASSERT_EQUAL(0, aqua_mqtt_parse_ap_request(NULL, ssid, pwd));
}

void test_mqtt_parse_ap_request_unknown_path_fallback_home(void) {
  char ssid[33], pwd[65];
  int result = aqua_mqtt_parse_ap_request(
      "GET /hotspot-detect.html HTTP/1.1\r\n", ssid, pwd);
  TEST_ASSERT_EQUAL(1, result);
}

void test_mqtt_parse_ap_request_absolute_uri_home(void) {
  char ssid[33], pwd[65];
  int result = aqua_mqtt_parse_ap_request(
      "GET http://192.168.4.1/ HTTP/1.1\r\n", ssid, pwd);
  TEST_ASSERT_EQUAL(1, result);
}

void test_mqtt_parse_ap_request_absolute_uri_config(void) {
  char ssid[33], pwd[65];
  int result = aqua_mqtt_parse_ap_request(
      "GET http://192.168.4.1/config?ssid=MyWiFi&pwd=MyPass HTTP/1.1\r\n",
      ssid, pwd);
  TEST_ASSERT_EQUAL(2, result);
  TEST_ASSERT_EQUAL_STRING("MyWiFi", ssid);
  TEST_ASSERT_EQUAL_STRING("MyPass", pwd);
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

 /* AP CWJAP */
  aqua_mqtt_start(&mqtt);
  feed_ok(&at); /* AT */
  aqua_mqtt_step(&mqtt);
  feed_ok(&at); /* ATE0 */
  aqua_mqtt_step(&mqtt);
  feed_ok(&at); /* CWMODE=1 */
  aqua_mqtt_step(&mqtt);
 /* 3 CWJAP */
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  feed_error(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_START, mqtt.state);

 /* AP_START -> AP_CIPMUX ( CWMODE=3 OK) */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_CIPMUX, mqtt.state);

 /* AP_CIPMUX -> AP_CIPDINFO ( CWSAP OK) */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_CIPDINFO, mqtt.state);

 /* AP_CIPDINFO -> AP_SERVER ( CIPMUX OK) */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_SERVER, mqtt.state);

 /* AP_SERVER -> AP_WAIT ( CIPDINFO OK CIPSERVER OK) */
  feed_ok(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_WAIT, mqtt.state);

 /* */
  const char *ipd =
      "+IPD,0,50:GET /config?ssid=NewSSID&pwd=NewPass HTTP/1.1\r\n";
  aqua_at_feed_rx(&at, (const uint8_t *)ipd, strlen(ipd));

 /* - AP_SENDING */
  bool got_config = aqua_mqtt_poll_ap_config(&mqtt);
  TEST_ASSERT_TRUE(got_config);
  TEST_ASSERT_EQUAL(MQTT_STATE_AP_SENDING, mqtt.state);

 /* */
  TEST_ASSERT_EQUAL_STRING("NewSSID", mqtt.config.wifi_ssid);
  TEST_ASSERT_EQUAL_STRING("NewPass", mqtt.config.wifi_password);

 /* app config_dirty */
  TEST_ASSERT_TRUE(app.state.config_dirty);
  TEST_ASSERT_EQUAL_STRING("NewSSID", app.state.config.wifi_ssid);
}

void test_mqtt_ap_wait_timeout_keeps_waiting(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  mqtt.state = MQTT_STATE_AP_WAIT;
  at.state = AT_STATE_DONE_TIMEOUT;

  aqua_mqtt_step(&mqtt);

  TEST_ASSERT_EQUAL(MQTT_STATE_AP_WAIT, mqtt.state);
  TEST_ASSERT_EQUAL(AT_STATE_IDLE, at.state);
}

void test_mqtt_cwmode_waiting_keeps_state(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  mqtt.state = MQTT_STATE_CWMODE;
  at.state = AT_STATE_WAITING;
  at.cmd_start_ms = 0;
  at.cmd_timeout_ms = 1000;

  aqua_mqtt_step(&mqtt);

  TEST_ASSERT_EQUAL(MQTT_STATE_CWMODE, mqtt.state);
}

void test_mqtt_cwjap_waiting_no_fail_increment(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  mqtt.state = MQTT_STATE_CWJAP;
  mqtt.cwjap_fail_count = 1;
  at.state = AT_STATE_WAITING;
  at.cmd_start_ms = 0;
  at.cmd_timeout_ms = 1000;

  aqua_mqtt_step(&mqtt);

  TEST_ASSERT_EQUAL(MQTT_STATE_CWJAP, mqtt.state);
  TEST_ASSERT_EQUAL(1, mqtt.cwjap_fail_count);
}

void test_mqtt_ap_start_waiting_keeps_state(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  mqtt.state = MQTT_STATE_AP_START;
  at.state = AT_STATE_WAITING;
  at.cmd_start_ms = 0;
  at.cmd_timeout_ms = 1000;

  aqua_mqtt_step(&mqtt);

  TEST_ASSERT_EQUAL(MQTT_STATE_AP_START, mqtt.state);
}

void test_mqtt_mqttsub_timeout_treated_online(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  mqtt.state = MQTT_STATE_MQTTSUB;
  at.state = AT_STATE_DONE_TIMEOUT;

  aqua_mqtt_step(&mqtt);

  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);
}

void test_mqtt_sntptime_time_updated_retries_query(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

  mqtt.state = MQTT_STATE_SNTPTIME;
  mqtt.retry_count = 0;
  at.state = AT_STATE_DONE_OK;
  strcpy(at.cmd_response.data, "+TIME_UPDATED");
  at.cmd_response.len = strlen(at.cmd_response.data);
  at.cmd_response.valid = true;

  aqua_mqtt_step(&mqtt);

  TEST_ASSERT_EQUAL(MQTT_STATE_SNTPTIME, mqtt.state);
  TEST_ASSERT_EQUAL(1, mqtt.retry_count);
  TEST_ASSERT_EQUAL(AT_STATE_WAITING, at.state);
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
 * 14 T 
 * ============================================================================
 */

void test_at_prompt_detection(void) {
  AtClient at;
  aqua_at_init(&at, mock_write, mock_now_ms);

 /* begin_with_prompt */
  aqua_at_begin_with_prompt(&at, "AT+CIPSEND=0,100", 5000);
  TEST_ASSERT_EQUAL(AT_STATE_WAITING, at.state);
  TEST_ASSERT_TRUE(at.expect_prompt);

 /* ESP-AT K > > */
  const char *rx = "OK\r\n>";
  aqua_at_feed_rx(&at, (const uint8_t *)rx, strlen(rx));
  aqua_at_step(&at);

 /* GOT_PROMPT */
  TEST_ASSERT_EQUAL(AT_STATE_GOT_PROMPT, at.state);
}

/* 15 > CRLF */
void test_at_prompt_bare(void) {
  AtClient at;
  aqua_at_init(&at, mock_write, mock_now_ms);

  aqua_at_begin_with_prompt(&at, "AT+CIPSEND=0,50", 5000);

 /* OK */
  feed_ok(&at);
  aqua_at_step(&at);
 TEST_ASSERT_EQUAL(AT_STATE_WAITING, at.state); /* > */
  TEST_ASSERT_TRUE(at.got_ok);

 /* > CRLF */
  feed_prompt_bare(&at);
  aqua_at_step(&at);
  TEST_ASSERT_EQUAL(AT_STATE_GOT_PROMPT, at.state);
}

/* ============================================================================
 * 14 RROR 
 * ============================================================================
 */

void test_mqtt_auto_reconnect_backoff(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "test");
  aqua_mqtt_init(&mqtt, &at, &app);

 /* ERROR */
  mqtt.state = MQTT_STATE_ERROR;
  mqtt.error_time_ms = 0;
  mqtt.reconnect_delay_ms = 0;

  g_mock_time_ms = 1000;
  aqua_mqtt_step(&mqtt);
 /* */
  TEST_ASSERT_EQUAL(MQTT_STATE_ERROR, mqtt.state);
  TEST_ASSERT_EQUAL(1000, mqtt.error_time_ms);
  TEST_ASSERT_EQUAL(RECONNECT_DELAY_INIT_MS, mqtt.reconnect_delay_ms);

 /* */
  g_mock_time_ms = 1000 + RECONNECT_DELAY_INIT_MS;
  aqua_mqtt_step(&mqtt);
 /* AT_TEST */
  TEST_ASSERT_EQUAL(MQTT_STATE_AT_TEST, mqtt.state);
 /* */
  TEST_ASSERT_EQUAL(RECONNECT_DELAY_INIT_MS * 2, mqtt.reconnect_delay_ms);
}

/* ============================================================================
 * 14 iFi 
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

 /* WiFi */
  aqua_mqtt_notify_wifi_changed(&mqtt);
  TEST_ASSERT_TRUE(mqtt.wifi_changed);

 /* */
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_AT_TEST, mqtt.state);
  TEST_ASSERT_FALSE(mqtt.wifi_changed);
}

/* ============================================================================
 * 14 
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
 * 20 set_config WiFi 
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

 /* set_config WiFi */
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
 /* wifi_changed */
  TEST_ASSERT_TRUE(mqtt.wifi_changed);
 /* */
  TEST_ASSERT_EQUAL_STRING("NewWiFi", mqtt.config.wifi_ssid);
  TEST_ASSERT_EQUAL_STRING("NewPass123", mqtt.config.wifi_password);

 /* ONLINE */
  feed_prompt(&at);
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUB_DATA, mqtt.state);

  const char *pub_ok = "+MQTTPUB:OK\r\n";
  aqua_at_feed_rx(&at, (const uint8_t *)pub_ok, strlen(pub_ok));
  aqua_mqtt_step(&mqtt);
  TEST_ASSERT_EQUAL(MQTT_STATE_ONLINE, mqtt.state);

 /* ONLINE */
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

 /* set_config WiFi */
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
 /* WiFi */
  TEST_ASSERT_FALSE(mqtt.wifi_changed);
}

void test_mqtt_set_config_same_wifi_no_reconnect(void) {
  AtClient at;
  AquariumApp app;
  MqttClient mqtt;

  reset_mocks();
  aqua_at_init(&at, mock_write, mock_now_ms);
  aqua_app_init(&app, "dev123");
  aqua_mqtt_init(&mqtt, &at, &app);
  mqtt.state = MQTT_STATE_ONLINE;

  strncpy(mqtt.config.wifi_ssid, "NewWiFi", sizeof(mqtt.config.wifi_ssid) - 1);
  strncpy(mqtt.config.wifi_password, "NewPass123",
          sizeof(mqtt.config.wifi_password) - 1);

  const char *payload =
      "{\"service_id\":\"aquariumConfig\","
      "\"command_name\":\"set_config\","
      "\"paras\":{\"wifi_ssid\":\"NewWiFi\",\"wifi_password\":\"NewPass123\"}}";
  char urc[512];
  snprintf(urc, sizeof(urc),
           "+MQTTSUBRECV:0,\"$oc/devices/dev123/sys/commands/"
           "request_id=r_same\",%zu,%s\r\n",
           strlen(payload), payload);

  aqua_at_feed_rx(&at, (const uint8_t *)urc, strlen(urc));

  bool handled = aqua_mqtt_poll_commands(&mqtt);
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL(MQTT_STATE_PUBLISHING, mqtt.state);
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

 /* set_config iFi SSID */
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
 /* SSID */
  TEST_ASSERT_FALSE(mqtt.wifi_changed);
}

/* ============================================================================
 * 
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_mqtt_init);
  RUN_TEST(test_mqtt_full_connect);
  RUN_TEST(test_mqtt_wifi_connect_fail);
  RUN_TEST(test_mqtt_placeholder_wifi_enters_ap_mode_directly);
  RUN_TEST(test_mqtt_cwjap_command_escapes_credentials);
  RUN_TEST(test_mqtt_sntpcfg_uses_multi_servers);
  RUN_TEST(test_mqtt_publish_online);
  RUN_TEST(test_mqtt_publish_not_online);
  RUN_TEST(test_mqtt_publish_completes);
  RUN_TEST(test_mqtt_publish_completes_with_plain_ok_only);
  RUN_TEST(test_mqtt_publish_timeout);
  RUN_TEST(test_mqtt_pub_data_preserves_subrecv_for_next_poll);
  RUN_TEST(test_mqtt_truncated_subrecv_still_handled);
  RUN_TEST(test_mqtt_truncated_subrecv_with_request_id_generates_error_response);
  RUN_TEST(test_mqtt_command_response_closed_loop);

 /* SNTP */
  RUN_TEST(test_mqtt_parse_sntp_time_valid);
  RUN_TEST(test_mqtt_parse_sntp_time_january);
  RUN_TEST(test_mqtt_parse_sntp_time_december);
  RUN_TEST(test_mqtt_parse_sntp_time_single_digit_day_with_double_spaces);
  RUN_TEST(test_mqtt_parse_sntp_time_invalid_null);
  RUN_TEST(test_mqtt_parse_sntp_time_invalid_format);

 /* AP */
  RUN_TEST(test_mqtt_cwjap_fail_enters_ap_mode);
  RUN_TEST(test_mqtt_parse_ap_request_home);
  RUN_TEST(test_mqtt_parse_ap_request_config);
  RUN_TEST(test_mqtt_parse_ap_request_url_encoded);
  RUN_TEST(test_mqtt_parse_ap_request_invalid);
  RUN_TEST(test_mqtt_parse_ap_request_unknown_path_fallback_home);
  RUN_TEST(test_mqtt_parse_ap_request_absolute_uri_home);
  RUN_TEST(test_mqtt_parse_ap_request_absolute_uri_config);
  RUN_TEST(test_mqtt_ap_full_flow);
  RUN_TEST(test_mqtt_ap_wait_timeout_keeps_waiting);
  RUN_TEST(test_mqtt_cwmode_waiting_keeps_state);
  RUN_TEST(test_mqtt_cwjap_waiting_no_fail_increment);
  RUN_TEST(test_mqtt_ap_start_waiting_keeps_state);
  RUN_TEST(test_mqtt_mqttsub_timeout_treated_online);
  RUN_TEST(test_mqtt_sntptime_time_updated_retries_query);
  RUN_TEST(test_mqtt_is_ap_mode);

 /* 14 */
  RUN_TEST(test_at_prompt_detection);
  RUN_TEST(test_mqtt_auto_reconnect_backoff);
  RUN_TEST(test_mqtt_wifi_change_triggers_reconnect);
  RUN_TEST(test_mqtt_get_net_status);

 /* 15 */
  RUN_TEST(test_at_prompt_bare);

 /* 20 set_config WiFi */
  RUN_TEST(test_mqtt_set_config_with_wifi_triggers_reconnect);
  RUN_TEST(test_mqtt_set_config_without_wifi_no_reconnect);
  RUN_TEST(test_mqtt_set_config_same_wifi_no_reconnect);
  RUN_TEST(test_mqtt_set_config_empty_ssid_no_reconnect);

  return UNITY_END();
}
