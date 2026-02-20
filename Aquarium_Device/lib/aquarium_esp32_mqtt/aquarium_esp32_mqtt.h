/**
 * @file aquarium_esp32_mqtt.h
 * @brief ESP32 AT WiFi/MQTT 鑱旈€氬眰
 *
 * 闈為樆濉炵姸鎬佹満锛歐iFi 杩炴帴 鈫?MQTT 閴存潈杩炴帴 鈫?璁㈤槄/鍙戝竷
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
 * 閰嶇疆甯搁噺
 * ============================================================================
 */

#define MQTT_BROKER_MAX_LEN 128
#define MQTT_TOPIC_MAX_LEN 256
#define MQTT_PAYLOAD_MAX_LEN 512

/* ============================================================================
 * 杩炴帴鐘舵€?
 * ============================================================================
 */

typedef enum {
  MQTT_STATE_IDLE = 0,    /* 鏈惎鍔?*/
  MQTT_STATE_AT_TEST,     /* AT 娴嬭瘯 */
  MQTT_STATE_ATE0,        /* 鍏抽棴鍥炴樉 */
  MQTT_STATE_CWMODE,      /* 璁剧疆 WiFi Station 妯″紡 */
  MQTT_STATE_CWJAP,       /* 杩炴帴 WiFi */
  MQTT_STATE_SNTPCFG,     /* 閰嶇疆 SNTP 鏈嶅姟鍣?*/
  MQTT_STATE_SNTPTIME,    /* 鏌ヨ SNTP 鏃堕棿 */
  MQTT_STATE_MQTTUSERCFG, /* 閰嶇疆 MQTT 鐢ㄦ埛 */
  MQTT_STATE_MQTTCONN,    /* 杩炴帴 MQTT Broker */
  MQTT_STATE_MQTTSUB,     /* 璁㈤槄鍛戒护 Topic */
  MQTT_STATE_ONLINE,      /* 鍦ㄧ嚎锛屽彲鏀跺彂 */
  MQTT_STATE_PUBLISHING,  /* 姝ｅ湪鍙戝竷 */
  MQTT_STATE_PUB_DATA,    /* 绛夊緟鍙戦€佹暟鎹?*/
  /* AP 閰嶇綉鐩稿叧鐘舵€?*/
  MQTT_STATE_AP_START,     /* 鍚姩 SoftAP 妯″紡 (CWMODE=3) */
  MQTT_STATE_AP_CIPMUX,    /* 閰嶇疆澶氳繛鎺?(CIPMUX=1) */
  MQTT_STATE_AP_CIPDINFO,  /* 閰嶇疆 IPD 鏍煎紡 (CIPDINFO=0) */
  MQTT_STATE_AP_SERVER,    /* 鍚姩 TCP 鏈嶅姟鍣?*/
  MQTT_STATE_AP_WAIT,      /* 绛夊緟鎵嬫満杩炴帴骞堕厤缃?*/
  MQTT_STATE_AP_SENDING,   /* 绛夊緟 AT+CIPSEND 鐨?OK/> */
  MQTT_STATE_AP_SEND_DATA, /* 鍙戦€佹暟鎹悗绛夊緟 SEND OK */
  MQTT_STATE_AP_CLOSE,     /* 鍏抽棴杩炴帴 (CIPCLOSE) */
  MQTT_STATE_AP_STOP,      /* 鍏抽棴鏈嶅姟鍣ㄥ苟閲嶆柊杩炴帴 WiFi */
  MQTT_STATE_ERROR         /* 閿欒鐘舵€?*/
} MqttConnState;

/* ============================================================================
 * MQTT 閰嶇疆
 * ============================================================================
 */

typedef struct {
  /* WiFi 閰嶇疆 */
  char wifi_ssid[33];
  char wifi_password[65];

  /* MQTT Broker 閰嶇疆 */
  char broker_host[MQTT_BROKER_MAX_LEN];
  uint16_t broker_port;

  /* 璁惧淇℃伅 */
  char device_id[65];
  char device_secret[65];
} MqttConfig;

/* ============================================================================
 * MQTT 瀹㈡埛绔笂涓嬫枃
 * ============================================================================
 */

