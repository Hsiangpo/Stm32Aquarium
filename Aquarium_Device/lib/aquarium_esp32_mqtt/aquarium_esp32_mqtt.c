/**
 * @file aquarium_esp32_mqtt.c
 * @brief ESP32 AT WiFi/MQTT 鑱旈€氬眰瀹炵幇
 */

#include "aquarium_esp32_mqtt.h"
#include "aquarium_iotda_auth.h"
#include "aquarium_protocol.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * 鍐呴儴甯搁噺
 * ============================================================================
 */

#define AT_TIMEOUT_SHORT 2000
#define AT_TIMEOUT_WIFI 15000
#define AT_TIMEOUT_MQTT 10000
#define PUB_DATA_TIMEOUT_MS 5000 /* +MQTTPUB:OK 绛夊緟瓒呮椂 */
#define AT_TIMEOUT_SNTP 5000     /* SNTP 鍛戒护瓒呮椂 */

/* AP 閰嶇綉鐩稿叧甯搁噺 */
#define AP_SSID_DEFAULT "Aquarium_Setup"
#define AP_PASSWORD_DEFAULT "12345678"
#define AP_SERVER_PORT 80

/* 鏈堜唤缂╁啓鏄犲皠琛?*/
static const char *MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/* ============================================================================
 * 鍒濆鍖?
 * ============================================================================
 */

void aqua_mqtt_init(MqttClient *mqtt, AtClient *at, AquariumApp *app) {
  if (!mqtt)
    return;
  memset(mqtt, 0, sizeof(MqttClient));
  mqtt->at = at;
  mqtt->app = app;
  mqtt->state = MQTT_STATE_IDLE;
}

void aqua_mqtt_set_config(MqttClient *mqtt, const MqttConfig *cfg) {
  if (!mqtt || !cfg)
    return;
  memcpy(&mqtt->config, cfg, sizeof(MqttConfig));
}

void aqua_mqtt_set_timestamp(MqttClient *mqtt, const char *ts) {
  if (!mqtt || !ts)
    return;
  strncpy(mqtt->timestamp, ts, sizeof(mqtt->timestamp) - 1);
  mqtt->timestamp[sizeof(mqtt->timestamp) - 1] = '\0';
}

void aqua_mqtt_set_ap_credentials(MqttClient *mqtt, const char *ssid,
                                  const char *password) {
  if (!mqtt)
    return;

  if (ssid && ssid[0] != '\0') {
    strncpy(mqtt->ap_ssid, ssid, sizeof(mqtt->ap_ssid) - 1);
    mqtt->ap_ssid[sizeof(mqtt->ap_ssid) - 1] = '\0';
  } else {
    mqtt->ap_ssid[0] = '\0';
  }

  if (password && password[0] != '\0') {
    strncpy(mqtt->ap_password, password, sizeof(mqtt->ap_password) - 1);
    mqtt->ap_password[sizeof(mqtt->ap_password) - 1] = '\0';
  } else {
    mqtt->ap_password[0] = '\0';
  }
}

void aqua_mqtt_start(MqttClient *mqtt) {
  if (!mqtt || !mqtt->at)
    return;
  mqtt->state = MQTT_STATE_AT_TEST;
  mqtt->retry_count = 0;
  aqua_at_begin(mqtt->at, "AT", AT_TIMEOUT_SHORT);
}

MqttConnState aqua_mqtt_get_state(const MqttClient *mqtt) {
  if (!mqtt)
    return MQTT_STATE_IDLE;
  return mqtt->state;
}

/* ============================================================================
 * 鍙戝竷
 * ============================================================================
 */

bool aqua_mqtt_publish(MqttClient *mqtt, const char *topic, const char *payload,
                       size_t len) {
  if (!mqtt || !topic || !payload)
    return false;
  if (mqtt->state != MQTT_STATE_ONLINE)
    return false;
  if (len > MQTT_PAYLOAD_MAX_LEN - 1)
    return false;

  /* 瀹夊叏澶嶅埗 topic锛岀‘淇?null 缁堟 */
  strncpy(mqtt->pub_topic, topic, MQTT_TOPIC_MAX_LEN - 1);
  mqtt->pub_topic[MQTT_TOPIC_MAX_LEN - 1] = '\0';

  memcpy(mqtt->pub_payload, payload, len);
  mqtt->pub_payload[len] = '\0';
  mqtt->pub_payload_len = len;

  /* 鍙戦€?AT+MQTTPUBRAW锛屼娇鐢ㄨ冻澶熷ぇ鐨勭紦鍐插尯 */
  /* 杩欎釜鍛戒护鍝嶅簲椤哄簭鏄?OK -> >锛岄渶瑕佷娇鐢?begin_with_prompt */
  char cmd[MQTT_TOPIC_MAX_LEN + 64];
  snprintf(cmd, sizeof(cmd), "AT+MQTTPUBRAW=0,\"%s\",%zu,0,0", mqtt->pub_topic,
           mqtt->pub_payload_len);
  aqua_at_begin_with_prompt(mqtt->at, cmd, AT_TIMEOUT_MQTT);
  mqtt->pub_start_ms = mqtt->at->now_ms_func();
  mqtt->state = MQTT_STATE_PUBLISHING;
  return true;
}

