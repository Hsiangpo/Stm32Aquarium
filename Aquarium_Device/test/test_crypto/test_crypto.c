/**
 * @file test_crypto.c
 * @brief SHA-256 和 HMAC-SHA256 单元测试
 */

#include "hmac.h"
#include "sha256.h"
#include <string.h>
#include <unity.h>


void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * SHA-256 测试（使用 NIST 标准测试向量）
 * ============================================================================
 */

void test_sha256_empty(void) {
  /* SHA-256("") =
   * e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
  uint8_t digest[SHA256_DIGEST_SIZE];
  sha256((const uint8_t *)"", 0, digest);

  uint8_t expected[] = {0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
                        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
                        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
                        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, digest, SHA256_DIGEST_SIZE);
}

void test_sha256_abc(void) {
  /* SHA-256("abc") =
   * ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad */
  uint8_t digest[SHA256_DIGEST_SIZE];
  sha256((const uint8_t *)"abc", 3, digest);

  uint8_t expected[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
                        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
                        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, digest, SHA256_DIGEST_SIZE);
}

void test_sha256_longer(void) {
  /* SHA-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") */
  const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  uint8_t digest[SHA256_DIGEST_SIZE];
  sha256((const uint8_t *)msg, strlen(msg), digest);

  uint8_t expected[] = {0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
                        0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
                        0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
                        0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, digest, SHA256_DIGEST_SIZE);
}

/* ============================================================================
 * HMAC-SHA256 测试（使用 RFC 4231 测试向量）
 * ============================================================================
 */

void test_hmac_sha256_rfc4231_test1(void) {
  /* RFC 4231 Test Case 1 */
  uint8_t key[20];
  memset(key, 0x0b, 20);
  const char *msg = "Hi There";
  uint8_t out[HMAC_SHA256_SIZE];

  hmac_sha256(key, 20, (const uint8_t *)msg, strlen(msg), out);

  uint8_t expected[] = {0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
                        0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
                        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
                        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, HMAC_SHA256_SIZE);
}

void test_hmac_sha256_rfc4231_test2(void) {
  /* RFC 4231 Test Case 2: "Jefe" + "what do ya want..." */
  const char *key = "Jefe";
  const char *msg = "what do ya want for nothing?";
  uint8_t out[HMAC_SHA256_SIZE];

  hmac_sha256((const uint8_t *)key, strlen(key), (const uint8_t *)msg,
              strlen(msg), out);

  uint8_t expected[] = {0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e,
                        0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75, 0xc7,
                        0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83,
                        0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, HMAC_SHA256_SIZE);
}

void test_hmac_sha256_hex_output(void) {
  /* 使用简单字符串测试 hex 输出 */
  char out_hex[65];
  aqua_hmac_sha256_hex("key", "message", out_hex);

  /* 验证长度和格式 */
  TEST_ASSERT_EQUAL(64, strlen(out_hex));

  /* 验证是有效的 hex 字符 */
  for (int i = 0; i < 64; i++) {
    char c = out_hex[i];
    TEST_ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  }
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  /* SHA-256 测试 */
  RUN_TEST(test_sha256_empty);
  RUN_TEST(test_sha256_abc);
  RUN_TEST(test_sha256_longer);

  /* HMAC-SHA256 测试 */
  RUN_TEST(test_hmac_sha256_rfc4231_test1);
  RUN_TEST(test_hmac_sha256_rfc4231_test2);
  RUN_TEST(test_hmac_sha256_hex_output);

  return UNITY_END();
}
