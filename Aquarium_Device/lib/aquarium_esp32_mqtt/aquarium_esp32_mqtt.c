/**
 * @file aquarium_esp32_mqtt.c
 * @brief ESP32 AT WiFi/MQTT 
 */

#include "aquarium_esp32_mqtt.h"
#include "aquarium_iotda_auth.h"
#include "aquarium_protocol.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * 
 * ============================================================================
 */

#define AT_TIMEOUT_SHORT 2000
#define AT_TIMEOUT_WIFI 35000
#define AT_TIMEOUT_MQTT 10000
#define AT_RESET_READY_TIMEOUT_MS 3000
#define PUB_DATA_TIMEOUT_MS 15000 /* +MQTTPUB:OK */
#define PUB_PROMPT_GUARD_MS 5 /* 让 ESP32 从 > 切到收数态 */
#define PUB_OK_COMPAT_GRACE_MS 1200 /* 普通 OK 后再给 +MQTTPUB 预留窗口 */
#define AT_TIMEOUT_SNTP 5000 /* SNTP */
#define SNTP_QUERY_MAX_RETRY 3
/* 每轮主循环最多处理 4 条 URC，避免串口持续灌入时饿死 OLED/LED 刷新。 */
#define MQTT_URC_PROCESS_BUDGET 4

/* AP */
#define AP_SSID_DEFAULT "Aquarium_Setup"
#define AP_PASSWORD_DEFAULT "12345678"
#define AP_SERVER_PORT 80

static bool mqtt_is_disconnect_urc(const char *line) {
  if (!line) {
    return false;
  }

  return strstr(line, "CLOSED") != NULL ||
         strstr(line, "DISCONNECT") != NULL ||
         strstr(line, "+MQTTDISCONNECTED") != NULL ||
         strstr(line, "+MQTTCONN:0,") != NULL ||
         strstr(line, "WIFI DISCONNECT") != NULL ||
         strstr(line, "+CWJAP:") != NULL;
}

static bool mqtt_is_ready_line(const char *line) {
  if (!line) {
    return false;
  }
  return strcmp(line, "ready") == 0;
}

/* */
static const char *MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static void at_escape_string(const char *src, char *dst, size_t dst_size) {
  size_t j = 0;
  if (!dst || dst_size == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }

  for (size_t i = 0; src[i] != '\0' && j + 1 < dst_size; ++i) {
    char ch = src[i];

    if (ch == '\r' || ch == '\n') {
      continue;
    }

    if ((ch == '\\' || ch == '"') && j + 2 < dst_size) {
      dst[j++] = '\\';
      dst[j++] = ch;
      continue;
    }

    dst[j++] = ch;
  }
  dst[j] = '\0';
}

static void aqua_mqtt_begin_cwjap(MqttClient *mqtt, char *cmd_buf,
                                  size_t cmd_buf_size) {
  char esc_ssid[33 * 2 + 1];
  char esc_password[65 * 2 + 1];

  at_escape_string(mqtt->config.wifi_ssid, esc_ssid, sizeof(esc_ssid));
  at_escape_string(mqtt->config.wifi_password, esc_password,
                   sizeof(esc_password));

  snprintf(cmd_buf, cmd_buf_size, "AT+CWJAP=\"%s\",\"%s\"", esc_ssid,
           esc_password);
  aqua_at_begin(mqtt->at, cmd_buf, AT_TIMEOUT_WIFI);
}

static void aqua_mqtt_begin_usercfg_base(MqttClient *mqtt, char *cmd_buf,
                                         size_t cmd_buf_size) {
  (void)mqtt;
  snprintf(cmd_buf, cmd_buf_size, "AT+MQTTUSERCFG=0,1,\"a\",\"a\",\"a\",0,0,\"\"");
}

static void aqua_mqtt_begin_client_id(MqttClient *mqtt, char *cmd_buf,
                                      size_t cmd_buf_size) {
  char client_id[128];
  char esc_client_id[128 * 2 + 1];

  aqua_iotda_build_client_id(mqtt->config.device_id, IOTDA_SIGN_TYPE_CHECK,
                             mqtt->timestamp, client_id, sizeof(client_id));
  at_escape_string(client_id, esc_client_id, sizeof(esc_client_id));
  snprintf(cmd_buf, cmd_buf_size, "AT+MQTTCLIENTID=0,\"%s\"", esc_client_id);
}

static void aqua_mqtt_begin_username(MqttClient *mqtt, char *cmd_buf,
                                     size_t cmd_buf_size) {
  char esc_username[65 * 2 + 1];

  at_escape_string(mqtt->config.device_id, esc_username, sizeof(esc_username));
  snprintf(cmd_buf, cmd_buf_size, "AT+MQTTUSERNAME=0,\"%s\"", esc_username);
}

static void aqua_mqtt_begin_password(MqttClient *mqtt, char *cmd_buf,
                                     size_t cmd_buf_size) {
  char password[65];
  char esc_password[65 * 2 + 1];

  aqua_iotda_build_password(mqtt->config.device_secret, mqtt->timestamp,
                            password);
  at_escape_string(password, esc_password, sizeof(esc_password));
  snprintf(cmd_buf, cmd_buf_size, "AT+MQTTPASSWORD=0,\"%s\"", esc_password);
}

