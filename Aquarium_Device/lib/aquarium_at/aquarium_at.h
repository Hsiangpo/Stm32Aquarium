/**
 * @file aquarium_at.h
 * @brief ESP32 AT 命令收发与行解析引擎
 *
 * 可单测、可移植的 AT-Client 基础层：
 * - 非阻塞设计，适合主循环集成
 * - 按 CRLF 切行解析
 * - 支持 OK/ERROR 终止识别与超时
 * - URC（未归属命令的响应行）队列
 */

#ifndef AQUARIUM_AT_H
#define AQUARIUM_AT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 配置常量
 * ============================================================================
 */

#ifndef AT_RX_BUFFER_SIZE
#define AT_RX_BUFFER_SIZE 512
#endif

#ifndef AT_LINE_MAX_LEN
#define AT_LINE_MAX_LEN 512 /* 需足够容纳 MQTT 下行 JSON（如 +MQTTSUBRECV） */
#endif

#ifndef AT_URC_QUEUE_SIZE
#define AT_URC_QUEUE_SIZE 8
#endif

/* ============================================================================
 * 错误码
 * ============================================================================
 */

typedef enum {
  AT_OK = 0,            /* 成功 */
  AT_ERR_NULL_PTR,      /* 空指针 */
  AT_ERR_BUSY,          /* 命令正在执行中 */
  AT_ERR_BUFFER_FULL,   /* 缓冲区满 */
  AT_ERR_LINE_TOO_LONG, /* 行过长被截断 */
  AT_ERR_TIMEOUT,       /* 命令超时 */
  AT_ERR_CMD_ERROR,     /* 收到 ERROR 响应 */
  AT_ERR_NO_LINE        /* 无可用行 */
} AtError;

/* ============================================================================
 * 命令状态
 * ============================================================================
 */

typedef enum {
  AT_STATE_IDLE = 0,    /* 空闲，可发送新命令 */
  AT_STATE_WAITING,     /* 等待响应中 */
  AT_STATE_DONE_OK,     /* 命令完成，收到 OK */
  AT_STATE_GOT_PROMPT,  /* 收到 > 提示符，可发送数据 */
  AT_STATE_DONE_ERROR,  /* 命令完成，收到 ERROR */
  AT_STATE_DONE_TIMEOUT /* 命令超时 */
} AtState;

/* ============================================================================
 * 回调函数类型
 * ============================================================================
 */

/**
 * @brief 写入数据到 UART 的回调
 * @param data  数据指针
 * @param len   数据长度
 * @return 实际写入的字节数
 */
typedef size_t (*AtWriteFunc)(const uint8_t *data, size_t len);

/**
 * @brief 获取当前时间戳的回调（毫秒）
 * @return 当前时间戳（毫秒）
 */
typedef uint32_t (*AtNowMsFunc)(void);

/* ============================================================================
 * AT 行结构
 * ============================================================================
 */

typedef struct {
  char data[AT_LINE_MAX_LEN + 1];
  size_t len;
  bool valid;
} AtLine;

/* ============================================================================
 * AT 客户端上下文
 * ============================================================================
 */

typedef struct {
  /* 回调函数 */
  AtWriteFunc write_func;
  AtNowMsFunc now_ms_func;

  /* RX 缓冲区 */
  uint8_t rx_buffer[AT_RX_BUFFER_SIZE];
  size_t rx_head;   /* 写入位置 */
  size_t rx_tail;   /* 读取位置 */
  bool rx_overflow; /* 溢出标志 */

  /* 行解析状态 */
  char line_buffer[AT_LINE_MAX_LEN + 1];
  size_t line_pos;
  bool last_was_cr; /* 上一个字符是否为 CR */

  /* 命令状态 */
  AtState state;
  uint32_t cmd_start_ms;
  uint32_t cmd_timeout_ms;
  bool expect_prompt; /* 是否期待 > 提示符（用于 CIPSEND/MQTTPUBRAW） */
  bool got_ok;        /* 已收到 OK（等待 > 提示符时使用） */

  /* 当前命令的响应行（第一行非空响应） */
  AtLine cmd_response;

  /* URC 队列 */
  AtLine urc_queue[AT_URC_QUEUE_SIZE];
  size_t urc_head;
  size_t urc_tail;
  size_t urc_count;
} AtClient;