typedef struct {
  MqttConnState state;
  MqttConfig config;
  AtClient *at;     /* AT 瀹㈡埛绔寚閽堬紙澶栭儴鎻愪緵锛?*/
  AquariumApp *app; /* 搴旂敤灞傛寚閽堬紙澶栭儴鎻愪緵锛?*/

  /* 鏃堕棿鎴筹紙鐢ㄤ簬閴存潈锛?*/
  char timestamp[12];

  /* 鍙戝竷缂撳啿 */
  char pub_topic[MQTT_TOPIC_MAX_LEN];
  char pub_payload[MQTT_PAYLOAD_MAX_LEN];
  size_t pub_payload_len;
  uint32_t pub_start_ms; /* 鍙戝竷寮€濮嬫椂闂达紙鐢ㄤ簬瓒呮椂妫€娴嬶級 */

  /* 閲嶈瘯璁℃暟 */
  uint8_t retry_count;
  uint8_t cwjap_fail_count; /* CWJAP 杩炵画澶辫触娆℃暟 */

  /* AP 閰嶇綉鍙戦€佺浉鍏?*/
  int ap_link_id;           /* 褰撳墠 HTTP 杩炴帴 ID */
  const char *ap_send_html; /* 寰呭彂閫佺殑 HTML 鍝嶅簲 */
  int ap_req_type;          /* 璇锋眰绫诲瀷锛?=棣栭〉, 2=閰嶇疆 */
  char ap_ssid[33];
  char ap_password[65];

  /* 鑷姩閲嶈繛鐩稿叧 */
  uint32_t error_time_ms;      /* 杩涘叆 ERROR 鐘舵€佺殑鏃堕棿 */
  uint32_t reconnect_delay_ms; /* 褰撳墠閲嶈繛閫€閬垮欢杩?*/
  bool wifi_changed;           /* WiFi 閰嶇疆宸插彉鏇达紝闇€瑕侀噸杩?*/
} MqttClient;

/* CWJAP 鏈€澶уけ璐ユ鏁帮紝瓒呰繃鍚庤繘鍏?AP 閰嶇綉 */
#define CWJAP_MAX_FAILS 3

/* 鑷姩閲嶈繛閫€閬垮弬鏁?*/
#define RECONNECT_DELAY_INIT_MS 2000 /* 鍒濆閫€閬垮欢杩?2s */
#define RECONNECT_DELAY_MAX_MS 60000 /* 鏈€澶ч€€閬垮欢杩?60s */
#define RECONNECT_DELAY_FACTOR 2     /* 閫€閬垮洜瀛?*/

/* ============================================================================
 * 鍒濆鍖栦笌鍚姩
 * ============================================================================
 */

/** @brief 鍒濆鍖?MQTT 瀹㈡埛绔?*/
void aqua_mqtt_init(MqttClient *mqtt, AtClient *at, AquariumApp *app);

/** @brief 璁剧疆杩炴帴閰嶇疆 */
void aqua_mqtt_set_config(MqttClient *mqtt, const MqttConfig *cfg);

/** @brief 璁剧疆鏃堕棿鎴筹紙鐢ㄤ簬閴存潈锛?*/
void aqua_mqtt_set_timestamp(MqttClient *mqtt, const char *ts);

/** @brief 设置 AP 配网 SSID/密码 */
void aqua_mqtt_set_ap_credentials(MqttClient *mqtt, const char *ssid,
                                  const char *password);

/** @brief 寮€濮嬭繛鎺ユ祦绋?*/
void aqua_mqtt_start(MqttClient *mqtt);

/** @brief 鎺ㄨ繘鐘舵€佹満锛堜富寰幆璋冪敤锛?*/
MqttConnState aqua_mqtt_step(MqttClient *mqtt);

/** @brief 鑾峰彇褰撳墠鐘舵€?*/
MqttConnState aqua_mqtt_get_state(const MqttClient *mqtt);

