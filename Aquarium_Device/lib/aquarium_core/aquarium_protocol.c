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

static bool aqua_append_text(char *buffer, size_t buf_size, size_t *pos,
                             const char *text) {
  if (!buffer || !pos || !text) {
    return false;
  }

  size_t len = strlen(text);
  if (*pos + len >= buf_size) {
    return false;
  }

  memcpy(buffer + *pos, text, len);
  *pos += len;
  buffer[*pos] = '\0';
  return true;
}

static void aqua_format_fixed2(float value, char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) {
    return;
  }

  float safe = aqua_safe_float(value);
  bool negative = safe < 0.0f;
  if (negative) {
    safe = -safe;
  }

  int32_t scaled = (int32_t)(safe * 100.0f + 0.5f);
  int32_t whole = scaled / 100;
  int32_t frac = scaled % 100;

  if (negative) {
    snprintf(buffer, buffer_size, "-%ld.%02ld", (long)whole, (long)frac);
  } else {
    snprintf(buffer, buffer_size, "%ld.%02ld", (long)whole, (long)frac);
  }
}

/* 简易 JSON 解析辅助函数声明 */
static const char *find_json_key(const char *json, const char *key);
static int parse_json_string(const char *start, char *out, size_t out_size);
static int parse_json_bool(const char *start, bool *out);
static int parse_json_int(const char *start, int32_t *out);
static int parse_json_int_or_string(const char *start, int32_t *out);
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

  char temp_str[24];
  char ph_str[24];
  char tds_str[24];
  char turb_str[24];
  char level_str[24];
  aqua_format_fixed2(props->temperature, temp_str, sizeof(temp_str));
  aqua_format_fixed2(props->ph, ph_str, sizeof(ph_str));
  aqua_format_fixed2(props->tds, tds_str, sizeof(tds_str));
  aqua_format_fixed2(props->turbidity, turb_str, sizeof(turb_str));
  aqua_format_fixed2(props->water_level, level_str, sizeof(level_str));

  char countdown_str[16];
  char alarm_str[16];
  snprintf(countdown_str, sizeof(countdown_str), "%d", (int)props->feed_countdown);
  snprintf(alarm_str, sizeof(alarm_str), "%d", (int)props->alarm_level);

  size_t pos = 0;
  buffer[0] = '\0';
  if (!aqua_append_text(buffer, buf_size, &pos, "{\"services\":[{") ||
      !aqua_append_text(buffer, buf_size, &pos,
                        "\"service_id\":\"" SERVICE_ID_AQUARIUM "\",") ||
      !aqua_append_text(buffer, buf_size, &pos, "\"properties\":{") ||
      !aqua_append_text(buffer, buf_size, &pos, "\"temperature\":") ||
      !aqua_append_text(buffer, buf_size, &pos, temp_str) ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"ph\":") ||
      !aqua_append_text(buffer, buf_size, &pos, ph_str) ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"tds\":") ||
      !aqua_append_text(buffer, buf_size, &pos, tds_str) ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"turbidity\":") ||
      !aqua_append_text(buffer, buf_size, &pos, turb_str) ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"water_level\":") ||
      !aqua_append_text(buffer, buf_size, &pos, level_str) ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"heater\":") ||
      !aqua_append_text(buffer, buf_size, &pos,
                        props->heater ? "true" : "false") ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"pump_in\":") ||
      !aqua_append_text(buffer, buf_size, &pos,
                        props->pump_in ? "true" : "false") ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"pump_out\":") ||
      !aqua_append_text(buffer, buf_size, &pos,
                        props->pump_out ? "true" : "false") ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"auto_mode\":") ||
      !aqua_append_text(buffer, buf_size, &pos,
                        props->auto_mode ? "true" : "false") ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"feed_countdown\":") ||
      !aqua_append_text(buffer, buf_size, &pos, countdown_str) ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"feeding_in_progress\":") ||
      !aqua_append_text(buffer, buf_size, &pos,
                        props->feeding_in_progress ? "true" : "false") ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"alarm_level\":") ||
      !aqua_append_text(buffer, buf_size, &pos, alarm_str) ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"alarm_muted\":") ||
      !aqua_append_text(buffer, buf_size, &pos,
                        props->alarm_muted ? "true" : "false") ||
      !aqua_append_text(buffer, buf_size, &pos, "}}]}")) {
    return AQUA_ERR_BUFFER_TOO_SMALL;
  }

  *out_len = pos;
  return AQUA_OK;
}

