/**
 * @file aquarium_at.c
 * @brief ESP32 AT 命令收发与行解析引擎实现
 */

#include "aquarium_at.h"
#include <string.h>

/* ============================================================================
 * 内部辅助函数
 * ============================================================================
 */

static bool is_final_ok(const char *line) { return (strcmp(line, "OK") == 0); }

static bool is_final_error(const char *line) {
  return (strcmp(line, "ERROR") == 0) ||
         (strncmp(line, "+CME ERROR:", 11) == 0) ||
         (strncmp(line, "+CMS ERROR:", 11) == 0);
}

static void push_urc(AtClient *client, const char *line, size_t len) {
  if (client->urc_count >= AT_URC_QUEUE_SIZE) {
    /* URC 队列满，丢弃最旧的 */
    client->urc_tail = (client->urc_tail + 1) % AT_URC_QUEUE_SIZE;
    client->urc_count--;
  }

  AtLine *urc = &client->urc_queue[client->urc_head];
  size_t copy_len = (len > AT_LINE_MAX_LEN) ? AT_LINE_MAX_LEN : len;
  memcpy(urc->data, line, copy_len);
  urc->data[copy_len] = '\0';
  urc->len = copy_len;
  urc->valid = true;

  client->urc_head = (client->urc_head + 1) % AT_URC_QUEUE_SIZE;
  client->urc_count++;
}

static void process_line(AtClient *client, const char *line, size_t len) {
  /* 跳过空行 */
  if (len == 0)
    return;

  if (client->state == AT_STATE_WAITING) {
    /* 命令执行中 */
    if (is_final_ok(line)) {
      if (client->expect_prompt) {
        /* 正在等待 > 提示符，OK 只是中间响应，继续等待 */
        client->got_ok = true;
      } else {
        client->state = AT_STATE_DONE_OK;
      }
    } else if (is_final_error(line)) {
      client->state = AT_STATE_DONE_ERROR;
    } else if (len == 1 && line[0] == '>') {
      /* AT+CIPSEND / AT+MQTTPUBRAW 的数据输入提示 */
      client->state = AT_STATE_GOT_PROMPT;
    } else {
      /* 非终止行，作为命令响应（保留第一个非空响应） */
      if (!client->cmd_response.valid) {
        size_t copy_len = (len > AT_LINE_MAX_LEN) ? AT_LINE_MAX_LEN : len;
        memcpy(client->cmd_response.data, line, copy_len);
        client->cmd_response.data[copy_len] = '\0';
        client->cmd_response.len = copy_len;
        client->cmd_response.valid = true;
      }

      /*
       * 同步放入 URC 队列，避免命令执行期间到来的异步事件（如 MQTT
       * 下行）被丢弃。 后续上层可按前缀/格式自行区分 URC 与多行命令响应。
       */
      push_urc(client, line, len);
    }
  } else {
    /* 空闲状态，放入 URC 队列 */
    push_urc(client, line, len);
  }
}

/* ============================================================================
 * 初始化
 * ============================================================================
 */

AtError aqua_at_init(AtClient *client, AtWriteFunc write_fn,
                     AtNowMsFunc now_ms_fn) {
  if (!client || !write_fn || !now_ms_fn) {
    return AT_ERR_NULL_PTR;
  }

  memset(client, 0, sizeof(AtClient));
  client->write_func = write_fn;
  client->now_ms_func = now_ms_fn;
  client->state = AT_STATE_IDLE;

  return AT_OK;
}

/* ============================================================================
 * 数据接收
 * ============================================================================
 */

AtError aqua_at_feed_rx(AtClient *client, const uint8_t *data, size_t len) {
  if (!client || !data) {
    return AT_ERR_NULL_PTR;
  }

  AtError result = AT_OK;

  for (size_t i = 0; i < len; i++) {
    uint8_t ch = data[i];

    /* 处理 CRLF */
    if (ch == '\r') {
      client->last_was_cr = true;
      continue;
    }

    if (ch == '\n') {
      if (client->last_was_cr || client->line_pos > 0) {
        /* 行结束，处理行 */
        client->line_buffer[client->line_pos] = '\0';
        process_line(client, client->line_buffer, client->line_pos);
        client->line_pos = 0;
      }
      client->last_was_cr = false;
      continue;
    }

    /* 如果之前有单独的 CR，先处理 */
    if (client->last_was_cr) {
      client->line_buffer[client->line_pos] = '\0';
      process_line(client, client->line_buffer, client->line_pos);
      client->line_pos = 0;
      client->last_was_cr = false;
    }

    /* 追加字符到行缓冲区 */
    if (client->line_pos < AT_LINE_MAX_LEN) {
      client->line_buffer[client->line_pos++] = (char)ch;

      /*
       * 支持"裸 >"（不带 CRLF）：
       * 如果正在等待 > 提示符，且当前只有一个字符 '>'，立即触发处理
       * 这样即使 ESP-AT 不输出 CRLF 也能正确识别
       */
      if (client->expect_prompt && client->line_pos == 1 && ch == '>') {
        client->line_buffer[1] = '\0';
        process_line(client, client->line_buffer, 1);
        client->line_pos = 0;
      }
    } else {
      /* 行过长，标记但继续接收 */
      result = AT_ERR_LINE_TOO_LONG;
    }
  }

  return result;
}

