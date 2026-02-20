/**
 * @file aquarium_protocol.h
 * @brief 智能水族箱 MQTT 协议编解码接口
 *
 * 提供与华为云 IoTDA 通信的 JSON 编解码功能：
 * - 生成属性上报 JSON
 * - 解析命令下发 JSON
 * - 生成命令响应 JSON
 * - 解析/组装 MQTT Topic
 *
 * 参考文档：docs/Interface.MD, docs/HuaweiCloud.MD
 */

#ifndef AQUARIUM_PROTOCOL_H
#define AQUARIUM_PROTOCOL_H

#include "aquarium_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 错误码定义
 * ============================================================================
 */

typedef enum {
  AQUA_OK = 0,               /* 成功 */
  AQUA_ERR_NULL_PTR,         /* 空指针错误 */
  AQUA_ERR_BUFFER_TOO_SMALL, /* 缓冲区太小 */
  AQUA_ERR_JSON_PARSE,       /* JSON 解析错误 */
  AQUA_ERR_INVALID_COMMAND,  /* 无效的命令 */
  AQUA_ERR_INVALID_SERVICE,  /* 无效的服务 ID */
  AQUA_ERR_MISSING_FIELD,    /* 缺少必要字段 */
  AQUA_ERR_TOPIC_PARSE       /* Topic 解析错误 */
} AquaError;

/* ============================================================================
 * 属性上报 JSON 生成
 * ============================================================================
 */

/**
 * @brief 生成属性上报 JSON
 *
 * 生成符合华为云 IoTDA 格式的属性上报 JSON，用于发布到
 * Topic: $oc/devices/{device_id}/sys/properties/report
 *
 * @param props     属性结构体指针
 * @param buffer    输出缓冲区
 * @param buf_size  缓冲区大小
 * @param out_len   [输出] 实际生成的 JSON 长度（不含 '\0'）
 * @return AquaError 错误码
 *
 * 示例输出：
 * {
 *   "services": [{
 *     "service_id": "Aquarium",
 *     "properties": {
 *       "temperature": 26.5,
 *       "ph": 7.2,
 *       ...
 *     }
 *   }]
 * }
 */
AquaError aqua_build_properties_json(const AquariumProperties *props,
                                     char *buffer, size_t buf_size,
                                     size_t *out_len);

/* ============================================================================
 * 命令下发 JSON 解析
 * ============================================================================
 */

/**
 * @brief 解析命令下发 JSON
 *
 * 解析从华为云 IoTDA 收到的命令 JSON，提取 service_id、command_name 和参数。
 * Topic: $oc/devices/{device_id}/sys/commands/request_id={request_id}
 *
 * @param json      输入 JSON 字符串
 * @param json_len  JSON 字符串长度
 * @param cmd       [输出] 解析后的命令结构
 * @return AquaError 错误码
 *
 * 支持的命令：
 * - service_id=aquarium_control, command_name=control
 * - service_id=aquarium_threshold, command_name=set_thresholds
 * - service_id=aquariumConfig, command_name=set_config
 */
AquaError aqua_parse_command_json(const char *json, size_t json_len,
                                  ParsedCommand *cmd);

/* ============================================================================
 * 命令响应 JSON 生成
 * ============================================================================
 */

/**
 * @brief 生成命令响应 JSON
 *
 * 生成符合华为云 IoTDA 格式的命令响应 JSON，用于发布到
 * Topic: $oc/devices/{device_id}/sys/commands/response/request_id={request_id}
 *
 * @param resp      响应结构体指针
 * @param buffer    输出缓冲区
 * @param buf_size  缓冲区大小
 * @param out_len   [输出] 实际生成的 JSON 长度（不含 '\0'）
 * @return AquaError 错误码
 *
 * 示例输出（成功）：
 * {
 *   "result_code": 0,
 *   "response_name": "control_response",
 *   "paras": { "result": "success" }
 * }
 *
 * 示例输出（失败）：
 * {
 *   "result_code": 1,
 *   "response_name": "control_response",
 *   "paras": { "result": "failed", "error": "heater malfunction" }
 * }
 */
AquaError aqua_build_response_json(const CommandResponse *resp, char *buffer,
                                   size_t buf_size, size_t *out_len);

/* ============================================================================
 * MQTT Topic 解析/组装
 * ============================================================================
 */

/**
 * @brief 从命令请求 Topic 中提取 request_id
 *
 * 输入 Topic 格式：$oc/devices/{device_id}/sys/commands/request_id={request_id}
 *
 * @param topic        输入 Topic 字符串
 * @param request_id   [输出] request_id 缓冲区
 * @param req_id_size  request_id 缓冲区大小
 * @return AquaError 错误码
 */
AquaError aqua_extract_request_id(const char *topic, char *request_id,
                                  size_t req_id_size);

/**
 * @brief 构建命令响应 Topic
 *
 * 输出 Topic
 * 格式：$oc/devices/{device_id}/sys/commands/response/request_id={request_id}
 *
 * @param device_id    设备 ID
 * @param request_id   请求 ID（从命令请求 Topic 中提取）
 * @param buffer       输出 Topic 缓冲区
 * @param buf_size     缓冲区大小
 * @param out_len      [输出] 实际生成的 Topic 长度（不含 '\0'）
 * @return AquaError 错误码
 */
AquaError aqua_build_response_topic(const char *device_id,
                                    const char *request_id, char *buffer,
                                    size_t buf_size, size_t *out_len);

/**
 * @brief 构建属性上报 Topic
 *
 * 输出 Topic 格式：$oc/devices/{device_id}/sys/properties/report
 *
 * @param device_id    设备 ID
 * @param buffer       输出 Topic 缓冲区
 * @param buf_size     缓冲区大小
 * @param out_len      [输出] 实际生成的 Topic 长度（不含 '\0'）
 * @return AquaError 错误码
 */
AquaError aqua_build_report_topic(const char *device_id, char *buffer,
                                  size_t buf_size, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_PROTOCOL_H */