AquaError aqua_build_properties_json_compact(const AquariumProperties *props,
                                             char *buffer, size_t buf_size,
                                             size_t *out_len) {
  if (!props || !buffer || !out_len) {
    return AQUA_ERR_NULL_PTR;
  }

  char temp_str[24];
  char level_str[24];
  char alarm_str[16];
  aqua_format_fixed2(props->temperature, temp_str, sizeof(temp_str));
  aqua_format_fixed2(props->water_level, level_str, sizeof(level_str));
  snprintf(alarm_str, sizeof(alarm_str), "%d", (int)props->alarm_level);

  size_t pos = 0;
  buffer[0] = '\0';
  if (!aqua_append_text(buffer, buf_size, &pos, "{\"services\":[{") ||
      !aqua_append_text(buffer, buf_size, &pos,
                        "\"service_id\":\"" SERVICE_ID_AQUARIUM "\",") ||
      !aqua_append_text(buffer, buf_size, &pos, "\"properties\":{") ||
      !aqua_append_text(buffer, buf_size, &pos, "\"temperature\":") ||
      !aqua_append_text(buffer, buf_size, &pos, temp_str) ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"water_level\":") ||
      !aqua_append_text(buffer, buf_size, &pos, level_str) ||
      !aqua_append_text(buffer, buf_size, &pos, ",\"alarm_level\":") ||
      !aqua_append_text(buffer, buf_size, &pos, alarm_str) ||
      !aqua_append_text(buffer, buf_size, &pos, "}}]}")) {
    return AQUA_ERR_BUFFER_TOO_SMALL;
  }

  *out_len = pos;
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

static int parse_hex_nibble(char c) {
  if (c >= '0' && c <= '9')
    return (int)(c - '0');
  if (c >= 'a' && c <= 'f')
    return 10 + (int)(c - 'a');
  if (c >= 'A' && c <= 'F')
    return 10 + (int)(c - 'A');
  return -1;
}

static int parse_json_string(const char *start, char *out, size_t out_size) {
  if (!start || !out || out_size == 0 || *start != '"')
    return -1;

  start++;
  size_t i = 0;

  while (*start) {
    char ch = *start;
    if (ch == '"') {
      out[i] = '\0';
      return 0;
    }

    if (i >= out_size - 1) {
      return -1;
    }

    if (ch == '\\') {
      start++;
      ch = *start;
      if (ch == '\0') {
        return -1;
      }

      switch (ch) {
      case '"':
      case '\\':
      case '/':
        out[i++] = ch;
        start++;
        break;
      case 'b':
        out[i++] = '\b';
        start++;
        break;
      case 'f':
        out[i++] = '\f';
        start++;
        break;
      case 'n':
        out[i++] = '\n';
        start++;
        break;
      case 'r':
        out[i++] = '\r';
        start++;
        break;
      case 't':
        out[i++] = '\t';
        start++;
        break;
      case 'u': {
        int code = 0;
        for (int k = 0; k < 4; ++k) {
          start++;
          int nibble = parse_hex_nibble(*start);
          if (nibble < 0) {
            return -1;
          }
          code = (code << 4) | nibble;
        }

        if (code < 0x00 || code > 0x7F) {
          return -1;
        }
        out[i++] = (char)code;
        start++;
        break;
      }
      default:
        return -1;
      }

      continue;
    }

    out[i++] = ch;
    start++;
  }

  return -1;
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

static int parse_json_int_or_string(const char *start, int32_t *out) {
  if (!start || !out) {
    return -1;
  }

  if (parse_json_int(start, out) == 0) {
    return 0;
  }

  char text[16];
  if (parse_json_string(start, text, sizeof(text)) != 0) {
    return -1;
  }

  char *end = NULL;
  long val = strtol(text, &end, 10);
  if (end == text || *end != '\0') {
    return -1;
  }
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
    if (pos && parse_json_int_or_string(pos, &p->feed_amount) == 0)
      p->has_feed_amount = true;

    pos = find_json_key(paras, "target_temp");
    if (pos && parse_json_float(pos, &p->target_temp) == 0)
      p->has_target_temp = true;

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