/* ============================================================================
 * 命令发送
 * ============================================================================
 */

AtError aqua_at_begin(AtClient *client, const char *cmd, uint32_t timeout_ms) {
  if (!client || !cmd) {
    return AT_ERR_NULL_PTR;
  }

  if (client->state == AT_STATE_WAITING) {
    return AT_ERR_BUSY;
  }

  /* 清空之前的响应和 prompt 状态 */
  memset(&client->cmd_response, 0, sizeof(AtLine));
  client->expect_prompt = false;
  client->got_ok = false;

  /* 发送命令 */
  size_t cmd_len = strlen(cmd);
  client->write_func((const uint8_t *)cmd, cmd_len);
  client->write_func((const uint8_t *)"\r\n", 2);

  /* 设置状态 */
  client->state = AT_STATE_WAITING;
  client->cmd_start_ms = client->now_ms_func();
  client->cmd_timeout_ms = timeout_ms;

  return AT_OK;
}

AtError aqua_at_begin_with_prompt(AtClient *client, const char *cmd,
                                  uint32_t timeout_ms) {
  if (!client || !cmd) {
    return AT_ERR_NULL_PTR;
  }

  if (client->state == AT_STATE_WAITING) {
    return AT_ERR_BUSY;
  }

  /* 清空之前的响应 */
  memset(&client->cmd_response, 0, sizeof(AtLine));
  client->expect_prompt = true; /* 期待 > 提示符 */
  client->got_ok = false;

  /* 发送命令 */
  size_t cmd_len = strlen(cmd);
  client->write_func((const uint8_t *)cmd, cmd_len);
  client->write_func((const uint8_t *)"\r\n", 2);

  /* 设置状态 */
  client->state = AT_STATE_WAITING;
  client->cmd_start_ms = client->now_ms_func();
  client->cmd_timeout_ms = timeout_ms;

  return AT_OK;
}

/* ============================================================================
 * 状态推进
 * ============================================================================
 */

AtState aqua_at_step(AtClient *client) {
  if (!client) {
    return AT_STATE_IDLE;
  }

  if (client->state == AT_STATE_WAITING) {
    /* 检查超时 */
    uint32_t now = client->now_ms_func();
    uint32_t elapsed = now - client->cmd_start_ms;

    if (elapsed >= client->cmd_timeout_ms) {
      client->state = AT_STATE_DONE_TIMEOUT;
    }
  }

  return client->state;
}

/* ============================================================================
 * 状态查询
 * ============================================================================
 */

AtState aqua_at_get_state(const AtClient *client) {
  if (!client)
    return AT_STATE_IDLE;
  return client->state;
}

const AtLine *aqua_at_get_response(const AtClient *client) {
  if (!client)
    return NULL;
  if (client->cmd_response.valid) {
    return &client->cmd_response;
  }
  return NULL;
}

void aqua_at_reset(AtClient *client) {
  if (!client)
    return;
  client->state = AT_STATE_IDLE;
  client->expect_prompt = false;
  client->got_ok = false;
  memset(&client->cmd_response, 0, sizeof(AtLine));
}

/* ============================================================================
 * URC 队列
 * ============================================================================
 */

bool aqua_at_has_urc(const AtClient *client) {
  if (!client)
    return false;
  return client->urc_count > 0;
}

AtError aqua_at_pop_line(AtClient *client, AtLine *out) {
  if (!client || !out) {
    return AT_ERR_NULL_PTR;
  }

  if (client->urc_count == 0) {
    return AT_ERR_NO_LINE;
  }

  AtLine *urc = &client->urc_queue[client->urc_tail];
  memcpy(out, urc, sizeof(AtLine));
  urc->valid = false;

  client->urc_tail = (client->urc_tail + 1) % AT_URC_QUEUE_SIZE;
  client->urc_count--;

  return AT_OK;
}