/* ============================================================================
 * 鐘舵€佹満姝ヨ繘
 * ============================================================================
 */

MqttConnState aqua_mqtt_step(MqttClient *mqtt) {
  if (!mqtt || !mqtt->at)
    return MQTT_STATE_ERROR;

  AtState at_state = aqua_at_step(mqtt->at);
  char cmd[256];

  /* 绛夊緟 AT 鍛戒护瀹屾垚 */
  if (at_state == AT_STATE_WAITING) {
    return mqtt->state;
  }

  switch (mqtt->state) {
  case MQTT_STATE_IDLE:
    break;

  case MQTT_STATE_AT_TEST:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      aqua_at_begin(mqtt->at, "ATE0", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_ATE0;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_ATE0:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      aqua_at_begin(mqtt->at, "AT+CWMODE=1", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_CWMODE;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_CWMODE:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"",
               mqtt->config.wifi_ssid, mqtt->config.wifi_password);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_WIFI);
      mqtt->state = MQTT_STATE_CWJAP;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_CWJAP:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      mqtt->cwjap_fail_count = 0; /* 鎴愬姛锛屾竻闆跺け璐ヨ鏁?*/
      /* WiFi 杩炴帴鎴愬姛锛岄厤缃?SNTP 鑾峰彇缃戠粶鏃堕棿 */
      /* IoTDA MQTT 閴存潈鏃堕棿鎴宠姹?UTC(YYYYMMDDHH) */
      aqua_at_begin(mqtt->at,
                    "AT+CIPSNTPCFG=1,0,\"ntp.aliyun.com\",\"ntp.ntsc.ac.cn\"",
                    AT_TIMEOUT_SNTP);
      mqtt->state = MQTT_STATE_SNTPCFG;
    } else {
      mqtt->cwjap_fail_count++;
      aqua_at_reset(mqtt->at);
      if (mqtt->cwjap_fail_count >= CWJAP_MAX_FAILS) {
        /* 瓒呰繃鏈€澶ч噸璇曟鏁帮紝杩涘叆 AP 閰嶇綉妯″紡 */
        /* 璁剧疆涓?AP+STA 妯″紡 */
        aqua_at_begin(mqtt->at, "AT+CWMODE=3", AT_TIMEOUT_SHORT);
        mqtt->state = MQTT_STATE_AP_START;
      } else {
        /* 閲嶈瘯杩炴帴 */
        snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"",
                 mqtt->config.wifi_ssid, mqtt->config.wifi_password);
        aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_WIFI);
      }
    }
    break;

  case MQTT_STATE_SNTPCFG:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      /* SNTP 閰嶇疆鎴愬姛锛屾煡璇㈡椂闂?*/
      aqua_at_begin(mqtt->at, "AT+CIPSNTPTIME?", AT_TIMEOUT_SNTP);
      mqtt->state = MQTT_STATE_SNTPTIME;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_SNTPTIME:
    if (at_state == AT_STATE_DONE_OK) {
      /* 瑙ｆ瀽鏃堕棿骞惰缃椂闂存埑 */
      const AtLine *resp = aqua_at_get_response(mqtt->at);
      char ts[12];
      bool ts_ok = (resp && aqua_mqtt_parse_sntp_time(resp->data, ts) &&
                    strlen(ts) == 10);
      if (ts_ok) {
        aqua_mqtt_set_timestamp(mqtt, ts);
      }
      aqua_at_reset(mqtt->at);
      /* SNTP 瑙ｆ瀽澶辫触锛氫笉瑕佺户缁敤鏃?timestamp 閴存潈锛岀洿鎺ヨ繘鍏?ERROR 瑙﹀彂閲嶈繛 */
      if (!ts_ok || strlen(mqtt->timestamp) != 10) {
        mqtt->state = MQTT_STATE_ERROR;
        break;
      }
      /* 鏋勫缓閴存潈鍙傛暟 */
      char client_id[128];
      char password[65];
      aqua_iotda_build_client_id(mqtt->config.device_id, IOTDA_SIGN_TYPE_CHECK,
                                 mqtt->timestamp, client_id, sizeof(client_id));
      aqua_iotda_build_password(mqtt->config.device_secret, mqtt->timestamp,
                                password);
      snprintf(cmd, sizeof(cmd),
               "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"", client_id,
               mqtt->config.device_id, password);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_MQTTUSERCFG;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_MQTTUSERCFG:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      snprintf(cmd, sizeof(cmd), "AT+MQTTCONN=0,\"%s\",%u,1",
               mqtt->config.broker_host, mqtt->config.broker_port);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_MQTT);
      mqtt->state = MQTT_STATE_MQTTCONN;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_MQTTCONN:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      snprintf(cmd, sizeof(cmd),
               "AT+MQTTSUB=0,\"$oc/devices/%s/sys/commands/#\",1",
               mqtt->config.device_id);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_MQTT);
      mqtt->state = MQTT_STATE_MQTTSUB;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_MQTTSUB:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      mqtt->state = MQTT_STATE_ONLINE;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_PUBLISHING:
    /*
     * AT+MQTTPUBRAW 娴佺▼锛?
     * 1. 鍙戦€佸懡浠?鈫?绛夊緟 > 鎻愮ず绗?
     * 2. 鍙戦€?payload 鏁版嵁
     * 3. 绛夊緟 +MQTTPUB:OK 鎴?+MQTTPUB:FAIL
     */
    if (at_state == AT_STATE_GOT_PROMPT) {
      /* 鏀跺埌 > 鎻愮ず绗︼紝鍙戦€?payload 鏁版嵁锛堜笉鐢?\r\n 缁撳熬锛?*/
      mqtt->at->write_func((const uint8_t *)mqtt->pub_payload,
                           mqtt->pub_payload_len);
      mqtt->state = MQTT_STATE_PUB_DATA;
      /* 閲嶇疆 AT 鐘舵€侊紝绛夊緟 URC */
      aqua_at_reset(mqtt->at);
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_PUB_DATA: {
    /*
     * 妫€鏌?URC 闃熷垪涓槸鍚︽湁 +MQTTPUB:OK 鎴?+MQTTPUB:FAIL
     */
    if (aqua_at_has_urc(mqtt->at)) {
      AtLine urc;
      while (aqua_at_pop_line(mqtt->at, &urc) == AT_OK) {
        if (strstr(urc.data, "+MQTTPUB:OK") != NULL) {
          mqtt->state = MQTT_STATE_ONLINE;
          break;
        } else if (strstr(urc.data, "+MQTTPUB:FAIL") != NULL) {
          mqtt->state = MQTT_STATE_ERROR;
          break;
        }
      }
    }

    /* 瓒呮椂妫€娴?*/
    if (mqtt->state == MQTT_STATE_PUB_DATA) {
      uint32_t now = mqtt->at->now_ms_func();
      if (now - mqtt->pub_start_ms >= PUB_DATA_TIMEOUT_MS) {
        mqtt->state = MQTT_STATE_ERROR;
      }
    }
    break;
  }

  /* ====================== AP 閰嶇綉鐘舵€佹満 ====================== */
  case MQTT_STATE_AP_START:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      /* 閰嶇疆 SoftAP */
      const char *ap_ssid =
          mqtt->ap_ssid[0] != '\0' ? mqtt->ap_ssid : AP_SSID_DEFAULT;
      const char *ap_pwd = mqtt->ap_password[0] != '\0' ? mqtt->ap_password
                                                        : AP_PASSWORD_DEFAULT;
      snprintf(cmd, sizeof(cmd), "AT+CWSAP=\"%s\",\"%s\",1,3", ap_ssid,
               ap_pwd);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AP_CIPMUX;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_AP_CIPMUX:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      /* 鍚敤澶氳繛鎺ユā寮忥紙TCP 鏈嶅姟鍣ㄩ渶瑕侊級 */
      aqua_at_begin(mqtt->at, "AT+CIPMUX=1", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AP_CIPDINFO;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_AP_CIPDINFO:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      /* 璁剧疆 +IPD 鏍煎紡涓虹畝鍗曟牸寮忥細+IPD,<link_id>,<len>:<data> */
      aqua_at_begin(mqtt->at, "AT+CIPDINFO=0", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AP_SERVER;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_AP_SERVER:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      /* 鍚姩 TCP 鏈嶅姟鍣?*/
      snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%d", AP_SERVER_PORT);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AP_WAIT;
    } else {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_AP_WAIT:
    /* 绛夊緟 AT+CIPSERVER OK 鍚庤繘鍏ョ┖闂茬瓑寰?*/
    /* HTTP 璇锋眰鐢?aqua_mqtt_poll_ap_config() 澶勭悊 */
    /* 璇ュ嚱鏁颁細璁剧疆 ap_link_id, ap_send_html, ap_req_type 骞惰浆鎹㈢姸鎬?*/
    break;

  case MQTT_STATE_AP_SENDING:
    /* 绛夊緟 AT+CIPSEND 杩斿洖 > 鎻愮ず绗︼紙琛ㄧず鍙互鍙戦€佹暟鎹級 */
    if (at_state == AT_STATE_GOT_PROMPT) {
      /* 鍙戦€?HTML 鏁版嵁 */
      if (mqtt->ap_send_html) {
        mqtt->at->write_func((const uint8_t *)mqtt->ap_send_html,
                             strlen(mqtt->ap_send_html));
      }
      aqua_at_reset(mqtt->at);
      mqtt->state = MQTT_STATE_AP_SEND_DATA;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      /* CIPSEND 澶辫触锛岀洿鎺ュ叧闂繛鎺?*/
      aqua_at_reset(mqtt->at);
      snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d", mqtt->ap_link_id);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AP_CLOSE;
    }
    break;

  case MQTT_STATE_AP_SEND_DATA: {
    /* 绛夊緟 SEND OK 鎴?SEND FAIL锛堥€氳繃 URC 妫€娴嬶級 */
    if (aqua_at_has_urc(mqtt->at)) {
      AtLine urc;
      while (aqua_at_pop_line(mqtt->at, &urc) == AT_OK) {
        if (strstr(urc.data, "SEND OK") != NULL ||
            strstr(urc.data, "SEND FAIL") != NULL) {
          /* 鍙戦€佸畬鎴愶紝鍏抽棴杩炴帴 */
          aqua_at_reset(mqtt->at);
          snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d", mqtt->ap_link_id);
          aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
          mqtt->state = MQTT_STATE_AP_CLOSE;
          break;
        }
      }
    }
    break;
  }

  case MQTT_STATE_AP_CLOSE:
    /* CIPCLOSE 瀹屾垚鍚庡喅瀹氫笅涓€姝?*/
    if (at_state == AT_STATE_DONE_OK || at_state == AT_STATE_DONE_ERROR) {
      aqua_at_reset(mqtt->at);
      if (mqtt->ap_req_type == 2) {
        /* 閰嶇疆宸叉彁浜わ紝鍏抽棴鏈嶅姟鍣ㄥ苟閲嶈繛 WiFi */
        aqua_at_begin(mqtt->at, "AT+CIPSERVER=0", AT_TIMEOUT_SHORT);
        mqtt->state = MQTT_STATE_AP_STOP;
      } else {
        /* 缁х画绛夊緟涓嬩竴涓姹?*/
        mqtt->state = MQTT_STATE_AP_WAIT;
      }
    }
    break;

  case MQTT_STATE_AP_STOP:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      /* 鍒囧洖 STA 妯″紡骞堕噸璇?WiFi 杩炴帴 */
      aqua_at_begin(mqtt->at, "AT+CWMODE=1", AT_TIMEOUT_SHORT);
      mqtt->cwjap_fail_count = 0;
      mqtt->state = MQTT_STATE_CWMODE;
    } else {
      /* 缁х画绛夊緟 */
    }
    break;

  case MQTT_STATE_ONLINE:
    /* 妫€鏌?WiFi 閰嶇疆鏄惁鍙樻洿锛岄渶瑕侀噸杩?*/
    if (mqtt->wifi_changed) {
      mqtt->wifi_changed = false;
      mqtt->cwjap_fail_count = 0;
      mqtt->reconnect_delay_ms = RECONNECT_DELAY_INIT_MS;
      aqua_at_reset(mqtt->at);
      /* 鍏堟柇寮€ MQTT */
      aqua_at_begin(mqtt->at, "AT+MQTTCLEAN=0", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AT_TEST; /* 浠庡ご閲嶈繛 */
    }
    break;

  case MQTT_STATE_ERROR: {
    /* 鑷姩閲嶈繛锛氬甫閫€閬跨殑寤惰繜閲嶈瘯 */
    uint32_t now = mqtt->at->now_ms_func();

    /* 棣栨杩涘叆 ERROR锛岃褰曟椂闂村苟璁剧疆鍒濆閫€閬?*/
    if (mqtt->error_time_ms == 0) {
      mqtt->error_time_ms = now;
      if (mqtt->reconnect_delay_ms == 0) {
        mqtt->reconnect_delay_ms = RECONNECT_DELAY_INIT_MS;
      }
    }

    /* 妫€鏌ユ槸鍚﹀埌杈鹃€€閬挎椂闂?*/
    if (now - mqtt->error_time_ms >= mqtt->reconnect_delay_ms) {
      /* 澧炲姞閫€閬垮欢杩燂紙鎸囨暟閫€閬匡級 */
      mqtt->reconnect_delay_ms *= RECONNECT_DELAY_FACTOR;
      if (mqtt->reconnect_delay_ms > RECONNECT_DELAY_MAX_MS) {
        mqtt->reconnect_delay_ms = RECONNECT_DELAY_MAX_MS;
      }
      /* 閲嶇疆骞跺紑濮嬮噸杩?*/
      mqtt->error_time_ms = 0;
      mqtt->cwjap_fail_count = 0;
      aqua_at_reset(mqtt->at);
      aqua_at_begin(mqtt->at, "AT", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AT_TEST;
    }
    break;
  }
  }

  return mqtt->state;
}

/* ============================================================================
 * 涓嬭鍛戒护澶勭悊
 * ============================================================================
 */

/**
 * 瑙ｆ瀽 +MQTTSUBRECV:<LinkID>,"<topic>",<data_len>,<data>
 * 杩斿洖 true 琛ㄧず瑙ｆ瀽鎴愬姛
 */
static bool parse_mqttsubrecv(const char *line, char *out_topic,
                              size_t topic_size, char *out_payload,
                              size_t payload_size) {
  /* 鏌ユ壘 +MQTTSUBRECV: */
  const char *p = strstr(line, "+MQTTSUBRECV:");
  if (!p)
    return false;
  p += 13; /* 璺宠繃 +MQTTSUBRECV: */

  /* 璺宠繃 LinkID 鍜岄€楀彿 */
  while (*p && *p != ',')
    p++;
  if (*p == ',')
    p++;

  /* 瑙ｆ瀽 topic锛堝湪寮曞彿鍐咃級 */
  if (*p != '"')
    return false;
  p++;
  const char *topic_start = p;
  while (*p && *p != '"')
    p++;
  size_t topic_len = (size_t)(p - topic_start);
  if (topic_len >= topic_size)
    topic_len = topic_size - 1;
  memcpy(out_topic, topic_start, topic_len);
  out_topic[topic_len] = '\0';

  if (*p == '"')
    p++;
  if (*p == ',')
    p++;

  /* 瑙ｆ瀽 data_len */
  int data_len = 0;
  while (*p >= '0' && *p <= '9') {
    data_len = data_len * 10 + (*p - '0');
    p++;
  }
  if (*p == ',')
    p++;

  /* 鏍￠獙瀹為檯鍙敤鏁版嵁闀垮害锛岄槻姝㈡埅鏂椂瓒婄晫 */
  size_t available_len = strlen(p);
  if ((size_t)data_len > available_len) {
    /* 鏁版嵁琚埅鏂紝鎷掔粷瑙ｆ瀽 */
    return false;
  }

  /* 澶嶅埗 payload */
  size_t copy_len = (size_t)data_len;
  if (copy_len >= payload_size)
    copy_len = payload_size - 1;
  memcpy(out_payload, p, copy_len);
  out_payload[copy_len] = '\0';

  return true;
}

bool aqua_mqtt_poll_commands(MqttClient *mqtt) {
  if (!mqtt || !mqtt->at || !mqtt->app)
    return false;
  if (mqtt->state != MQTT_STATE_ONLINE)
    return false;

  bool handled = false;
  AtLine urc;

  while (aqua_at_pop_line(mqtt->at, &urc) == AT_OK) {
    /* 妫€鏌ユ槸鍚︽槸 +MQTTSUBRECV */
    if (strstr(urc.data, "+MQTTSUBRECV:") == NULL)
      continue;

    char topic[MQTT_TOPIC_MAX_LEN];
    char payload[MQTT_PAYLOAD_MAX_LEN];

    if (!parse_mqttsubrecv(urc.data, topic, sizeof(topic), payload,
                           sizeof(payload))) {
      continue;
    }

    /* 瑙ｆ瀽鍛戒护浠ュ垽鏂槸鍚︽秹鍙?WiFi 鍙樻洿 */
    ParsedCommand cmd;
    bool wifi_change_needed = false;
    if (aqua_parse_command_json(payload, strlen(payload), &cmd) == AQUA_OK) {
      if (cmd.type == COMMAND_TYPE_SET_CONFIG &&
          cmd.params.config.has_wifi_ssid &&
          cmd.params.config.has_wifi_password &&
          cmd.params.config.wifi_ssid[0] != '\0') {
        wifi_change_needed = true;
      }
    }

    /* 璋冪敤 app 灞傚鐞嗗懡浠?*/
    char resp_topic[MQTT_TOPIC_MAX_LEN];
    char resp_payload[MQTT_PAYLOAD_MAX_LEN];
    bool has_response = false;

    AquaError err = aqua_app_on_mqtt_command(
        mqtt->app, topic, payload, strlen(payload), &has_response, resp_topic,
        sizeof(resp_topic), resp_payload, sizeof(resp_payload));

    /* 濡傛灉鏈夊搷搴斾笖闇€瑕佸洖鍙?*/
    if (err == AQUA_OK && has_response) {
      aqua_mqtt_publish(mqtt, resp_topic, resp_payload, strlen(resp_payload));

      /* 鑻ユ秹鍙?WiFi 鍙樻洿锛屽悓姝ュ埌 mqtt->config 骞舵爣璁伴噸杩?*/
      if (wifi_change_needed) {
        strncpy(mqtt->config.wifi_ssid, mqtt->app->state.config.wifi_ssid,
                sizeof(mqtt->config.wifi_ssid) - 1);
        mqtt->config.wifi_ssid[sizeof(mqtt->config.wifi_ssid) - 1] = '\0';
        strncpy(mqtt->config.wifi_password,
                mqtt->app->state.config.wifi_password,
                sizeof(mqtt->config.wifi_password) - 1);
        mqtt->config.wifi_password[sizeof(mqtt->config.wifi_password) - 1] =
            '\0';
        aqua_mqtt_notify_wifi_changed(mqtt);
      }

      handled = true;
      /* publish 宸插彂璧凤紝break 閬垮厤缁х画澶勭悊鍚庣画鍛戒护 */
      break;
    }

    handled = true;
  }

  return handled;
}

/* 閰嶇疆椤?HTML锛堟瀬绠€鐗堟湰浠ヨ妭鐪佸唴瀛橈級 */
static const char *AP_CONFIG_HTML =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<title>Aquarium Setup</title></head><body>"
    "<h2>WiFi Config</h2>"
    "<form action=\"/config\" method=\"get\">"
    "SSID:<input name=\"ssid\"><br>"
    "Password:<input name=\"pwd\" type=\"password\"><br>"
    "<button type=\"submit\">Save</button></form></body></html>";

static const char *AP_SUCCESS_HTML =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<title>Success</title></head><body>"
    "<h2>Config Saved!</h2><p>Device will reconnect...</p></body></html>";

/**
 * @brief AP 閰嶇綉妯″紡涓嬪鐞?HTTP 璇锋眰锛堥潪闃诲锛?
 *
 * 瑙ｆ瀽 +IPD URC锛岃缃彂閫佸弬鏁板悗杞崲鍒?AP_SENDING 鐘舵€?
 * 瀹為檯鍙戦€佺敱鐘舵€佹満鐨?AP_SENDING/AP_SEND_DATA 澶勭悊
 *
 * @return true 濡傛灉瑙﹀彂浜嗗彂閫佹祦绋?
 */
bool aqua_mqtt_poll_ap_config(MqttClient *mqtt) {
  if (!mqtt || !mqtt->at || !mqtt->app)
    return false;
  if (mqtt->state != MQTT_STATE_AP_WAIT)
    return false;

  AtLine urc;
  while (aqua_at_pop_line(mqtt->at, &urc) == AT_OK) {
    /*
     * 鍏煎涓ょ +IPD 鏍煎紡锛?
     * - 绠€鍗曟牸寮?(CIPDINFO=0): +IPD,<link_id>,<len>:<data>
     * - 璇︾粏鏍煎紡 (CIPDINFO=1):
     * +IPD,<link_id>,<len>,<remote_ip>,<remote_port>:<data>
     */
    const char *ipd = strstr(urc.data, "+IPD,");
    if (!ipd)
      continue;

    /* 瑙ｆ瀽 link_id */
    ipd += 5;
    int link_id = 0;
    while (*ipd >= '0' && *ipd <= '9') {
      link_id = link_id * 10 + (*ipd - '0');
      ipd++;
    }
    if (*ipd == ',')
      ipd++;

    /* 璺宠繃 len */
    while (*ipd >= '0' && *ipd <= '9')
      ipd++;

    /* 璺宠繃鍙兘鐨?remote_ip 鍜?remote_port锛堣缁嗘牸寮忥級 */
    while (*ipd == ',' || *ipd == '.' || (*ipd >= '0' && *ipd <= '9'))
      ipd++;

    if (*ipd == ':')
      ipd++;

    /* 鐜板湪 ipd 鎸囧悜 HTTP 璇锋眰鏁版嵁 */
    char ssid[33] = {0};
    char pwd[65] = {0};
    int req_type = aqua_mqtt_parse_ap_request(ipd, ssid, pwd);

    if (req_type == 0)
      continue; /* 鏃犳晥璇锋眰锛屽拷鐣?*/

    /* 淇濆瓨鍙戦€佸弬鏁?*/
    mqtt->ap_link_id = link_id;
    mqtt->ap_req_type = req_type;

    char cmd[128];
    if (req_type == 1) {
      /* 璇锋眰棣栭〉 */
      mqtt->ap_send_html = AP_CONFIG_HTML;
      snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%zu", link_id,
               strlen(AP_CONFIG_HTML));
    } else if (req_type == 2) {
      /* 淇濆瓨閰嶇疆 */
      strncpy(mqtt->config.wifi_ssid, ssid, sizeof(mqtt->config.wifi_ssid) - 1);
      mqtt->config.wifi_ssid[sizeof(mqtt->config.wifi_ssid) - 1] = '\0';
      strncpy(mqtt->config.wifi_password, pwd,
              sizeof(mqtt->config.wifi_password) - 1);
      mqtt->config.wifi_password[sizeof(mqtt->config.wifi_password) - 1] =
          '\0';

      /* 鍚屾鍒?app 灞傞厤缃苟鏍囪涓鸿剰 */
      if (mqtt->app) {
        strncpy(mqtt->app->state.config.wifi_ssid, ssid,
                sizeof(mqtt->app->state.config.wifi_ssid) - 1);
        mqtt->app->state.config.wifi_ssid
            [sizeof(mqtt->app->state.config.wifi_ssid) - 1] = '\0';
        strncpy(mqtt->app->state.config.wifi_password, pwd,
                sizeof(mqtt->app->state.config.wifi_password) - 1);
        mqtt->app->state.config.wifi_password
            [sizeof(mqtt->app->state.config.wifi_password) - 1] = '\0';
        mqtt->app->state.config_dirty = true;
      }

      mqtt->ap_send_html = AP_SUCCESS_HTML;
      snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%zu", link_id,
               strlen(AP_SUCCESS_HTML));
    }

    /* 鍙戣捣 AT+CIPSEND锛堝搷搴旈『搴忔槸 OK -> >锛岄渶瑕佷娇鐢?begin_with_prompt锛?*/
    aqua_at_begin_with_prompt(mqtt->at, cmd, AT_TIMEOUT_SHORT);
    mqtt->state = MQTT_STATE_AP_SENDING;
    return true;
  }

  return false;
}

