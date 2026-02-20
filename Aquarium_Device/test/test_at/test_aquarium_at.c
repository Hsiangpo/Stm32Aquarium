/**
 * @file test_aquarium_at.c
 * @brief ESP32 AT 命令引擎单元测试
 */

#include "aquarium_at.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>


/* ============================================================================
 * Mock 回调
 * ============================================================================
 */

static uint8_t g_tx_buffer[256];
static size_t g_tx_len = 0;
static uint32_t g_mock_time_ms = 0;

static size_t mock_write(const uint8_t *data, size_t len) {
  if (g_tx_len + len <= sizeof(g_tx_buffer)) {
    memcpy(g_tx_buffer + g_tx_len, data, len);
    g_tx_len += len;
  }
  return len;
}

static uint32_t mock_now_ms(void) { return g_mock_time_ms; }

static void reset_mocks(void) {
  g_tx_len = 0;
  g_mock_time_ms = 0;
  memset(g_tx_buffer, 0, sizeof(g_tx_buffer));
}

void setUp(void) { reset_mocks(); }

void tearDown(void) {}

/* ============================================================================
 * 测试：初始化
 * ============================================================================
 */

void test_at_init_success(void) {
  AtClient client;
  AtError err = aqua_at_init(&client, mock_write, mock_now_ms);

  TEST_ASSERT_EQUAL(AT_OK, err);
  TEST_ASSERT_EQUAL(AT_STATE_IDLE, client.state);
}

void test_at_init_null_ptr(void) {
  AtClient client;
  TEST_ASSERT_EQUAL(AT_ERR_NULL_PTR,
                    aqua_at_init(NULL, mock_write, mock_now_ms));
  TEST_ASSERT_EQUAL(AT_ERR_NULL_PTR, aqua_at_init(&client, NULL, mock_now_ms));
  TEST_ASSERT_EQUAL(AT_ERR_NULL_PTR, aqua_at_init(&client, mock_write, NULL));
}

/* ============================================================================
 * 测试：命令发送
 * ============================================================================
 */

void test_at_begin_sends_command(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  AtError err = aqua_at_begin(&client, "AT", 1000);

  TEST_ASSERT_EQUAL(AT_OK, err);
  TEST_ASSERT_EQUAL(AT_STATE_WAITING, client.state);
  TEST_ASSERT_EQUAL(4, g_tx_len);
  TEST_ASSERT_EQUAL_STRING_LEN("AT\r\n", (char *)g_tx_buffer, 4);
}

void test_at_begin_busy(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  aqua_at_begin(&client, "AT", 1000);
  AtError err = aqua_at_begin(&client, "AT+GMR", 1000);

  TEST_ASSERT_EQUAL(AT_ERR_BUSY, err);
}

/* ============================================================================
 * 测试：CRLF 行解析
 * ============================================================================
 */

void test_feed_rx_single_line_crlf(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  const char *rx = "OK\r\n";
  aqua_at_feed_rx(&client, (const uint8_t *)rx, strlen(rx));

  /* IDLE 状态下收到的行应进入 URC 队列 */
  TEST_ASSERT_TRUE(aqua_at_has_urc(&client));

  AtLine line;
  AtError err = aqua_at_pop_line(&client, &line);
  TEST_ASSERT_EQUAL(AT_OK, err);
  TEST_ASSERT_EQUAL_STRING("OK", line.data);
}

void test_feed_rx_crlf_split_across_fragments(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  /* 分片 1：数据 + CR */
  const char *frag1 = "Hello\r";
  aqua_at_feed_rx(&client, (const uint8_t *)frag1, strlen(frag1));
  TEST_ASSERT_FALSE(aqua_at_has_urc(&client)); /* 行未完成 */

  /* 分片 2：LF */
  const char *frag2 = "\n";
  aqua_at_feed_rx(&client, (const uint8_t *)frag2, strlen(frag2));
  TEST_ASSERT_TRUE(aqua_at_has_urc(&client));

  AtLine line;
  aqua_at_pop_line(&client, &line);
  TEST_ASSERT_EQUAL_STRING("Hello", line.data);
}

void test_feed_rx_multiple_lines_at_once(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  const char *rx = "Line1\r\nLine2\r\nLine3\r\n";
  aqua_at_feed_rx(&client, (const uint8_t *)rx, strlen(rx));

  TEST_ASSERT_EQUAL(3, client.urc_count);

  AtLine line;
  aqua_at_pop_line(&client, &line);
  TEST_ASSERT_EQUAL_STRING("Line1", line.data);

  aqua_at_pop_line(&client, &line);
  TEST_ASSERT_EQUAL_STRING("Line2", line.data);

  aqua_at_pop_line(&client, &line);
  TEST_ASSERT_EQUAL_STRING("Line3", line.data);
}

void test_feed_rx_empty_lines_ignored(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  const char *rx = "\r\n\r\nData\r\n\r\n";
  aqua_at_feed_rx(&client, (const uint8_t *)rx, strlen(rx));

  /* 只有非空行会被处理 */
  TEST_ASSERT_EQUAL(1, client.urc_count);

  AtLine line;
  aqua_at_pop_line(&client, &line);
  TEST_ASSERT_EQUAL_STRING("Data", line.data);
}

/* ============================================================================
 * 测试：OK/ERROR 终止识别
 * ============================================================================
 */

void test_command_ok_response(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  aqua_at_begin(&client, "AT", 1000);
  TEST_ASSERT_EQUAL(AT_STATE_WAITING, client.state);

  const char *rx = "OK\r\n";
  aqua_at_feed_rx(&client, (const uint8_t *)rx, strlen(rx));

  TEST_ASSERT_EQUAL(AT_STATE_DONE_OK, client.state);
}

