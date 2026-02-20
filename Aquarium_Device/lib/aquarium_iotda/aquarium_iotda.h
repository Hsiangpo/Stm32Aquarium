/**
 * @file aquarium_iotda.h
 * @brief 华为云 IoTDA 适配层
 *
 * 将 aquarium_core（协议编解码）与 aquarium_logic（业务逻辑）串联：
 * - 生成属性上报 Topic + Payload
 * - 处理命令请求并生成响应 Topic + Payload
 */

#ifndef AQUARIUM_IOTDA_H
#define AQUARIUM_IOTDA_H

#include "aquarium_logic.h"
#include "aquarium_protocol.h"
#include "aquarium_types.h"
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 缓冲区大小常量
 * ============================================================================
 */

#define IOTDA_TOPIC_MAX_LEN 256
#define IOTDA_PAYLOAD_MAX_LEN 1024

/* ============================================================================
 * 属性上报生成
 * ============================================================================
 */

/**
 * @brief 生成属性上报的 Topic 和 Payload
 *
 * @param device_id     设备 ID
 * @param props         属性结构体指针
 * @param out_topic     [输出] Topic 缓冲区
 * @param topic_size    Topic 缓冲区大小
 * @param out_payload   [输出] Payload 缓冲区
 * @param payload_size  Payload 缓冲区大小
 * @param out_topic_len [输出] 实际 Topic 长度
 * @param out_payload_len [输出] 实际 Payload 长度
 * @return AquaError 错误码
 */
AquaError aqua_iotda_build_report(const char *device_id,
                                  const AquariumProperties *props,
                                  char *out_topic, size_t topic_size,
                                  char *out_payload, size_t payload_size,
                                  size_t *out_topic_len,
                                  size_t *out_payload_len);

/* ============================================================================
 * 命令处理结果
 * ============================================================================
 */

typedef struct {
  bool has_response; /* 是否需要发送响应 */
  char response_topic[IOTDA_TOPIC_MAX_LEN];
  size_t response_topic_len;
  char response_payload[IOTDA_PAYLOAD_MAX_LEN];
  size_t response_payload_len;
} IoTDACommandResult;

/* ============================================================================
 * 命令处理
 * ============================================================================
 */

/**
 * @brief 处理 MQTT 命令请求
 *
 * 流程：
 * 1. 从 in_topic 提取 request_id
 * 2. 解析 in_payload 得到命令
 * 3. 应用命令到 state
 * 4. 生成响应 topic 和 payload
 *
 * 响应约定：
 * - response_name = {command_name}_response
 * - 成功：result_code=0, result="success"
 * - 解析/参数错误：result_code=2, result="failed", error="..."
 *
 * @param device_id     设备 ID
 * @param in_topic      输入命令 Topic
 * @param in_payload    输入命令 Payload（JSON）
 * @param payload_len   Payload 长度
 * @param state         设备状态指针（会被更新）
 * @param result        [输出] 命令处理结果
 * @return AquaError 错误码（表示函数执行状态，非命令执行结果）
 */
AquaError aqua_iotda_handle_command(const char *device_id, const char *in_topic,
                                    const char *in_payload, size_t payload_len,
                                    AquariumState *state,
                                    IoTDACommandResult *result);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_IOTDA_H */