/* ============================================================================
 * SNTP 鏃堕棿瑙ｆ瀽
 * ============================================================================
 */

/**
 * 瑙ｆ瀽鏈堜唤缂╁啓涓烘暟瀛楋紙1-12锛?
 */
static int parse_month(const char *mon) {
  for (int i = 0; i < 12; i++) {
    if (strncmp(mon, MONTH_NAMES[i], 3) == 0) {
      return i + 1;
    }
  }
  return 0;
}

bool aqua_mqtt_parse_sntp_time(const char *sntp_line, char *out_ts) {
  if (!sntp_line || !out_ts)
    return false;

  /* 鏌ユ壘 +CIPSNTPTIME: 鍓嶇紑 */
  const char *p = strstr(sntp_line, "+CIPSNTPTIME:");
  if (!p)
    return false;
  p += 13; /* 璺宠繃 "+CIPSNTPTIME:" */

  /* 鏍煎紡: "Mon Oct 18 20:12:27 2021" */
  /* 璺宠繃鏄熸湡鍑狅紙3瀛楃+绌烘牸锛?*/
  while (*p && *p != ' ')
    p++;
  if (*p == ' ')
    p++;

  /* 瑙ｆ瀽鏈堜唤锛?瀛楃锛?*/
  char mon[4] = {0};
  if (strlen(p) < 3)
    return false;
  strncpy(mon, p, 3);
  int month = parse_month(mon);
  if (month == 0)
    return false;
  p += 3;
  if (*p == ' ')
    p++;

  /* 瑙ｆ瀽鏃ユ湡 */
  int day = 0;
  while (*p >= '0' && *p <= '9') {
    day = day * 10 + (*p - '0');
    p++;
  }
  if (day < 1 || day > 31)
    return false;
  if (*p == ' ')
    p++;

  /* 瑙ｆ瀽灏忔椂 */
  int hour = 0;
  while (*p >= '0' && *p <= '9') {
    hour = hour * 10 + (*p - '0');
    p++;
  }
  if (hour > 23)
    return false;

  /* 璺宠繃鍒嗛挓鍜岀 (":MM:SS ") */
  while (*p && *p != ' ')
    p++;
  if (*p == ' ')
    p++;

  /* 瑙ｆ瀽骞翠唤 */
  int year = 0;
  while (*p >= '0' && *p <= '9') {
    year = year * 10 + (*p - '0');
    p++;
  }
  if (year < 2020 || year > 2100)
    return false;

  /* 杈撳嚭 YYYYMMDDHH 鏍煎紡锛堝浐瀹?10 瀛楃 + '\0'锛?*/
  out_ts[0] = (char)('0' + ((year / 1000) % 10));
  out_ts[1] = (char)('0' + ((year / 100) % 10));
  out_ts[2] = (char)('0' + ((year / 10) % 10));
  out_ts[3] = (char)('0' + (year % 10));

  out_ts[4] = (char)('0' + ((month / 10) % 10));
  out_ts[5] = (char)('0' + (month % 10));

  out_ts[6] = (char)('0' + ((day / 10) % 10));
  out_ts[7] = (char)('0' + (day % 10));

  out_ts[8] = (char)('0' + ((hour / 10) % 10));
  out_ts[9] = (char)('0' + (hour % 10));

  out_ts[10] = '\0';
  return true;
}

