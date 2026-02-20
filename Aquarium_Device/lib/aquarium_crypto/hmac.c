/**
 * @file hmac.c
 * @brief HMAC-SHA256 实现
 */

#include "hmac.h"
#include <string.h>

#define BLOCK_SIZE 64

void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *msg,
                 size_t msg_len, uint8_t out[HMAC_SHA256_SIZE]) {
  uint8_t k_pad[BLOCK_SIZE];
  uint8_t i_pad[BLOCK_SIZE];
  uint8_t o_pad[BLOCK_SIZE];
  uint8_t temp_key[SHA256_DIGEST_SIZE];
  Sha256Context ctx;
  uint8_t inner_hash[SHA256_DIGEST_SIZE];

  /* 如果密钥过长，先哈希 */
  if (key_len > BLOCK_SIZE) {
    sha256(key, key_len, temp_key);
    key = temp_key;
    key_len = SHA256_DIGEST_SIZE;
  }

  /* 构造密钥填充 */
  memset(k_pad, 0, BLOCK_SIZE);
  memcpy(k_pad, key, key_len);

  /* 构造 ipad 和 opad */
  for (int i = 0; i < BLOCK_SIZE; i++) {
    i_pad[i] = k_pad[i] ^ 0x36;
    o_pad[i] = k_pad[i] ^ 0x5c;
  }

  /* 内层哈希: H(ipad || msg) */
  sha256_init(&ctx);
  sha256_update(&ctx, i_pad, BLOCK_SIZE);
  sha256_update(&ctx, msg, msg_len);
  sha256_final(&ctx, inner_hash);

  /* 外层哈希: H(opad || inner_hash) */
  sha256_init(&ctx);
  sha256_update(&ctx, o_pad, BLOCK_SIZE);
  sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
  sha256_final(&ctx, out);
}

void aqua_hmac_sha256_hex(const char *key, const char *msg, char out_hex[65]) {
  uint8_t hash[HMAC_SHA256_SIZE];
  static const char hex_chars[] = "0123456789abcdef";

  hmac_sha256((const uint8_t *)key, strlen(key), (const uint8_t *)msg,
              strlen(msg), hash);

  for (int i = 0; i < HMAC_SHA256_SIZE; i++) {
    out_hex[i * 2] = hex_chars[(hash[i] >> 4) & 0x0f];
    out_hex[i * 2 + 1] = hex_chars[hash[i] & 0x0f];
  }
  out_hex[64] = '\0';
}