static bool is_placeholder_wifi_ssid(const char *ssid) {
  if (!ssid || ssid[0] == '\0') {
    return true;
  }
  return (strcmp(ssid, "YourWiFiSSID") == 0 || strcmp(ssid, "your_wifi_ssid") == 0 ||
          strcmp(ssid, "CHANGE_ME") == 0);
}

static bool is_placeholder_wifi_password(const char *password) {
  if (!password || password[0] == '\0') {
    return true;
  }
  return (strcmp(password, "YourWiFiPassword") == 0 ||
          strcmp(password, "your_wifi_password") == 0 ||
          strcmp(password, "CHANGE_ME") == 0);
}

static bool aqua_mqtt_should_enter_ap_bootstrap(const MqttClient *mqtt) {
  if (!mqtt) {
    return true;
  }
  return is_placeholder_wifi_ssid(mqtt->config.wifi_ssid) ||
         is_placeholder_wifi_password(mqtt->config.wifi_password);
}

/* Preserve non-publish URCs observed during PUB_DATA so command URCs are not
 * lost while waiting for +MQTTPUB completion. */
static void aqua_mqtt_requeue_urcs(AtClient *at, const AtLine *lines,
                                   size_t count) {
  if (!at || !lines || count == 0) {
    return;
  }

  for (size_t i = 0; i < count; ++i) {
    const AtLine *src = &lines[i];
    if (!src->valid) {
      continue;
    }
    if (at->urc_count >= AT_URC_QUEUE_SIZE) {
      break;
    }

    AtLine *dst = &at->urc_queue[at->urc_head];
    size_t copy_len = src->len;
    if (copy_len > AT_LINE_MAX_LEN) {
      copy_len = AT_LINE_MAX_LEN;
    }

    memcpy(dst->data, src->data, copy_len);
    dst->data[copy_len] = '\0';
    dst->len = copy_len;
    dst->valid = true;

    at->urc_head = (at->urc_head + 1) % AT_URC_QUEUE_SIZE;
    at->urc_count++;
  }
}

/*
 * 仅遍历“当前拍到”的 URC 数量，避免把刚回灌的非发布 URC 再次弹出，
 * 同时彻底去掉 PUB_DATA 路径上的大块栈数组。
 */
static void aqua_mqtt_process_pub_data_urcs(MqttClient *mqtt) {
  if (!mqtt || !mqtt->at || !aqua_at_has_urc(mqtt->at)) {
    return;
  }

  size_t pending = mqtt->at->urc_count;
  while (pending-- > 0) {
    AtLine urc;
    if (aqua_at_pop_line(mqtt->at, &urc) != AT_OK) {
      break;
    }

    if (strstr(urc.data, "+MQTTPUB:OK") != NULL) {
      aqua_at_reset(mqtt->at);
      mqtt->state = MQTT_STATE_ONLINE;
      break;
    }

    if (strstr(urc.data, "+MQTTPUB:FAIL") != NULL) {
      aqua_at_reset(mqtt->at);
      mqtt->state = MQTT_STATE_ERROR;
      break;
    }

    aqua_mqtt_requeue_urcs(mqtt->at, &urc, 1);
  }
}

static void aqua_mqtt_write_payload_chunked(MqttClient *mqtt) {
  if (!mqtt || !mqtt->at || !mqtt->at->write_func) {
    return;
  }

  size_t offset = 0;
  while (offset < mqtt->pub_payload_len) {
    size_t chunk_len = mqtt->pub_payload_len - offset;
    if (chunk_len > 32) {
      chunk_len = 32;
    }

    /* 分块发送，给 UART1 现场链路留出自然间隙，降低长帧突发丢字节概率。 */
    mqtt->at->write_func((const uint8_t *)mqtt->pub_payload + offset, chunk_len);
    offset += chunk_len;
  }
}

static void aqua_mqtt_record_publish_cmd(MqttClient *mqtt, const char *cmd) {
  if (!mqtt) {
    return;
  }

  mqtt->diag_last_pub_cmd[0] = '\0';
  mqtt->diag_last_pub_cmd_len = 0;
  if (!cmd) {
    return;
  }

  strncpy(mqtt->diag_last_pub_cmd, cmd, sizeof(mqtt->diag_last_pub_cmd) - 1);
  mqtt->diag_last_pub_cmd[sizeof(mqtt->diag_last_pub_cmd) - 1] = '\0';
  mqtt->diag_last_pub_cmd_len = strlen(mqtt->diag_last_pub_cmd);
}

static void aqua_format_size_dec(size_t value, char *buffer,
                                 size_t buffer_size) {
  if (!buffer || buffer_size == 0) {
    return;
  }

  snprintf(buffer, buffer_size, "%lu", (unsigned long)value);
}

