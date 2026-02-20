/**
 * @file aquarium_protocol.c
 * @brief 智能水族箱 MQTT 协议编解码实现
 */

#include "aquarium_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AQUA_JSON_MAX_LEN 1024


static bool aqua_is_finitef(float v) { return (v == v) && ((v - v) == 0.0f); }

static float aqua_safe_float(float v) { return aqua_is_finitef(v) ? v : 0.0f; }

/* 简易 JSON 解析辅助函数声明 */
static const char *find_json_key(const char *json, const char *key);
static int parse_json_string(const char *start, char *out, size_t out_size);
static int parse_json_bool(const char *start, bool *out);
static int parse_json_int(const char *start, int32_t *out);
static int parse_json_float(const char *start, float *out);

/* ============================================================================
 * 属性上报 JSON 生成
 * ============================================================================
 */

AquaError aqua_build_properties_json(const AquariumProperties *props,
                                     char *buffer, size_t buf_size,
                                     size_t *out_len) {
  if (!props || !buffer || !out_len) {
    return AQUA_ERR_NULL_PTR;
  }

  int len = snprintf(
      buffer, buf_size,
      "{\"services\":[{"
      "\"service_id\":\"" SERVICE_ID_AQUARIUM "\","
      "\"properties\":{"
      "\"temperature\":%.2f,"
      "\"ph\":%.2f,"
      "\"tds\":%.2f,"
      "\"turbidity\":%.2f,"
      "\"water_level\":%.2f,"
      "\"heater\":%s,"
      "\"pump_in\":%s,"
      "\"pump_out\":%s,"
      "\"auto_mode\":%s,"
      "\"feed_countdown\":%d,"
      "\"feeding_in_progress\":%s,"
      "\"alarm_level\":%d,"
      "\"alarm_muted\":%s"
      "}}]}",
      aqua_safe_float(props->temperature), aqua_safe_float(props->ph),
      aqua_safe_float(props->tds), aqua_safe_float(props->turbidity),
      aqua_safe_float(props->water_level), props->heater ? "true" : "false",
      props->pump_in ? "true" : "false", props->pump_out ? "true" : "false",
      props->auto_mode ? "true" : "false", (int)props->feed_countdown,
      props->feeding_in_progress ? "true" : "false", (int)props->alarm_level,
      props->alarm_muted ? "true" : "false");

  if (len < 0 || (size_t)len >= buf_size) {
    return AQUA_ERR_BUFFER_TOO_SMALL;
  }

  *out_len = (size_t)len;
  return AQUA_OK;
}

/* ============================================================================
 * 命令响应 JSON 生成
 * ============================================================================
 */

AquaError aqua_build_response_json(const CommandResponse *resp, char *buffer,
                                   size_t buf_size, size_t *out_len) {
  if (!resp || !buffer || !out_len) {
    return AQUA_ERR_NULL_PTR;
  }

  int len;
  if (resp->has_error) {
    len = snprintf(buffer, buf_size,
                   "{\"result_code\":%d,"
                   "\"response_name\":\"%s\","
                   "\"paras\":{\"result\":\"%s\",\"error\":\"%s\"}}",
                   (int)resp->result_code, resp->response_name, resp->result,
                   resp->error);
  } else {
    len = snprintf(buffer, buf_size,
                   "{\"result_code\":%d,"
                   "\"response_name\":\"%s\","
                   "\"paras\":{\"result\":\"%s\"}}",
                   (int)resp->result_code, resp->response_name, resp->result);
  }

  if (len < 0 || (size_t)len >= buf_size) {
    return AQUA_ERR_BUFFER_TOO_SMALL;
  }

  *out_len = (size_t)len;
  return AQUA_OK;
}

/* ============================================================================
 * MQTT Topic 解析/组装
 * ============================================================================
 */

AquaError aqua_extract_request_id(const char *topic, char *request_id,
                                  size_t req_id_size) {
  if (!topic || !request_id) {
    return AQUA_ERR_NULL_PTR;
  }

  const char *marker = "request_id=";
  const char *pos = strstr(topic, marker);
  if (!pos) {
    return AQUA_ERR_TOPIC_PARSE;
  }

  pos += strlen(marker);
  size_t i = 0;
  while (*pos && *pos != '/' && *pos != '?' && i < req_id_size - 1) {
    request_id[i++] = *pos++;
  }
  request_id[i] = '\0';

  if (i == 0) {
    return AQUA_ERR_TOPIC_PARSE;
  }

  return AQUA_OK;
}