/* ============================================================================
 * AP 閰嶇綉杈呭姪鍑芥暟
 * ============================================================================
 */

/**
 * @brief URL 瑙ｇ爜杈呭姪鍑芥暟
 */
static void url_decode(const char *src, char *dst, size_t dst_size) {
  size_t i = 0;
  while (*src && i < dst_size - 1) {
    if (*src == '%' && src[1] && src[2]) {
      /* 瑙ｆ瀽 %XX */
      char hex[3] = {src[1], src[2], '\0'};
      int val = 0;
      for (int j = 0; j < 2; j++) {
        val <<= 4;
        char c = hex[j];
        if (c >= '0' && c <= '9')
          val += c - '0';
        else if (c >= 'A' && c <= 'F')
          val += c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
          val += c - 'a' + 10;
      }
      dst[i++] = (char)val;
      src += 3;
    } else if (*src == '+') {
      dst[i++] = ' ';
      src++;
    } else {
      dst[i++] = *src++;
    }
  }
  dst[i] = '\0';
}

/**
 * @brief 浠?URL 鏌ヨ瀛楃涓蹭腑鎻愬彇鍙傛暟鍊?
 */
static bool extract_param(const char *query, const char *name, char *out,
                          size_t out_size) {
  size_t name_len = strlen(name);
  const char *p = query;

  while ((p = strstr(p, name)) != NULL) {
    /* 妫€鏌ユ槸鍚︽槸鍙傛暟寮€澶达紙鍓嶉潰鏄?? 鎴?&锛?*/
    if (p == query || *(p - 1) == '?' || *(p - 1) == '&') {
      if (p[name_len] == '=') {
        p += name_len + 1;
        /* 鏌ユ壘鍙傛暟缁撳熬 */
        const char *end = p;
        while (*end && *end != '&' && *end != ' ' && *end != '\r' &&
               *end != '\n') {
          end++;
        }
        size_t len = (size_t)(end - p);
        if (len >= out_size)
          len = out_size - 1;
        /* 鍏堝鍒跺埌涓存椂缂撳啿鍖哄啀 URL 瑙ｇ爜 */
        char temp[128];
        if (len >= sizeof(temp))
          len = sizeof(temp) - 1;
        memcpy(temp, p, len);
        temp[len] = '\0';
        url_decode(temp, out, out_size);
        return true;
      }
    }
    p++;
  }
  return false;
}