/* ============================================================================
 * 
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
 * 
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

 /* topic null */
  strncpy(mqtt->pub_topic, topic, MQTT_TOPIC_MAX_LEN - 1);
  mqtt->pub_topic[MQTT_TOPIC_MAX_LEN - 1] = '\0';

  memcpy(mqtt->pub_payload, payload, len);
  mqtt->pub_payload[len] = '\0';
  mqtt->pub_payload_len = len;
  mqtt->pub_use_raw = true;
  mqtt->pub_prompt_seen = false;
  mqtt->pub_prompt_ms = 0;
  mqtt->pub_final_ok_seen = false;
  mqtt->pub_final_ok_ms = 0;

 /* AT+MQTTPUBRAW */
 /* OK -> > begin_with_prompt */
  char len_str[24];
  char cmd[MQTT_TOPIC_MAX_LEN + 64];
  aqua_format_size_dec(mqtt->pub_payload_len, len_str, sizeof(len_str));
  snprintf(cmd, sizeof(cmd), "AT+MQTTPUBRAW=0,\"%s\",%s,1,0", mqtt->pub_topic,
           len_str);
  aqua_mqtt_record_publish_cmd(mqtt, cmd);
  aqua_at_begin_with_prompt(mqtt->at, cmd, AT_TIMEOUT_MQTT);
  mqtt->pub_start_ms = mqtt->at->now_ms_func();
  mqtt->state = MQTT_STATE_PUBLISHING;
  return true;
}

/* After raw payload is sent, continue waiting for publish completion markers. */
static void aqua_mqtt_arm_publish_result_wait(MqttClient *mqtt) {
  if (!mqtt || !mqtt->at) {
    return;
  }
  mqtt->at->state = AT_STATE_WAITING;
  mqtt->at->expect_prompt = false;
  mqtt->at->got_ok = false;
  mqtt->at->cmd_start_ms = mqtt->at->now_ms_func();
  mqtt->at->cmd_timeout_ms = PUB_DATA_TIMEOUT_MS;
}

/* ============================================================================
 * 
 * ============================================================================
 */

