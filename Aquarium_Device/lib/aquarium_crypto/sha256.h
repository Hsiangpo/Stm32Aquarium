/**
 * @file sha256.h
 * @brief SHA-256 哈希算法接口
 *
 * 纯 C 实现，无外部依赖
 */

#ifndef AQUA_SHA256_H
#define AQUA_SHA256_H

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

/* SHA-256 输出长度（字节） */
#define SHA256_DIGEST_SIZE 32

/* SHA-256 上下文 */
typedef struct {
  uint32_t state[8];
  uint64_t count;
  uint8_t buffer[64];
} Sha256Context;

/**
 * @brief 初始化 SHA-256 上下文
 */
void sha256_init(Sha256Context *ctx);

/**
 * @brief 更新哈希计算
 */
void sha256_update(Sha256Context *ctx, const uint8_t *data, size_t len);

/**
 * @brief 完成哈希计算
 * @param digest 输出缓冲区（32 字节）
 */
void sha256_final(Sha256Context *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

/**
 * @brief 一次性计算 SHA-256
 */
void sha256(const uint8_t *data, size_t len,
            uint8_t digest[SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* AQUA_SHA256_H */
