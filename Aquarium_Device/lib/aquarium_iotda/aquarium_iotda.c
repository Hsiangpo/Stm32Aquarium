/**
 * @file aquarium_iotda.c
 * @brief 华为云 IoTDA 适配层实现
 */

#include "aquarium_iotda.h"
#include <stdio.h>
#include <string.h>


/* ============================================================================
 * 属性上报生成
 * ============================================================================
 */

AquaError aqua_iotda_build_report(const char *device_id,
                                  const AquariumProperties *props,
                                  char *out_topic, size_t topic_size,
                                  char *out_payload, size_t payload_size,
                                  size_t *out_topic_len,
                                  size_t *out_payload_len) {
  if (!device_id || !props || !out_topic || !out_payload || !out_topic_len ||
      !out_payload_len) {
    return AQUA_ERR_NULL_PTR;
  }

  /* 生成 Topic */
  AquaError err =
      aqua_build_report_topic(device_id, out_topic, topic_size, out_topic_len);
  if (err != AQUA_OK) {
    return err;
  }

  /* 生成 Payload */
  err = aqua_build_properties_json(props, out_payload, payload_size,
                                   out_payload_len);

  return err;
}

/* ============================================================================
 * 辅助函数：构建响应名称
 * ============================================================================
 */

static void build_response_name(const char *command_name, char *out,
                                size_t size) {
  if (!out || size == 0) {
    return;
  }

  const char *suffix = "_response";

  if (!command_name) {
    strncpy(out, "response", size - 1);
    out[size - 1] = '\0';
    return;
  }

  size_t name_len = strlen(command_name);
  size_t suffix_len = strlen(suffix);

  if (name_len + suffix_len + 1 > size) {
    strncpy(out, "response", size - 1);
    out[size - 1] = '\0';
    return;
  }

  memcpy(out, command_name, name_len);
  memcpy(out + name_len, suffix, suffix_len + 1);
}

/* ============================================================================
 * 辅助函数：构建错误响应
 * ============================================================================
 */

static AquaError build_error_response(const char *device_id,
                                      const char *request_id,
                                      const char *command_name,
                                      const char *error_msg,
                                      IoTDACommandResult *result) {
  result->has_response = true;

  /* 构建响应 Topic */
  AquaError err = aqua_build_response_topic(
      device_id, request_id, result->response_topic,
      sizeof(result->response_topic), &result->response_topic_len);
  if (err != AQUA_OK) {
    return err;
  }

  /* 构建响应 Payload */
  CommandResponse resp = {0};
  resp.result_code = 2; /* 参数错误 */
  build_response_name(command_name, resp.response_name,
                      sizeof(resp.response_name));
  strncpy(resp.result, "failed", sizeof(resp.result) - 1);
  strncpy(resp.error, error_msg, sizeof(resp.error) - 1);
  resp.has_error = true;

  err = aqua_build_response_json(&resp, result->response_payload,
                                 sizeof(result->response_payload),
                                 &result->response_payload_len);

  return err;
}

/* ============================================================================
 * 辅助函数：构建成功响应
 * ============================================================================
 */

static AquaError build_success_response(const char *device_id,
                                        const char *request_id,
                                        const char *command_name,
                                        IoTDACommandResult *result) {
  result->has_response = true;

  /* 构建响应 Topic */
  AquaError err = aqua_build_response_topic(
      device_id, request_id, result->response_topic,
      sizeof(result->response_topic), &result->response_topic_len);
  if (err != AQUA_OK) {
    return err;
  }

  /* 构建响应 Payload */
  CommandResponse resp = {0};
  resp.result_code = 0; /* 成功 */
  build_response_name(command_name, resp.response_name,
                      sizeof(resp.response_name));
  strncpy(resp.result, "success", sizeof(resp.result) - 1);
  resp.has_error = false;

  err = aqua_build_response_json(&resp, result->response_payload,
                                 sizeof(result->response_payload),
                                 &result->response_payload_len);

  return err;
}

/* ============================================================================
 * 命令处理
 * ============================================================================
 */

AquaError aqua_iotda_handle_command(const char *device_id, const char *in_topic,
                                    const char *in_payload, size_t payload_len,
                                    AquariumState *state,
                                    IoTDACommandResult *result) {
  if (!device_id || !in_topic || !in_payload || !state || !result) {
    return AQUA_ERR_NULL_PTR;
  }

  memset(result, 0, sizeof(IoTDACommandResult));

  /* 1. 提取 request_id */
  char request_id[64] = {0};
  AquaError err =
      aqua_extract_request_id(in_topic, request_id, sizeof(request_id));
  if (err != AQUA_OK) {
    /* Topic 解析失败，无法生成响应 */
    result->has_response = false;
    return err;
  }

  /* 2. 解析命令 */
  ParsedCommand cmd = {0};
  err = aqua_parse_command_json(in_payload, payload_len, &cmd);
  if (err != AQUA_OK) {
    /* 解析失败，返回错误响应 */
    const char *error_msg = "JSON parse error";
    if (err == AQUA_ERR_MISSING_FIELD) {
      error_msg = "missing required field";
    } else if (err == AQUA_ERR_INVALID_COMMAND) {
      error_msg = "unknown command";
    }
    const char *command_name =
        (cmd.command_name[0] != '\0') ? cmd.command_name : "unknown";
    return build_error_response(device_id, request_id, command_name, error_msg,
                                result);
  }

  /* 3. 应用命令到状态 */
  err = aqua_logic_apply_command(state, &cmd);
  if (err != AQUA_OK) {
    return build_error_response(device_id, request_id, cmd.command_name,
                                "command apply failed", result);
  }

  /* 4. 构建成功响应 */
  return build_success_response(device_id, request_id, cmd.command_name,
                                result);
}