MqttConnState aqua_mqtt_step(MqttClient *mqtt) {
  if (!mqtt || !mqtt->at)
    return MQTT_STATE_ERROR;

  AtState at_state = aqua_at_step(mqtt->at);
  char cmd[256];

 /* AT */
  if (at_state == AT_STATE_WAITING && mqtt->state != MQTT_STATE_PUB_DATA) {
    return mqtt->state;
  }

  switch (mqtt->state) {
  case MQTT_STATE_IDLE:
    break;

 case MQTT_STATE_AT_TEST:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      aqua_at_begin(mqtt->at, "AT+RST", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AT_RESET;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_AT_RESET:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      mqtt->reset_wait_start_ms = mqtt->at->now_ms_func();
      mqtt->state = MQTT_STATE_AT_RESET_WAIT;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_AT_RESET_WAIT: {
    bool ready_seen = false;
    if (aqua_at_has_urc(mqtt->at)) {
      AtLine urc;
      size_t pending = mqtt->at->urc_count;
      while (pending-- > 0 && aqua_at_pop_line(mqtt->at, &urc) == AT_OK) {
        if (mqtt_is_ready_line(urc.data)) {
          ready_seen = true;
          break;
        }
      }
    }

    uint32_t now = mqtt->at->now_ms_func();
    if (ready_seen || (now - mqtt->reset_wait_start_ms >= AT_RESET_READY_TIMEOUT_MS)) {
      aqua_at_begin(mqtt->at, "ATE0", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_ATE0;
    }
    break;
  }

  case MQTT_STATE_ATE0:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      aqua_at_begin(mqtt->at, "AT+CWMODE=1", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_CWMODE;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_CWMODE:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      if (aqua_mqtt_should_enter_ap_bootstrap(mqtt)) {
        /* Empty/placeholder Wi-Fi defaults should not loop CWJAP failures. */
        mqtt->cwjap_fail_count = 0;
        aqua_at_begin(mqtt->at, "AT+CWMODE=3", AT_TIMEOUT_SHORT);
        mqtt->state = MQTT_STATE_AP_START;
      } else {
        aqua_mqtt_begin_cwjap(mqtt, cmd, sizeof(cmd));
        mqtt->state = MQTT_STATE_CWJAP;
      }
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_CWJAP:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
 mqtt->cwjap_fail_count = 0; /* */
 /* WiFi SNTP */
 /* IoTDA MQTT UTC(YYYYMMDDHH) */
      aqua_at_begin(mqtt->at,
                    "AT+CIPSNTPCFG=1,0,\"ntp.aliyun.com\","
                    "\"ntp.ntsc.ac.cn\",\"time.cloudflare.com\"",
                    AT_TIMEOUT_SNTP);
      mqtt->retry_count = 0;
      mqtt->state = MQTT_STATE_SNTPCFG;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->cwjap_fail_count++;
      aqua_at_reset(mqtt->at);
      if (mqtt->cwjap_fail_count >= CWJAP_MAX_FAILS) {
 /* AP */
 /* AP+STA */
        aqua_at_begin(mqtt->at, "AT+CWMODE=3", AT_TIMEOUT_SHORT);
        mqtt->state = MQTT_STATE_AP_START;
      } else {
 /* */
        aqua_mqtt_begin_cwjap(mqtt, cmd, sizeof(cmd));
      }
    }
    break;

  case MQTT_STATE_SNTPCFG:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
 /* SNTP */
      aqua_at_begin(mqtt->at, "AT+CIPSNTPTIME?", AT_TIMEOUT_SNTP);
      mqtt->state = MQTT_STATE_SNTPTIME;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_SNTPTIME:
    if (at_state == AT_STATE_DONE_OK) {
 /* */
      const AtLine *resp = aqua_at_get_response(mqtt->at);
      char ts[12];
      bool ts_ok = (resp && aqua_mqtt_parse_sntp_time(resp->data, ts) &&
                    strlen(ts) == 10);

      if (!ts_ok && aqua_at_has_urc(mqtt->at)) {
        AtLine urc;
        while (aqua_at_pop_line(mqtt->at, &urc) == AT_OK) {
          if (aqua_mqtt_parse_sntp_time(urc.data, ts) && strlen(ts) == 10) {
            ts_ok = true;
            break;
          }
        }
      }

      if (ts_ok) {
        aqua_mqtt_set_timestamp(mqtt, ts);
        mqtt->retry_count = 0;
      }
      aqua_at_reset(mqtt->at);

      if (!ts_ok) {
        if (mqtt->retry_count < SNTP_QUERY_MAX_RETRY) {
          mqtt->retry_count++;
          aqua_at_begin(mqtt->at, "AT+CIPSNTPTIME?", AT_TIMEOUT_SNTP);
          mqtt->state = MQTT_STATE_SNTPTIME;
        } else {
          mqtt->state = MQTT_STATE_ERROR;
        }
        break;
      }

      if (strlen(mqtt->timestamp) != 10) {
        mqtt->state = MQTT_STATE_ERROR;
        break;
      }
 /* */
      aqua_mqtt_begin_usercfg_base(mqtt, cmd, sizeof(cmd));
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_MQTTUSERCFG;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_MQTTUSERCFG:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      aqua_mqtt_begin_client_id(mqtt, cmd, sizeof(cmd));
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_MQTTCLIENTID;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_MQTTCLIENTID:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      aqua_mqtt_begin_username(mqtt, cmd, sizeof(cmd));
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_MQTTUSERNAME;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_MQTTUSERNAME:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      aqua_mqtt_begin_password(mqtt, cmd, sizeof(cmd));
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_MQTTPASSWORD;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_MQTTPASSWORD:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      snprintf(cmd, sizeof(cmd), "AT+MQTTCONN=0,\"%s\",%u,1",
               mqtt->config.broker_host, mqtt->config.broker_port);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_MQTT);
      mqtt->state = MQTT_STATE_MQTTCONN;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
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
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_MQTTSUB:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      snprintf(cmd, sizeof(cmd),
               "AT+MQTTSUB=0,\"$oc/devices/%s/sys/messages/down\",1",
               mqtt->config.device_id);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_MQTT);
      mqtt->state = MQTT_STATE_MQTTMSGSUB;
    } else if (at_state == AT_STATE_DONE_TIMEOUT) {
      /*
       * 现场和云端跟踪表明，部分 ESP-AT 固件在订阅已生效时仍可能丢失尾部 OK。
       * 这里保留兼容路径，继续尝试补订阅 messages/down。
       */
      aqua_at_reset(mqtt->at);
      snprintf(cmd, sizeof(cmd),
               "AT+MQTTSUB=0,\"$oc/devices/%s/sys/messages/down\",1",
               mqtt->config.device_id);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_MQTT);
      mqtt->state = MQTT_STATE_MQTTMSGSUB;
    } else if (at_state == AT_STATE_DONE_ERROR) {
      mqtt->state = MQTT_STATE_ERROR;
      mqtt->error_time_ms = mqtt->at->now_ms_func();
    }
    break;

  case MQTT_STATE_MQTTMSGSUB:
    if (at_state == AT_STATE_DONE_OK || at_state == AT_STATE_DONE_TIMEOUT) {
      aqua_at_reset(mqtt->at);
      mqtt->state = MQTT_STATE_ONLINE;
    } else if (at_state == AT_STATE_DONE_ERROR) {
      mqtt->state = MQTT_STATE_ERROR;
      mqtt->error_time_ms = mqtt->at->now_ms_func();
    }
    break;

  case MQTT_STATE_PUBLISHING:
    /*
 * AT+MQTTPUBRAW 
 * 1. > 
 * 2. payload 
 * 3. +MQTTPUB:OK +MQTTPUB:FAIL
     */
    if (at_state == AT_STATE_GOT_PROMPT) {
      uint32_t now = mqtt->at->now_ms_func();
      if (!mqtt->pub_prompt_seen) {
        mqtt->pub_prompt_seen = true;
        mqtt->pub_prompt_ms = now;
        break;
      }
      if (now - mqtt->pub_prompt_ms < PUB_PROMPT_GUARD_MS) {
        break;
      }

      /* > 后给出极短保护窗口，再发送 payload，避免 UART1 现场链路首字节丢失。 */
      aqua_mqtt_write_payload_chunked(mqtt);
      mqtt->state = MQTT_STATE_PUB_DATA;
      /*
       * Some ESP-AT builds report publish completion via plain final OK
       * (without +MQTTPUB URC). Re-arm AT wait state so DONE_OK is observable.
       */
      aqua_mqtt_arm_publish_result_wait(mqtt);
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_PUB_DATA: {
    /*
 * URC +MQTTPUB:OK +MQTTPUB:FAIL
     */
    aqua_mqtt_process_pub_data_urcs(mqtt);

    if (mqtt->state == MQTT_STATE_PUB_DATA) {
      uint32_t now = mqtt->at->now_ms_func();

      /*
       * 官方文档定义的成功标志是 +MQTTPUB:OK。
       * 这里把 payload 后的普通 OK 视为“待确认”而非立即成功，
       * 先给 +MQTTPUB:OK / FAIL 留出一小段窗口，避免把假成功吞掉。
       */
      if (at_state == AT_STATE_DONE_OK) {
        aqua_at_reset(mqtt->at);
        mqtt->pub_final_ok_seen = true;
        mqtt->pub_final_ok_ms = now;
      }
      if (mqtt->state == MQTT_STATE_PUB_DATA &&
          at_state == AT_STATE_DONE_ERROR) {
        aqua_at_reset(mqtt->at);
        mqtt->state = MQTT_STATE_ERROR;
        break;
      }
      if (mqtt->state == MQTT_STATE_PUB_DATA &&
          at_state == AT_STATE_DONE_TIMEOUT) {
        aqua_at_reset(mqtt->at);
        mqtt->state = MQTT_STATE_ERROR;
        mqtt->error_time_ms = now;
        break;
      }

      if (mqtt->pub_final_ok_seen &&
          (now - mqtt->pub_final_ok_ms >= PUB_OK_COMPAT_GRACE_MS)) {
        mqtt->state = MQTT_STATE_ONLINE;
        break;
      }

      if (now - mqtt->pub_start_ms >= PUB_DATA_TIMEOUT_MS) {
        aqua_at_reset(mqtt->at);
        mqtt->state = MQTT_STATE_ERROR;
        mqtt->error_time_ms = now;
      }
    }
    break;
  }

 /* ====================== AP ====================== */
  case MQTT_STATE_AP_START:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
 /* SoftAP */
      const char *ap_ssid =
          mqtt->ap_ssid[0] != '\0' ? mqtt->ap_ssid : AP_SSID_DEFAULT;
      const char *ap_pwd = mqtt->ap_password[0] != '\0' ? mqtt->ap_password
                                                        : AP_PASSWORD_DEFAULT;
      snprintf(cmd, sizeof(cmd), "AT+CWSAP=\"%s\",\"%s\",1,3", ap_ssid,
               ap_pwd);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AP_CIPMUX;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_AP_CIPMUX:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      /* Enable multi-connection mode required by TCP server. */
      aqua_at_begin(mqtt->at, "AT+CIPMUX=1", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AP_CIPDINFO;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_AP_CIPDINFO:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      /* Force active receive mode so +IPD includes HTTP payload. */
      aqua_at_begin(mqtt->at, "AT+CIPRECVMODE=0", AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AP_SERVER;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_AP_SERVER:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
      /* Start TCP config server. */
      snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%d", AP_SERVER_PORT);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AP_WAIT;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
      mqtt->state = MQTT_STATE_ERROR;
    }
    break;

  case MQTT_STATE_AP_WAIT:
    /*
     * AP_WAIT has two phases:
     * 1) immediately after issuing AT+CIPSERVER=1,80: consume DONE_OK / DONE_ERROR
     * 2) steady-state idle: wait URC +IPD and let poll_ap_config() handle requests
     */
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
    } else if (at_state == AT_STATE_DONE_ERROR) {
      aqua_at_reset(mqtt->at);
      mqtt->state = MQTT_STATE_ERROR;
    } else if (at_state == AT_STATE_DONE_TIMEOUT) {
      /*
       * Some ESP-AT builds can accept CIPSERVER but miss the final OK line
       * under heavy captive-portal traffic. Do not drop AP mode on this timeout;
       * reset AT state and continue serving incoming +IPD requests.
       */
      aqua_at_reset(mqtt->at);
    }
    break;

  case MQTT_STATE_AP_SENDING:
 /* AT+CIPSEND > */
    if (at_state == AT_STATE_GOT_PROMPT) {
 /* HTML */
      if (mqtt->ap_send_html) {
        mqtt->at->write_func((const uint8_t *)mqtt->ap_send_html,
                             strlen(mqtt->ap_send_html));
      }
      aqua_at_reset(mqtt->at);
      mqtt->state = MQTT_STATE_AP_SEND_DATA;
    } else if (at_state == AT_STATE_DONE_ERROR ||
               at_state == AT_STATE_DONE_TIMEOUT) {
 /* CIPSEND */
      aqua_at_reset(mqtt->at);
      snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d", mqtt->ap_link_id);
      aqua_at_begin(mqtt->at, cmd, AT_TIMEOUT_SHORT);
      mqtt->state = MQTT_STATE_AP_CLOSE;
    }
    break;

  case MQTT_STATE_AP_SEND_DATA: {
 /* SEND OK SEND FAIL URC */
    if (aqua_at_has_urc(mqtt->at)) {
      AtLine urc;
      while (aqua_at_pop_line(mqtt->at, &urc) == AT_OK) {
        if (strstr(urc.data, "SEND OK") != NULL ||
            strstr(urc.data, "SEND FAIL") != NULL) {
 /* */
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
 /* CIPCLOSE */
    if (at_state == AT_STATE_DONE_OK || at_state == AT_STATE_DONE_ERROR) {
      aqua_at_reset(mqtt->at);
      if (mqtt->ap_req_type == 2) {
 /* WiFi */
        aqua_at_begin(mqtt->at, "AT+CIPSERVER=0", AT_TIMEOUT_SHORT);
        mqtt->state = MQTT_STATE_AP_STOP;
      } else {
 /* */
        mqtt->state = MQTT_STATE_AP_WAIT;
      }
    }
    break;

  case MQTT_STATE_AP_STOP:
    if (at_state == AT_STATE_DONE_OK) {
      aqua_at_reset(mqtt->at);
 /* STA WiFi */
      aqua_at_begin(mqtt->at, "AT+CWMODE=1", AT_TIMEOUT_SHORT);
      mqtt->cwjap_fail_count = 0;
      mqtt->state = MQTT_STATE_CWMODE;
    } else {
 /* */
    }
    break;

  case MQTT_STATE_ONLINE:
    if (aqua_at_has_urc(mqtt->at)) {
      AtLine urc;
      while (aqua_at_pop_line(mqtt->at, &urc) == AT_OK) {
        if (mqtt_is_disconnect_urc(urc.data)) {
          mqtt->state = MQTT_STATE_ERROR;
          mqtt->error_time_ms = mqtt->at->now_ms_func();
          break;
        }
      }

      if (mqtt->state == MQTT_STATE_ERROR) {
        break;
      }
    }
 /* WiFi */
    if (mqtt->wifi_changed) {
      mqtt->wifi_changed = false;
      mqtt->cwjap_fail_count = 0;
      mqtt->reconnect_delay_ms = RECONNECT_DELAY_INIT_MS;
      aqua_at_reset(mqtt->at);
 /* MQTT */
      aqua_at_begin(mqtt->at, "AT+MQTTCLEAN=0", AT_TIMEOUT_SHORT);
 mqtt->state = MQTT_STATE_AT_TEST; /* */
    }
    break;

  case MQTT_STATE_ERROR: {
 /* */
    uint32_t now = mqtt->at->now_ms_func();

 /* ERROR */
    if (mqtt->error_time_ms == 0) {
      mqtt->error_time_ms = now;
      if (mqtt->reconnect_delay_ms == 0) {
        mqtt->reconnect_delay_ms = RECONNECT_DELAY_INIT_MS;
      }
    }

 /* */
    if (now - mqtt->error_time_ms >= mqtt->reconnect_delay_ms) {
 /* */
      mqtt->reconnect_delay_ms *= RECONNECT_DELAY_FACTOR;
      if (mqtt->reconnect_delay_ms > RECONNECT_DELAY_MAX_MS) {
        mqtt->reconnect_delay_ms = RECONNECT_DELAY_MAX_MS;
      }
 /* */
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
 * 
 * ============================================================================
 */

/**
 * +MQTTSUBRECV:<LinkID>,"<topic>",<data_len>,<data>
 * true 
 */
static bool parse_mqttsubrecv(const char *line, char *out_topic,
                              size_t topic_size, char *out_payload,
                              size_t payload_size) {
 /* +MQTTSUBRECV: */
  const char *p = strstr(line, "+MQTTSUBRECV:");
  if (!p)
    return false;
 p += 13; /* +MQTTSUBRECV: */

 /* LinkID */
  while (*p && *p != ',')
    p++;
  if (*p == ',')
    p++;

 /* topic */
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

 /* data_len */
  int data_len = 0;
  while (*p >= '0' && *p <= '9') {
    data_len = data_len * 10 + (*p - '0');
    p++;
  }
  if (*p == ',')
    p++;

 /* payload：
  * 某些场景下 URC 可能被截断（例如 AT 行缓冲不足），这里不直接丢弃，
  * 而是尽量保留可用部分，后续交给命令解析层返回错误响应，避免平台超时。 */
  size_t available_len = strlen(p);
  size_t copy_len = (size_t)data_len;
  if (copy_len > available_len) {
    copy_len = available_len;
  }
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
  uint8_t processed = 0;

  while (processed < MQTT_URC_PROCESS_BUDGET &&
         aqua_at_pop_line(mqtt->at, &urc) == AT_OK) {
    processed++;
    if (mqtt_is_disconnect_urc(urc.data)) {
      mqtt->state = MQTT_STATE_ERROR;
      mqtt->error_time_ms = mqtt->at->now_ms_func();
      handled = true;
      break;
    }
 /* +MQTTSUBRECV */
    if (strstr(urc.data, "+MQTTSUBRECV:") == NULL)
      continue;

    char topic[MQTT_TOPIC_MAX_LEN];
    char payload[MQTT_PAYLOAD_MAX_LEN];

    if (!parse_mqttsubrecv(urc.data, topic, sizeof(topic), payload,
                           sizeof(payload))) {
      continue;
    }

    bool is_command_topic = strstr(topic, "/sys/commands/") != NULL;
    bool is_message_topic = strstr(topic, "/sys/messages/down") != NULL;
    if (!is_command_topic && !is_message_topic) {
      continue;
    }

 /* WiFi */
    ParsedCommand cmd;
    AquaError parse_err = aqua_parse_command_json(payload, strlen(payload), &cmd);
    bool wifi_change_needed = false;
    if (parse_err == AQUA_OK) {
      if (cmd.type == COMMAND_TYPE_SET_CONFIG &&
          cmd.params.config.has_wifi_ssid &&
          cmd.params.config.has_wifi_password &&
          cmd.params.config.wifi_ssid[0] != '\0') {
        if (strcmp(cmd.params.config.wifi_ssid, mqtt->config.wifi_ssid) != 0 ||
            strcmp(cmd.params.config.wifi_password, mqtt->config.wifi_password) !=
                0) {
          wifi_change_needed = true;
        }
      }
    }

    if (is_message_topic) {
      if (parse_err == AQUA_OK &&
          aqua_app_apply_parsed_command(mqtt->app, &cmd) == AQUA_OK) {
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
      }
      handled = true;
      break;
    }

 /* app */
    char resp_topic[MQTT_TOPIC_MAX_LEN];
    char resp_payload[MQTT_PAYLOAD_MAX_LEN];
    bool has_response = false;

    AquaError err = aqua_app_on_mqtt_command(
        mqtt->app, topic, payload, strlen(payload), &has_response, resp_topic,
        sizeof(resp_topic), resp_payload, sizeof(resp_payload));

 /* */
    if (err == AQUA_OK && has_response) {
      bool publish_started =
          aqua_mqtt_publish(mqtt, resp_topic, resp_payload, strlen(resp_payload));
      if (!publish_started) {
        /*
         * 同步命令必须回包。若回包发布未启动（状态异常/缓冲问题），不要静默吞掉，
         * 直接进入 ERROR 触发重连，避免平台持续超时且现场无感知。
         */
        mqtt->state = MQTT_STATE_ERROR;
        mqtt->error_time_ms = mqtt->at->now_ms_func();
      }

 /* WiFi mqtt->config */
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
 /* publish break */
      break;
    }

    handled = true;
  }

  return handled;
}

/* HTML */
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
 * @brief AP HTTP 
 *
 * +IPD URC AP_SENDING 
 * AP_SENDING/AP_SEND_DATA 
 *
 * @return true 
 */
bool aqua_mqtt_poll_ap_config(MqttClient *mqtt) {
  if (!mqtt || !mqtt->at || !mqtt->app)
    return false;
  if (mqtt->state != MQTT_STATE_AP_WAIT)
    return false;

  AtLine urc;
  uint8_t processed = 0;
  while (processed < MQTT_URC_PROCESS_BUDGET &&
         aqua_at_pop_line(mqtt->at, &urc) == AT_OK) {
    processed++;
    /*
 * +IPD 
 * - (CIPDINFO=0): +IPD,<link_id>,<len>:<data>
 * - (CIPDINFO=1):
     * +IPD,<link_id>,<len>,<remote_ip>,<remote_port>:<data>
     */
    const char *ipd = strstr(urc.data, "+IPD,");
    if (!ipd)
      continue;

 /* link_id */
    ipd += 5;
    int link_id = 0;
    while (*ipd >= '0' && *ipd <= '9') {
      link_id = link_id * 10 + (*ipd - '0');
      ipd++;
    }
    if (*ipd == ',')
      ipd++;

 /* len */
    while (*ipd >= '0' && *ipd <= '9')
      ipd++;

 /* remote_ip remote_port */
    while (*ipd == ',' || *ipd == '.' || (*ipd >= '0' && *ipd <= '9'))
      ipd++;

    if (*ipd == ':')
      ipd++;

 /* ipd HTTP */
    char ssid[33] = {0};
    char pwd[65] = {0};
    int req_type = aqua_mqtt_parse_ap_request(ipd, ssid, pwd);

    if (req_type == 0)
 continue; /* */

 /* */
    mqtt->ap_link_id = link_id;
    mqtt->ap_req_type = req_type;

    char cmd[128];
    char send_len_str[24];
    if (req_type == 1) {
 /* */
      mqtt->ap_send_html = AP_CONFIG_HTML;
      aqua_format_size_dec(strlen(AP_CONFIG_HTML), send_len_str,
                           sizeof(send_len_str));
      snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%s", link_id, send_len_str);
    } else if (req_type == 2) {
 /* */
      strncpy(mqtt->config.wifi_ssid, ssid, sizeof(mqtt->config.wifi_ssid) - 1);
      mqtt->config.wifi_ssid[sizeof(mqtt->config.wifi_ssid) - 1] = '\0';
      strncpy(mqtt->config.wifi_password, pwd,
              sizeof(mqtt->config.wifi_password) - 1);
      mqtt->config.wifi_password[sizeof(mqtt->config.wifi_password) - 1] =
          '\0';

/* app */
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
      aqua_format_size_dec(strlen(AP_SUCCESS_HTML), send_len_str,
                           sizeof(send_len_str));
      snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%s", link_id, send_len_str);
    }

 /* AT+CIPSEND OK -> > begin_with_prompt */
    aqua_at_begin_with_prompt(mqtt->at, cmd, AT_TIMEOUT_SHORT);
    mqtt->state = MQTT_STATE_AP_SENDING;
    return true;
  }

  return false;
}

/* ============================================================================
 * SNTP 
 * ============================================================================
 */

/**
 * 1-12 
 */
static int parse_month(const char *mon) {
  for (int i = 0; i < 12; i++) {
    if (strncmp(mon, MONTH_NAMES[i], 3) == 0) {
      return i + 1;
    }
  }
  return 0;
}

static const char *skip_spaces(const char *p) {
  while (p && *p == ' ') {
    p++;
  }
  return p;
}

bool aqua_mqtt_parse_sntp_time(const char *sntp_line, char *out_ts) {
  if (!sntp_line || !out_ts)
    return false;

 /* +CIPSNTPTIME: */
  const char *p = strstr(sntp_line, "+CIPSNTPTIME:");
  if (!p)
    return false;
 p += 13; /* "+CIPSNTPTIME:" */

 /* : "Mon Oct 18 20:12:27 2021" */
 /* 3 + */
  while (*p && *p != ' ')
    p++;
  if (*p == ' ')
    p++;

 /* */
  char mon[4] = {0};
  if (strlen(p) < 3)
    return false;
  strncpy(mon, p, 3);
  int month = parse_month(mon);
  if (month == 0)
    return false;
  p += 3;
  p = skip_spaces(p);

 /* */
  int day = 0;
  while (*p >= '0' && *p <= '9') {
    day = day * 10 + (*p - '0');
    p++;
  }
  if (day < 1 || day > 31)
    return false;
  p = skip_spaces(p);

 /* */
  int hour = 0;
  while (*p >= '0' && *p <= '9') {
    hour = hour * 10 + (*p - '0');
    p++;
  }
  if (hour > 23)
    return false;

 /* (":MM:SS ") */
  while (*p && *p != ' ')
    p++;
  p = skip_spaces(p);

 /* */
  int year = 0;
  while (*p >= '0' && *p <= '9') {
    year = year * 10 + (*p - '0');
    p++;
  }
  if (year < 2020 || year > 2100)
    return false;

 /* YYYYMMDDHH 10 + '\0' */
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
 * AP 
 * ============================================================================
 */

/**
 * @brief URL 
 */
static void url_decode(const char *src, char *dst, size_t dst_size) {
  size_t i = 0;
  while (*src && i < dst_size - 1) {
    if (*src == '%' && src[1] && src[2]) {
 /* %XX */
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
 * @brief URL 
 */
static bool extract_param(const char *query, const char *name, char *out,
                          size_t out_size) {
  size_t name_len = strlen(name);
  const char *p = query;

  while ((p = strstr(p, name)) != NULL) {
 /* & */
    if (p == query || *(p - 1) == '?' || *(p - 1) == '&') {
      if (p[name_len] == '=') {
        p += name_len + 1;
 /* */
        const char *end = p;
        while (*end && *end != '&' && *end != ' ' && *end != '\r' &&
               *end != '\n') {
          end++;
        }
        size_t len = (size_t)(end - p);
        if (len >= out_size)
          len = out_size - 1;
 /* URL */
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

 /* HTTP */
  const char *get = strstr(http_req, "GET ");
  if (!get)
    return 0;

  get += 4; /* skip "GET " */

  /* parse request-target: origin-form or absolute-form */
  const char *target_end = get;
  while (*target_end && *target_end != ' ' && *target_end != '\r' &&
         *target_end != '\n') {
    target_end++;
  }
  if (target_end == get) {
    return 0;
  }

  char target[256];
  size_t target_len = (size_t)(target_end - get);
  if (target_len >= sizeof(target)) {
    target_len = sizeof(target) - 1;
  }
  memcpy(target, get, target_len);
  target[target_len] = '\0';

  const char *path = target;
  if (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0) {
    const char *host = strstr(path, "://");
    if (host) {
      host += 3;
      const char *slash = strchr(host, '/');
      path = slash ? slash : "/";
    }
  }

 /* /configssid=...&pwd=... */
  if (strncmp(path, "/config?", 8) == 0) {
 const char *query = path + 7; /* points to '' */
    if (out_ssid && out_pwd && extract_param(query, "ssid", out_ssid, 33) &&
        extract_param(query, "pwd", out_pwd, 65)) {
      return 2;
    }
    return 1;
  }

 /* / , /foo, /hotspot-detect.html and other GET paths -> config homepage */
  if (path[0] == '/') {
    return 1;
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
 /* ERROR */
  mqtt->error_time_ms = 0;
  mqtt->reconnect_delay_ms = RECONNECT_DELAY_INIT_MS;
}

int aqua_mqtt_get_net_status(const MqttClient *mqtt) {
  if (!mqtt)
    return 0;
  switch (mqtt->state) {
  case MQTT_STATE_ONLINE:
 return 2; /* */
  case MQTT_STATE_ERROR:
 return 0; /* / */
  case MQTT_STATE_AP_START:
  case MQTT_STATE_AP_CIPMUX:
  case MQTT_STATE_AP_CIPDINFO:
  case MQTT_STATE_AP_SERVER:
  case MQTT_STATE_AP_WAIT:
  case MQTT_STATE_AP_SENDING:
  case MQTT_STATE_AP_SEND_DATA:
  case MQTT_STATE_AP_CLOSE:
  case MQTT_STATE_AP_STOP:
 return 3; /* AP */
  default:
 return 1; /* */
  }
}