int aqua_mqtt_parse_ap_request(const char *http_req, char *out_ssid,
                               char *out_pwd) {
  if (!http_req)
    return 0;

  /* 鏌ユ壘 HTTP 璇锋眰琛?*/
  const char *get = strstr(http_req, "GET ");
  if (!get)
    return 0;

  get += 4; /* 璺宠繃 "GET " */

  /* GET / -> 杩斿洖閰嶇疆椤?*/
  if (*get == '/' && (get[1] == ' ' || get[1] == '\r' || get[1] == '\n')) {
    return 1;
  }

  /* GET /config?ssid=...&pwd=... -> 鎻愬彇閰嶇疆 */
  if (strncmp(get, "/config?", 8) == 0) {
    const char *query = get + 7; /* 鎸囧悜 '?' */

    if (out_ssid && extract_param(query, "ssid", out_ssid, 33)) {
      if (out_pwd && extract_param(query, "pwd", out_pwd, 65)) {
        return 2;
      }
    }
  }

  return 0;
}

bool aqua_mqtt_is_ap_mode(const MqttClient *mqtt) {
  if (!mqtt)
    return false;
  return (mqtt->state == MQTT_STATE_AP_START ||
          mqtt->state == MQTT_STATE_AP_CIPMUX ||
          mqtt->state == MQTT_STATE_AP_CIPDINFO ||
          mqtt->state == MQTT_STATE_AP_SERVER ||
          mqtt->state == MQTT_STATE_AP_WAIT ||
          mqtt->state == MQTT_STATE_AP_SENDING ||
          mqtt->state == MQTT_STATE_AP_SEND_DATA ||
          mqtt->state == MQTT_STATE_AP_CLOSE ||
          mqtt->state == MQTT_STATE_AP_STOP);
}