void test_command_error_response(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  aqua_at_begin(&client, "AT+INVALID", 1000);

  const char *rx = "ERROR\r\n";
  aqua_at_feed_rx(&client, (const uint8_t *)rx, strlen(rx));

  TEST_ASSERT_EQUAL(AT_STATE_DONE_ERROR, client.state);
}

void test_command_cme_error_response(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  aqua_at_begin(&client, "AT+COPS?", 1000);

  const char *rx = "+CME ERROR: 30\r\n";
  aqua_at_feed_rx(&client, (const uint8_t *)rx, strlen(rx));

  TEST_ASSERT_EQUAL(AT_STATE_DONE_ERROR, client.state);
}

void test_command_with_response_line(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  aqua_at_begin(&client, "AT+GMR", 1000);

  const char *rx = "AT version:1.0\r\nOK\r\n";
  aqua_at_feed_rx(&client, (const uint8_t *)rx, strlen(rx));

  TEST_ASSERT_EQUAL(AT_STATE_DONE_OK, client.state);

  const AtLine *resp = aqua_at_get_response(&client);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL_STRING("AT version:1.0", resp->data);
}

/* ============================================================================
 * 测试：超时
 * ============================================================================
 */

void test_command_timeout(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  g_mock_time_ms = 1000;
  aqua_at_begin(&client, "AT", 500);

  /* 推进时间 499ms，未超时 */
  g_mock_time_ms = 1499;
  AtState state = aqua_at_step(&client);
  TEST_ASSERT_EQUAL(AT_STATE_WAITING, state);

  /* 推进时间到 500ms，超时 */
  g_mock_time_ms = 1500;
  state = aqua_at_step(&client);
  TEST_ASSERT_EQUAL(AT_STATE_DONE_TIMEOUT, state);
}

/* ============================================================================
 * 测试：重置
 * ============================================================================
 */

void test_reset_to_idle(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  aqua_at_begin(&client, "AT", 1000);
  const char *rx = "OK\r\n";
  aqua_at_feed_rx(&client, (const uint8_t *)rx, strlen(rx));

  TEST_ASSERT_EQUAL(AT_STATE_DONE_OK, client.state);

  aqua_at_reset(&client);
  TEST_ASSERT_EQUAL(AT_STATE_IDLE, client.state);
}

/* ============================================================================
 * 测试：URC 队列
 * ============================================================================
 */

void test_urc_queue_overflow(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  /* 发送超过队列容量的 URC */
  for (int i = 0; i < AT_URC_QUEUE_SIZE + 2; i++) {
    char line[32];
    snprintf(line, sizeof(line), "URC%d\r\n", i);
    aqua_at_feed_rx(&client, (const uint8_t *)line, strlen(line));
  }

  /* 队列应该只保留最新的 AT_URC_QUEUE_SIZE 个 */
  TEST_ASSERT_EQUAL(AT_URC_QUEUE_SIZE, client.urc_count);

  /* 第一个弹出的应该是 URC2（最旧的两个被丢弃） */
  AtLine line;
  aqua_at_pop_line(&client, &line);
  TEST_ASSERT_EQUAL_STRING("URC2", line.data);
}

void test_pop_line_empty_queue(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  AtLine line;
  AtError err = aqua_at_pop_line(&client, &line);
  TEST_ASSERT_EQUAL(AT_ERR_NO_LINE, err);
}

/* ============================================================================
 * 测试：行过长
 * ============================================================================
 */

void test_line_too_long_truncated(void) {
  AtClient client;
  aqua_at_init(&client, mock_write, mock_now_ms);

  /* 构造超长行 */
  char long_line[AT_LINE_MAX_LEN + 50];
  memset(long_line, 'A', sizeof(long_line) - 3);
  long_line[sizeof(long_line) - 3] = '\r';
  long_line[sizeof(long_line) - 2] = '\n';
  long_line[sizeof(long_line) - 1] = '\0';

  AtError err =
      aqua_at_feed_rx(&client, (const uint8_t *)long_line, strlen(long_line));
  TEST_ASSERT_EQUAL(AT_ERR_LINE_TOO_LONG, err);

  /* 行被截断但仍可读 */
  TEST_ASSERT_TRUE(aqua_at_has_urc(&client));

  AtLine line;
  aqua_at_pop_line(&client, &line);
  TEST_ASSERT_EQUAL(AT_LINE_MAX_LEN, line.len);
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  /* 初始化测试 */
  RUN_TEST(test_at_init_success);
  RUN_TEST(test_at_init_null_ptr);

  /* 命令发送测试 */
  RUN_TEST(test_at_begin_sends_command);
  RUN_TEST(test_at_begin_busy);

  /* CRLF 行解析测试 */
  RUN_TEST(test_feed_rx_single_line_crlf);
  RUN_TEST(test_feed_rx_crlf_split_across_fragments);
  RUN_TEST(test_feed_rx_multiple_lines_at_once);
  RUN_TEST(test_feed_rx_empty_lines_ignored);

  /* OK/ERROR 终止测试 */
  RUN_TEST(test_command_ok_response);
  RUN_TEST(test_command_error_response);
  RUN_TEST(test_command_cme_error_response);
  RUN_TEST(test_command_with_response_line);

  /* 超时测试 */
  RUN_TEST(test_command_timeout);

  /* 重置测试 */
  RUN_TEST(test_reset_to_idle);

  /* URC 队列测试 */
  RUN_TEST(test_urc_queue_overflow);
  RUN_TEST(test_pop_line_empty_queue);

  /* 行过长测试 */
  RUN_TEST(test_line_too_long_truncated);

  return UNITY_END();
}