AquaError aqua_build_response_topic(const char *device_id,
                                    const char *request_id, char *buffer,
                                    size_t buf_size, size_t *out_len) {
  if (!device_id || !request_id || !buffer || !out_len) {
    return AQUA_ERR_NULL_PTR;
  }

  int len = snprintf(buffer, buf_size,
                     "$oc/devices/%s/sys/commands/response/request_id=%s",
                     device_id, request_id);

  if (len < 0 || (size_t)len >= buf_size) {
    return AQUA_ERR_BUFFER_TOO_SMALL;
  }

  *out_len = (size_t)len;
  return AQUA_OK;
}

AquaError aqua_build_report_topic(const char *device_id, char *buffer,
                                  size_t buf_size, size_t *out_len) {
  if (!device_id || !buffer || !out_len) {
    return AQUA_ERR_NULL_PTR;
  }

  int len = snprintf(buffer, buf_size, "$oc/devices/%s/sys/properties/report",
                     device_id);

  if (len < 0 || (size_t)len >= buf_size) {
    return AQUA_ERR_BUFFER_TOO_SMALL;
  }

  *out_len = (size_t)len;
  return AQUA_OK;
}

/* ============================================================================
 * 简易 JSON 解析辅助函数实现
 * ============================================================================
 */

static const char *find_json_key(const char *json, const char *key) {
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *pos = strstr(json, pattern);
  if (!pos)
    return NULL;

  pos += strlen(pattern);
  while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')
    pos++;
  if (*pos != ':')
    return NULL;
  pos++;
  while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')
    pos++;
  return pos;
}

static int parse_json_string(const char *start, char *out, size_t out_size) {
  if (*start != '"')
    return -1;
  start++;
  size_t i = 0;
  while (*start && *start != '"' && i < out_size - 1) {
    out[i++] = *start++;
  }
  out[i] = '\0';
  return (*start == '"') ? 0 : -1;
}

static int parse_json_bool(const char *start, bool *out) {
  if (strncmp(start, "true", 4) == 0) {
    *out = true;
    return 0;
  } else if (strncmp(start, "false", 5) == 0) {
    *out = false;
    return 0;
  }
  return -1;
}

static int parse_json_int(const char *start, int32_t *out) {
  char *end;
  long val = strtol(start, &end, 10);
  if (end == start)
    return -1;
  *out = (int32_t)val;
  return 0;
}

static int parse_json_float(const char *start, float *out) {
  char *end;
  double val = strtod(start, &end);
  if (end == start)
    return -1;
  *out = (float)val;
  return 0;
}

/* ============================================================================
 * 命令下发 JSON 解析
 * ============================================================================
 */