/**
 * @brief 閫氱煡 WiFi 閰嶇疆宸插彉鏇达紝瑙﹀彂閲嶈繛
 *
 * 鍦?ONLINE 鐘舵€佷笅璋冪敤锛屼細瑙﹀彂鏂紑鈫掗噸杩炴祦绋?
 * 搴斿湪鏇存柊 mqtt->config 鐨?wifi_ssid/wifi_password 鍚庤皟鐢?
 */
void aqua_mqtt_notify_wifi_changed(MqttClient *mqtt);

/**
 * @brief 鑾峰彇缃戠粶杩炴帴鐘舵€侊紙绠€鍖栫増锛?
 * @return 0=绂荤嚎/閿欒, 1=杩炴帴涓? 2=鍦ㄧ嚎, 3=AP閰嶇綉涓?
 */
int aqua_mqtt_get_net_status(const MqttClient *mqtt);

/** @brief 璇锋眰鍙戝竷娑堟伅锛圤nline 鐘舵€佷笅璋冪敤锛?*/
bool aqua_mqtt_publish(MqttClient *mqtt, const char *topic, const char *payload,
                       size_t len);

/**
 * @brief 杞澶勭悊涓嬭鍛戒护
 *
 * 妫€鏌?URC 闃熷垪涓殑 +MQTTSUBRECV锛岃В鏋愬悗璋冪敤 app 灞傚鐞?
 * 濡傛湁鍝嶅簲鍒欒嚜鍔ㄥ洖鍙?
 *
 * @return true 濡傛灉澶勭悊浜嗚嚦灏戜竴鏉″懡浠?
 */
bool aqua_mqtt_poll_commands(MqttClient *mqtt);

/**
 * @brief AP 閰嶇綉妯″紡涓嬭疆璇㈠鐞?HTTP 璇锋眰
 *
 * 鍦?MQTT_STATE_AP_WAIT 鐘舵€佷笅璋冪敤锛屽鐞嗘潵鑷墜鏈虹殑閰嶇疆璇锋眰
 *
 * @return true 濡傛灉鎴愬姛鏀跺埌閰嶇疆骞跺噯澶囬€€鍑?AP 妯″紡
 */
bool aqua_mqtt_poll_ap_config(MqttClient *mqtt);

/**
 * @brief 瑙ｆ瀽 SNTP 鏃堕棿瀛楃涓蹭负 IoTDA 鏃堕棿鎴虫牸寮?
 *
 * 灏?"+CIPSNTPTIME:Mon Oct 18 20:12:27 2021" 鏍煎紡瑙ｆ瀽涓?"YYYYMMDDHH"
 *
 * @param sntp_line 鏉ヨ嚜 AT+CIPSNTPTIME? 鐨勫搷搴旇
 * @param out_ts    杈撳嚭缂撳啿鍖猴紝鑷冲皯 11 瀛楄妭锛?0瀛楃+null锛?
 * @return true 瑙ｆ瀽鎴愬姛
 */
bool aqua_mqtt_parse_sntp_time(const char *sntp_line, char *out_ts);

/**
 * @brief 瑙ｆ瀽 AP 閰嶇綉鐨?HTTP 璇锋眰
 *
 * 鏀寔锛?
 * - GET / 杩斿洖閰嶇疆椤?
 * - GET /config?ssid=...&pwd=... 鎻愬彇閰嶇疆
 *
 * @param http_req HTTP 璇锋眰鏁版嵁
 * @param out_ssid [杈撳嚭] SSID 缂撳啿鍖猴紙鑷冲皯 33 瀛楄妭锛?
 * @param out_pwd  [杈撳嚭] 瀵嗙爜缂撳啿鍖猴紙鑷冲皯 65 瀛楄妭锛?
 * @return 1=璇锋眰棣栭〉, 2=鎻愪氦閰嶇疆鎴愬姛, 0=鏃犳晥/闈炴硶璇锋眰
 */
int aqua_mqtt_parse_ap_request(const char *http_req, char *out_ssid,
                               char *out_pwd);

/**
 * @brief 妫€鏌ユ槸鍚﹀浜?AP 閰嶇綉妯″紡
 */
bool aqua_mqtt_is_ap_mode(const MqttClient *mqtt);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_ESP32_MQTT_H */
