/**
 * @file aquarium_iotda_auth.h
 * @brief 华为云 IoTDA MQTT 鉴权参数生成
 *
 * 生成 MQTT 连接所需的 ClientId 和 Password
 * 参考：docs/HuaweiCloud.MD
 */

#ifndef AQUARIUM_IOTDA_AUTH_H
#define AQUARIUM_IOTDA_AUTH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 常量定义
 * ============================================================================
 */

/* ClientId 最大长度 */
#define IOTDA_CLIENT_ID_MAX_LEN 128

/* Password 长度（64 hex + 1 '\0'） */
#define IOTDA_PASSWORD_LEN 65

/* Timestamp 格式：YYYYMMDDHH */
#define IOTDA_TIMESTAMP_LEN 10

/* ============================================================================
 * 签名类型
 * ============================================================================
 */

typedef enum {
  IOTDA_SIGN_TYPE_NO_CHECK = 0, /* 不校验时间戳 */
  IOTDA_SIGN_TYPE_CHECK = 1     /* 校验时间戳 */
} IoTDASignType;

/* ============================================================================
 * ClientId 生成
 * ============================================================================
 */

/**
 * @brief 构建 MQTT ClientId
 *
 * 格式：{device_id}_0_{sign_type}_{timestamp}
 * 示例：690237639798273cc4fd09cb_MyAquarium_01_0_1_2025121312
 *
 * @param device_id  设备 ID
 * @param sign_type  签名类型（0 或 1）
 * @param timestamp  UTC 时间戳，格式 YYYYMMDDHH
 * @param out        输出缓冲区
 * @param out_size   缓冲区大小
 * @return 实际写入的字符数（不含 '\0'），-1 表示错误
 */
int aqua_iotda_build_client_id(const char *device_id, IoTDASignType sign_type,
                               const char *timestamp, char *out,
                               size_t out_size);

/* ============================================================================
 * Password 生成
 * ============================================================================
 */

/**
 * @brief 构建 MQTT Password
 *
 * 算法：HMACSHA256(message=secret, key=timestamp)
 * 根据华为云文档，参数顺序是"以 secret 为消息，timestamp 为密钥"
 *
 * @param secret     设备密钥
 * @param timestamp  UTC 时间戳，格式 YYYYMMDDHH
 * @param out_hex    输出缓冲区（64 hex + 1 '\0'）
 */
void aqua_iotda_build_password(const char *secret, const char *timestamp,
                               char out_hex[IOTDA_PASSWORD_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_IOTDA_AUTH_H */
