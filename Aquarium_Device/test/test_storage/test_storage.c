/**
 * @file test_storage.c
 * @brief 配置持久化存储单元测试
 */

#include "aquarium_storage.h"
#include <string.h>
#include <unity.h>

/* ============================================================================
 * 模拟 Flash 后端
 * ============================================================================
 */

static uint8_t g_mock_flash[512];
static bool g_erase_fail = false;
static bool g_write_fail = false;

static size_t mock_read(uint32_t offset, void *buf, size_t len) {
  if (offset + len > sizeof(g_mock_flash))
    return 0;
  memcpy(buf, g_mock_flash + offset, len);
  return len;
}

static size_t mock_write(uint32_t offset, const void *buf, size_t len) {
  if (g_write_fail)
    return 0;
  if (offset + len > sizeof(g_mock_flash))
    return 0;
  memcpy(g_mock_flash + offset, buf, len);
  return len;
}

static bool mock_erase(void) {
  if (g_erase_fail)
    return false;
  memset(g_mock_flash, 0xFF, sizeof(g_mock_flash));
  return true;
}

static void reset_mocks(void) {
  memset(g_mock_flash, 0xFF, sizeof(g_mock_flash));
  g_erase_fail = false;
  g_write_fail = false;
}

void setUp(void) { reset_mocks(); }
void tearDown(void) {}

/* ============================================================================
 * 测试：保存和加载
 * ============================================================================
 */

void test_storage_save_load(void) {
  StorageContext ctx;
  aqua_storage_init(&ctx, mock_read, mock_write, mock_erase);

  DeviceConfig cfg_in = {0};
  strcpy(cfg_in.wifi_ssid, "TestSSID");
  strcpy(cfg_in.wifi_password, "TestPass123");
  cfg_in.ph_offset = 0.5f;
  cfg_in.tds_factor = 1.2f;

  StorageError err = aqua_storage_save(&ctx, &cfg_in);
  TEST_ASSERT_EQUAL(STORAGE_OK, err);

  DeviceConfig cfg_out = {0};
  err = aqua_storage_load(&ctx, &cfg_out);
  TEST_ASSERT_EQUAL(STORAGE_OK, err);

  TEST_ASSERT_EQUAL_STRING("TestSSID", cfg_out.wifi_ssid);
  TEST_ASSERT_EQUAL_STRING("TestPass123", cfg_out.wifi_password);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, cfg_out.ph_offset);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.2f, cfg_out.tds_factor);
}

/* ============================================================================
 * 测试：Magic 校验失败
 * ============================================================================
 */

void test_storage_magic_mismatch(void) {
  StorageContext ctx;
  aqua_storage_init(&ctx, mock_read, mock_write, mock_erase);

  /* 写入错误的 magic */
  uint32_t bad_magic = 0x12345678;
  memcpy(g_mock_flash, &bad_magic, sizeof(bad_magic));

  DeviceConfig cfg;
  StorageError err = aqua_storage_load(&ctx, &cfg);
  TEST_ASSERT_EQUAL(STORAGE_ERR_MAGIC_MISMATCH, err);
}

/* ============================================================================
 * 测试：CRC 校验失败
 * ============================================================================
 */

void test_storage_crc_mismatch(void) {
  StorageContext ctx;
  aqua_storage_init(&ctx, mock_read, mock_write, mock_erase);

  DeviceConfig cfg_in = {0};
  strcpy(cfg_in.wifi_ssid, "TestSSID");
  aqua_storage_save(&ctx, &cfg_in);

  /* 篡改配置区的数据 */
  g_mock_flash[sizeof(uint32_t) * 2 + 5] ^= 0xFF;

  DeviceConfig cfg_out;
  StorageError err = aqua_storage_load(&ctx, &cfg_out);
  TEST_ASSERT_EQUAL(STORAGE_ERR_CRC_MISMATCH, err);
}

/* ============================================================================
 * 测试：擦除失败
 * ============================================================================
 */

void test_storage_erase_fail(void) {
  StorageContext ctx;
  aqua_storage_init(&ctx, mock_read, mock_write, mock_erase);

  g_erase_fail = true;

  DeviceConfig cfg = {0};
  StorageError err = aqua_storage_save(&ctx, &cfg);
  TEST_ASSERT_EQUAL(STORAGE_ERR_ERASE_FAILED, err);
}

/* ============================================================================
 * 测试：写入失败
 * ============================================================================
 */

void test_storage_write_fail(void) {
  StorageContext ctx;
  aqua_storage_init(&ctx, mock_read, mock_write, mock_erase);

  g_write_fail = true;

  DeviceConfig cfg = {0};
  StorageError err = aqua_storage_save(&ctx, &cfg);
  TEST_ASSERT_EQUAL(STORAGE_ERR_WRITE_FAILED, err);
}

/* ============================================================================
 * 测试：CRC32 计算
 * ============================================================================
 */

void test_storage_crc32(void) {
  const char *data = "hello";
  uint32_t crc = aqua_storage_crc32(data, 5);
  /* 预期值：0x3610A686（标准 CRC32） */
  TEST_ASSERT_EQUAL_HEX32(0x3610A686, crc);
}

/* ============================================================================
 * 测试：空指针处理
 * ============================================================================
 */

void test_storage_null_ptr(void) {
  StorageContext ctx;
  aqua_storage_init(&ctx, mock_read, mock_write, mock_erase);

  TEST_ASSERT_EQUAL(STORAGE_ERR_NULL_PTR, aqua_storage_load(&ctx, NULL));
  TEST_ASSERT_EQUAL(STORAGE_ERR_NULL_PTR, aqua_storage_save(&ctx, NULL));
  TEST_ASSERT_EQUAL(STORAGE_ERR_NULL_PTR, aqua_storage_load(NULL, NULL));
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_storage_save_load);
  RUN_TEST(test_storage_magic_mismatch);
  RUN_TEST(test_storage_crc_mismatch);
  RUN_TEST(test_storage_erase_fail);
  RUN_TEST(test_storage_write_fail);
  RUN_TEST(test_storage_crc32);
  RUN_TEST(test_storage_null_ptr);

  return UNITY_END();
}