/* ============================================================================
 * 初始化
 * ============================================================================
 */

/**
 * @brief 初始化 AT 客户端
 *
 * @param client    AT 客户端上下文指针
 * @param write_fn  写入 UART 的回调函数
 * @param now_ms_fn 获取当前时间戳的回调函数
 * @return AtError 错误码
 */
AtError aqua_at_init(AtClient *client, AtWriteFunc write_fn,
                     AtNowMsFunc now_ms_fn);

/* ============================================================================
 * 数据接收
 * ============================================================================
 */

/**
 * @brief 喂入 UART 收到的字节流
 *
 * 按 CRLF 切行，解析出的行会被分类：
 * - 命令执行中时：归属到当前命令响应或检测 OK/ERROR
 * - 空闲时：放入 URC 队列
 *
 * @param client AT 客户端上下文指针
 * @param data   接收到的数据
 * @param len    数据长度
 * @return AtError 错误码（AT_ERR_BUFFER_FULL 表示溢出）
 */
AtError aqua_at_feed_rx(AtClient *client, const uint8_t *data, size_t len);

/* ============================================================================
 * 命令发送
 * ============================================================================
 */

/**
 * @brief 开始发送一条 AT 命令
 *
 * 会自动在命令末尾追加 \r\n
 * 单通道串行执行，如已有命令在执行则返回 AT_ERR_BUSY
 *
 * @param client     AT 客户端上下文指针
 * @param cmd        AT 命令字符串（不含 \r\n）
 * @param timeout_ms 超时时间（毫秒）
 * @return AtError 错误码
 */
AtError aqua_at_begin(AtClient *client, const char *cmd, uint32_t timeout_ms);

/**
 * @brief 开始发送需要等待 > 提示符的 AT 命令
 *
 * 用于 AT+CIPSEND / AT+MQTTPUBRAW 等命令
 * 命令响应顺序：OK -> > 提示符
 * 收到 > 后状态变为 AT_STATE_GOT_PROMPT
 *
 * @param client     AT 客户端上下文指针
 * @param cmd        AT 命令字符串（不含 \r\n）
 * @param timeout_ms 超时时间（毫秒）
 * @return AtError 错误码
 */
AtError aqua_at_begin_with_prompt(AtClient *client, const char *cmd,
                                  uint32_t timeout_ms);

/* ============================================================================
 * 状态推进
 * ============================================================================
 */

/**
 * @brief 推进 AT 状态机
 *
 * 检查超时，处理接收缓冲区中的行，更新命令状态
 *
 * @param client AT 客户端上下文指针
 * @return 当前状态
 */
AtState aqua_at_step(AtClient *client);

/* ============================================================================
 * 状态查询
 * ============================================================================
 */

/**
 * @brief 获取当前状态
 */
AtState aqua_at_get_state(const AtClient *client);

/**
 * @brief 获取命令响应行（仅在 DONE 状态有效）
 */
const AtLine *aqua_at_get_response(const AtClient *client);

/**
 * @brief 重置为 IDLE 状态（命令处理完成后调用）
 */
void aqua_at_reset(AtClient *client);

/* ============================================================================
 * URC 队列
 * ============================================================================
 */

/**
 * @brief 检查是否有 URC 行可读
 */
bool aqua_at_has_urc(const AtClient *client);

/**
 * @brief 弹出一个 URC 行
 *
 * @param client AT 客户端上下文指针
 * @param out    [输出] URC 行缓冲区
 * @return AtError 错误码（AT_ERR_NO_LINE 表示无可用行）
 */
AtError aqua_at_pop_line(AtClient *client, AtLine *out);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_AT_H */