void aqua_mqtt_notify_wifi_changed(MqttClient *mqtt) {
  if (!mqtt)
    return;
  mqtt->wifi_changed = true;
  /* 濡傛灉鍦?ERROR 鐘舵€侊紝绔嬪嵆閲嶇疆閫€閬胯鏃跺櫒 */
  mqtt->error_time_ms = 0;
  mqtt->reconnect_delay_ms = RECONNECT_DELAY_INIT_MS;
}

int aqua_mqtt_get_net_status(const MqttClient *mqtt) {
  if (!mqtt)
    return 0;
  switch (mqtt->state) {
  case MQTT_STATE_ONLINE:
    return 2; /* 鍦ㄧ嚎 */
  case MQTT_STATE_ERROR:
    return 0; /* 绂荤嚎/閿欒 */
  case MQTT_STATE_AP_START:
  case MQTT_STATE_AP_CIPMUX:
  case MQTT_STATE_AP_CIPDINFO:
  case MQTT_STATE_AP_SERVER:
  case MQTT_STATE_AP_WAIT:
  case MQTT_STATE_AP_SENDING:
  case MQTT_STATE_AP_SEND_DATA:
  case MQTT_STATE_AP_CLOSE:
  case MQTT_STATE_AP_STOP:
    return 3; /* AP 閰嶇綉涓?*/
  default:
    return 1; /* 杩炴帴涓?*/
  }
}