AquaError aqua_parse_command_json(const char *json, size_t json_len,
                                  ParsedCommand *cmd) {
  if (!json || !cmd) {
    return AQUA_ERR_NULL_PTR;
  }

  if (json_len == 0 || json_len >= AQUA_JSON_MAX_LEN) {
    return AQUA_ERR_BUFFER_TOO_SMALL;
  }

  char json_buf[AQUA_JSON_MAX_LEN];
  memcpy(json_buf, json, json_len);
  json_buf[json_len] = '\0';
  json = json_buf;

  memset(cmd, 0, sizeof(ParsedCommand));
  cmd->type = COMMAND_TYPE_UNKNOWN;

  /* 解析 service_id */
  const char *pos = find_json_key(json, "service_id");
  if (!pos ||
      parse_json_string(pos, cmd->service_id, sizeof(cmd->service_id)) != 0) {
    return AQUA_ERR_MISSING_FIELD;
  }

  /* 解析 command_name */
  pos = find_json_key(json, "command_name");
  if (!pos || parse_json_string(pos, cmd->command_name,
                                sizeof(cmd->command_name)) != 0) {
    return AQUA_ERR_MISSING_FIELD;
  }

  /* 定位 paras 对象 */
  const char *paras = find_json_key(json, "paras");
  if (!paras) {
    return AQUA_ERR_MISSING_FIELD;
  }

  /* 根据 service_id 和 command_name 分流解析 */
  if (strcmp(cmd->service_id, SERVICE_ID_AQUARIUM_CONTROL) == 0 &&
      strcmp(cmd->command_name, COMMAND_NAME_CONTROL) == 0) {
    cmd->type = COMMAND_TYPE_CONTROL;
    ControlCommandParams *p = &cmd->params.control;

    pos = find_json_key(paras, "heater");
    if (pos && parse_json_bool(pos, &p->heater) == 0)
      p->has_heater = true;

    pos = find_json_key(paras, "pump_in");
    if (pos && parse_json_bool(pos, &p->pump_in) == 0)
      p->has_pump_in = true;

    pos = find_json_key(paras, "pump_out");
    if (pos && parse_json_bool(pos, &p->pump_out) == 0)
      p->has_pump_out = true;

    pos = find_json_key(paras, "mute");
    if (pos && parse_json_bool(pos, &p->mute) == 0)
      p->has_mute = true;

    pos = find_json_key(paras, "auto_mode");
    if (pos && parse_json_bool(pos, &p->auto_mode) == 0)
      p->has_auto_mode = true;

    pos = find_json_key(paras, "feed");
    if (pos && parse_json_bool(pos, &p->feed) == 0)
      p->has_feed = true;

    pos = find_json_key(paras, "feed_once_delay");
    if (pos && parse_json_int(pos, &p->feed_once_delay) == 0)
      p->has_feed_once_delay = true;

    pos = find_json_key(paras, "target_temp");
    if (pos && parse_json_float(pos, &p->target_temp) == 0)
      p->has_target_temp = true;

  } else if (strcmp(cmd->service_id, SERVICE_ID_AQUARIUM_THRESHOLD) == 0 &&
             strcmp(cmd->command_name, COMMAND_NAME_SET_THRESHOLDS) == 0) {
    cmd->type = COMMAND_TYPE_SET_THRESHOLDS;
    ThresholdCommandParams *p = &cmd->params.threshold;

    pos = find_json_key(paras, "temp_min");
    if (pos && parse_json_float(pos, &p->temp_min) == 0)
      p->has_temp_min = true;

    pos = find_json_key(paras, "temp_max");
    if (pos && parse_json_float(pos, &p->temp_max) == 0)
      p->has_temp_max = true;

    pos = find_json_key(paras, "ph_min");
    if (pos && parse_json_float(pos, &p->ph_min) == 0)
      p->has_ph_min = true;

    pos = find_json_key(paras, "ph_max");
    if (pos && parse_json_float(pos, &p->ph_max) == 0)
      p->has_ph_max = true;

    pos = find_json_key(paras, "tds_warn");
    if (pos && parse_json_int(pos, &p->tds_warn) == 0)
      p->has_tds_warn = true;

    pos = find_json_key(paras, "tds_critical");
    if (pos && parse_json_int(pos, &p->tds_critical) == 0)
      p->has_tds_critical = true;

    pos = find_json_key(paras, "turbidity_warn");
    if (pos && parse_json_int(pos, &p->turbidity_warn) == 0)
      p->has_turbidity_warn = true;

    pos = find_json_key(paras, "turbidity_critical");
    if (pos && parse_json_int(pos, &p->turbidity_critical) == 0)
      p->has_turbidity_critical = true;

    pos = find_json_key(paras, "level_min");
    if (pos && parse_json_int(pos, &p->level_min) == 0)
      p->has_level_min = true;

    pos = find_json_key(paras, "level_max");
    if (pos && parse_json_int(pos, &p->level_max) == 0)
      p->has_level_max = true;

    pos = find_json_key(paras, "feed_interval");
    if (pos && parse_json_int(pos, &p->feed_interval) == 0)
      p->has_feed_interval = true;

    pos = find_json_key(paras, "feed_amount");
    if (pos && parse_json_int(pos, &p->feed_amount) == 0)
      p->has_feed_amount = true;

  } else if (strcmp(cmd->service_id, SERVICE_ID_AQUARIUM_CONFIG) == 0 &&
             strcmp(cmd->command_name, COMMAND_NAME_SET_CONFIG) == 0) {
    cmd->type = COMMAND_TYPE_SET_CONFIG;
    ConfigCommandParams *p = &cmd->params.config;

    pos = find_json_key(paras, "wifi_ssid");
    if (pos &&
        parse_json_string(pos, p->wifi_ssid, sizeof(p->wifi_ssid)) == 0) {
      p->has_wifi_ssid = true;
    }

    pos = find_json_key(paras, "wifi_password");
    if (pos && parse_json_string(pos, p->wifi_password,
                                 sizeof(p->wifi_password)) == 0) {
      p->has_wifi_password = true;
    }

    pos = find_json_key(paras, "ph_offset");
    if (pos && parse_json_float(pos, &p->ph_offset) == 0)
      p->has_ph_offset = true;

    pos = find_json_key(paras, "tds_factor");
    if (pos && parse_json_float(pos, &p->tds_factor) == 0)
      p->has_tds_factor = true;

  } else {
    return AQUA_ERR_INVALID_COMMAND;
  }

  return AQUA_OK;
}
