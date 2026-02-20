/**
 * @file test_iotda_auth.c
 * @brief 华为云 IoTDA 鉴权单元测试
 */

#include "aquarium_iotda_auth.h"
#include <string.h>
#include <unity.h>


void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * 官方校验向量测试（docs/HuaweiCloud.MD）
 * ============================================================================
 */

void test_password_official_vector(void) {
  /*
   * 官方测试向量：
   * - secret = "12345678"
   * - timestamp = "2025041401"
   * - 期望 password =
   * c75150e6cb841417396819e4d2ee4358a416344a03a083e3a8567074ddec820a
   */
  char password[IOTDA_PASSWORD_LEN];

  aqua_iotda_build_password("12345678", "2025041401", password);

  TEST_ASSERT_EQUAL_STRING(
      "c75150e6cb841417396819e4d2ee4358a416344a03a083e3a8567074ddec820a",
      password);
}

/* ============================================================================
 * ClientId 生成测试
 * ============================================================================
 */

void test_client_id_format(void) {
  char client_id[IOTDA_CLIENT_ID_MAX_LEN];

  int len = aqua_iotda_build_client_id("690237639798273cc4fd09cb_MyAquarium_01",
                                       IOTDA_SIGN_TYPE_CHECK, "2025121312",
                                       client_id, sizeof(client_id));

  TEST_ASSERT_GREATER_THAN(0, len);
  TEST_ASSERT_EQUAL_STRING(
      "690237639798273cc4fd09cb_MyAquarium_01_0_1_2025121312", client_id);
}

void test_client_id_no_check(void) {
  char client_id[IOTDA_CLIENT_ID_MAX_LEN];

  aqua_iotda_build_client_id("device123", IOTDA_SIGN_TYPE_NO_CHECK,
                             "2025010100", client_id, sizeof(client_id));

  TEST_ASSERT_EQUAL_STRING("device123_0_0_2025010100", client_id);
}

void test_client_id_buffer_too_small(void) {
  char client_id[10]; /* 太小 */

  int len =
      aqua_iotda_build_client_id("a_long_device_id", IOTDA_SIGN_TYPE_CHECK,
                                 "2025121312", client_id, sizeof(client_id));

  TEST_ASSERT_EQUAL(-1, len);
}

void test_client_id_null_params(void) {
  char client_id[IOTDA_CLIENT_ID_MAX_LEN];

  TEST_ASSERT_EQUAL(-1, aqua_iotda_build_client_id(NULL, IOTDA_SIGN_TYPE_CHECK,
                                                   "2025", client_id,
                                                   sizeof(client_id)));
  TEST_ASSERT_EQUAL(-1, aqua_iotda_build_client_id("dev", IOTDA_SIGN_TYPE_CHECK,
                                                   NULL, client_id,
                                                   sizeof(client_id)));
  TEST_ASSERT_EQUAL(-1, aqua_iotda_build_client_id("dev", IOTDA_SIGN_TYPE_CHECK,
                                                   "2025", NULL,
                                                   sizeof(client_id)));
}

/* ============================================================================
 * Password 生成测试
 * ============================================================================
 */

void test_password_length(void) {
  char password[IOTDA_PASSWORD_LEN];

  aqua_iotda_build_password("secret", "2025121312", password);

  TEST_ASSERT_EQUAL(64, strlen(password));
}

void test_password_hex_format(void) {
  char password[IOTDA_PASSWORD_LEN];

  aqua_iotda_build_password("secret", "timestamp", password);

  /* 验证是有效的小写 hex 字符 */
  for (int i = 0; i < 64; i++) {
    char c = password[i];
    TEST_ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  }
}

void test_password_different_inputs(void) {
  char pwd1[IOTDA_PASSWORD_LEN];
  char pwd2[IOTDA_PASSWORD_LEN];

  aqua_iotda_build_password("secret1", "2025121312", pwd1);
  aqua_iotda_build_password("secret2", "2025121312", pwd2);

  /* 不同密钥应产生不同密码 */
  TEST_ASSERT_TRUE(strcmp(pwd1, pwd2) != 0);
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  /* 官方校验向量（最重要） */
  RUN_TEST(test_password_official_vector);

  /* ClientId 测试 */
  RUN_TEST(test_client_id_format);
  RUN_TEST(test_client_id_no_check);
  RUN_TEST(test_client_id_buffer_too_small);
  RUN_TEST(test_client_id_null_params);

  /* Password 测试 */
  RUN_TEST(test_password_length);
  RUN_TEST(test_password_hex_format);
  RUN_TEST(test_password_different_inputs);

  return UNITY_END();
}
