/**
 * @file hmac.h
 * @brief HMAC-SHA256 接口
 */

#ifndef AQUA_HMAC_H
#define AQUA_HMAC_H

#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/* HMAC-SHA256 输出长度（字节） */
#define HMAC_SHA256_SIZE 32

/**
 * @brief 计算 HMAC-SHA256
 *
 * @param key     密钥
 * @param key_len 密钥长度
 * @param msg     消息
 * @param msg_len 消息长度
 * @param out     输出缓冲区（32 字节）
 */
void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *msg,
                 size_t msg_len, uint8_t out[HMAC_SHA256_SIZE]);

/**
 * @brief 计算 HMAC-SHA256 并输出为十六进制字符串
 *
 * @param key     密钥（字符串）
 * @param msg     消息（字符串）
 * @param out_hex 输出缓冲区（64 字节 + 1 个 '\0'）
 */
void aqua_hmac_sha256_hex(const char *key, const char *msg, char out_hex[65]);

#ifdef __cplusplus
}
#endif

#endif /* AQUA_HMAC_H */
