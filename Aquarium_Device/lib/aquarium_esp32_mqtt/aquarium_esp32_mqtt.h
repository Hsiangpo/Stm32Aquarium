/**
 * @file aquarium_esp32_mqtt.h
 * @brief ESP32 AT WiFi/MQTT 
 *
 * iFi MQTT / 
 */

#ifndef AQUARIUM_ESP32_MQTT_H
#define AQUARIUM_ESP32_MQTT_H

#include "aquarium_app.h"
#include "aquarium_at.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 
 * ============================================================================
 */

#define MQTT_BROKER_MAX_LEN 128
#define MQTT_TOPIC_MAX_LEN 256
#define MQTT_PAYLOAD_MAX_LEN 512

/* ============================================================================
 * 
 * ============================================================================
 */

typedef enum {
 MQTT_STATE_IDLE = 0, /* */
 MQTT_STATE_AT_TEST, /* AT */
 MQTT_STATE_ATE0, /* */
 MQTT_STATE_CWMODE, /* WiFi Station */
 MQTT_STATE_CWJAP, /* WiFi */
 MQTT_STATE_SNTPCFG, /* SNTP */
 MQTT_STATE_SNTPTIME, /* SNTP */
 MQTT_STATE_MQTTUSERCFG, /* MQTT */
 MQTT_STATE_MQTTCONN, /* MQTT Broker */
 MQTT_STATE_MQTTSUB, /* Topic */
 MQTT_STATE_ONLINE, /* */
 MQTT_STATE_PUBLISHING, /* */
 MQTT_STATE_PUB_DATA, /* */
 /* AP */
 MQTT_STATE_AP_START, /* SoftAP (CWMODE=3) */
 MQTT_STATE_AP_CIPMUX, /* (CIPMUX=1) */
 MQTT_STATE_AP_CIPDINFO, /* IPD (CIPDINFO=0) */
 MQTT_STATE_AP_SERVER, /* TCP */
 MQTT_STATE_AP_WAIT, /* */
 MQTT_STATE_AP_SENDING, /* AT+CIPSEND OK/> */
 MQTT_STATE_AP_SEND_DATA, /* SEND OK */
 MQTT_STATE_AP_CLOSE, /* (CIPCLOSE) */
 MQTT_STATE_AP_STOP, /* WiFi */
 MQTT_STATE_ERROR /* */
} MqttConnState;

/* ============================================================================
 * MQTT 
 * ============================================================================
 */

typedef struct {
 /* WiFi */
  char wifi_ssid[33];
  char wifi_password[65];

 /* MQTT Broker */
  char broker_host[MQTT_BROKER_MAX_LEN];
  uint16_t broker_port;

 /* */
  char device_id[65];
  char device_secret[65];
} MqttConfig;

/* ============================================================================
 * MQTT 
 * ============================================================================
 */

typedef struct {
  MqttConnState state;
  MqttConfig config;
 AtClient *at; /* AT */
 AquariumApp *app; /* */

 /* */
  char timestamp[12];

 /* */
  char pub_topic[MQTT_TOPIC_MAX_LEN];
  char pub_payload[MQTT_PAYLOAD_MAX_LEN];
  size_t pub_payload_len;
 uint32_t pub_start_ms; /* */

 /* */
  uint8_t retry_count;
 uint8_t cwjap_fail_count; /* CWJAP */

 /* AP */
 int ap_link_id; /* HTTP ID */
 const char *ap_send_html; /* HTML */
 int ap_req_type; /* = , 2= */
  char ap_ssid[33];
  char ap_password[65];

 /* */
 uint32_t error_time_ms; /* ERROR */
 uint32_t reconnect_delay_ms; /* */
 bool wifi_changed; /* WiFi */
} MqttClient;

/* CWJAP AP */
#define CWJAP_MAX_FAILS 3

/* */
#define RECONNECT_DELAY_INIT_MS 2000 /* 2s */
#define RECONNECT_DELAY_MAX_MS 60000 /* 60s */
#define RECONNECT_DELAY_FACTOR 2 /* */

/* ============================================================================
 * 
 * ============================================================================
 */

/** @brief MQTT */
void aqua_mqtt_init(MqttClient *mqtt, AtClient *at, AquariumApp *app);

/** @brief */
void aqua_mqtt_set_config(MqttClient *mqtt, const MqttConfig *cfg);

/** @brief */
void aqua_mqtt_set_timestamp(MqttClient *mqtt, const char *ts);

/** @brief AP SSID/ */
void aqua_mqtt_set_ap_credentials(MqttClient *mqtt, const char *ssid,
                                  const char *password);

/** @brief */
void aqua_mqtt_start(MqttClient *mqtt);

/** @brief */
MqttConnState aqua_mqtt_step(MqttClient *mqtt);

/** @brief */
MqttConnState aqua_mqtt_get_state(const MqttClient *mqtt);

/**
 * @brief WiFi 
 *
 * ONLINE 
 * mqtt->config wifi_ssid/wifi_password 
 */
void aqua_mqtt_notify_wifi_changed(MqttClient *mqtt);

/**
 * @brief 
 * @return 0= / , 1= 2= , 3=AP 
 */
int aqua_mqtt_get_net_status(const MqttClient *mqtt);

/** @brief nline */
bool aqua_mqtt_publish(MqttClient *mqtt, const char *topic, const char *payload,
                       size_t len);

/**
 * @brief 
 *
 * URC +MQTTSUBRECV app 
 * 
 *
 * @return true 
 */
bool aqua_mqtt_poll_commands(MqttClient *mqtt);

/**
 * @brief AP HTTP 
 *
 * MQTT_STATE_AP_WAIT 
 *
 * @return true AP 
 */
bool aqua_mqtt_poll_ap_config(MqttClient *mqtt);

/**
 * @brief SNTP IoTDA 
 *
 * "+CIPSNTPTIME:Mon Oct 18 20:12:27 2021" "YYYYMMDDHH"
 *
 * @param sntp_line AT+CIPSNTPTIME 
 * @param out_ts 11 0 +null 
 * @return true 
 */
bool aqua_mqtt_parse_sntp_time(const char *sntp_line, char *out_ts);

/**
 * @brief AP HTTP 
 *
 * 
 * - GET / 
 * - GET /configssid=...&pwd=... 
 *
 * @param http_req HTTP 
 * @param out_ssid [ ] SSID 33 
 * @param out_pwd [ ] 65 
 * @return 1= , 2= , 0= / 
 */
int aqua_mqtt_parse_ap_request(const char *http_req, char *out_ssid,
                               char *out_pwd);

/**
 * @brief AP 
 */
bool aqua_mqtt_is_ap_mode(const MqttClient *mqtt);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_ESP32_MQTT_H */
