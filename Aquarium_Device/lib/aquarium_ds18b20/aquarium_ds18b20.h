/**
 * @file aquarium_ds18b20.h
 * @brief DS18B20 温度传感器驱动（1-Wire 协议）
 *
 * 特性：
 * - 非阻塞采样状态机（不使用 HAL_Delay 阻塞主循环）
 * - CRC8 校验 scratchpad 数据
 * - raw_to_celsius 纯函数（可单测）
 * - 支持失败兜底策略
 */

#ifndef AQUARIUM_DS18B20_H
#define AQUARIUM_DS18B20_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * DS18B20 命令定义
 * ============================================================================
 */

#define DS18B20_CMD_SKIP_ROM 0xCC     /* 跳过 ROM（单设备总线） */
#define DS18B20_CMD_CONVERT_T 0x44    /* 启动温度转换 */
#define DS18B20_CMD_READ_SCRATCH 0xBE /* 读取 Scratchpad */

/* 转换等待时间：12位精度需要 750ms */
#define DS18B20_CONVERT_TIME_MS 750

/* ============================================================================
 * 采样状态机状态
 * ============================================================================
 */

typedef enum {
  DS18B20_STATE_IDLE = 0,   /* 空闲，等待启动 */
  DS18B20_STATE_CONVERTING, /* 正在转换中，等待 750ms */
  DS18B20_STATE_READY,      /* 转换完成，可读取 */
  DS18B20_STATE_ERROR       /* 出错（无设备/CRC 错误） */
} DS18B20State;

/* ============================================================================
 * DS18B20 上下文结构
 * ============================================================================
 */

typedef struct {
  DS18B20State state;        /* 当前状态 */
  uint32_t convert_start_ms; /* 转换开始时间 */
  float last_temp;           /* 上次有效温度（失败时兜底） */
  bool has_valid_temp;       /* 是否有有效温度 */
} DS18B20Context;

/* ============================================================================
 * 1-Wire 底层回调（硬件抽象）
 * ============================================================================
 */

/**
 * @brief 1-Wire 硬件操作回调结构
 *
 * 这些回调由 main.c 实现，将 GPIO 操作注入到驱动中
 */
typedef struct {
  void (*set_pin_output)(void);  /* 设置引脚为输出模式 */
  void (*set_pin_input)(void);   /* 设置引脚为输入模式 */
  void (*write_pin)(bool level); /* 写引脚电平 */
  bool (*read_pin)(void);        /* 读引脚电平 */
  void (*delay_us)(uint32_t us); /* 微秒延时 */
  uint32_t (*get_tick_ms)(void); /* 获取毫秒时间戳 */
} DS18B20HwOps;

/* ============================================================================
 * 纯函数（可单测，不依赖硬件）
 * ============================================================================
 */

/**
 * @brief 计算 CRC8（1-Wire CRC，多项式 0x31）
 *
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return CRC8 值
 */
uint8_t ds18b20_crc8(const uint8_t *data, uint8_t len);

/**
 * @brief 将 DS18B20 原始值转换为摄氏度
 *
 * @param raw_lsb 温度 LSB
 * @param raw_msb 温度 MSB
 * @return 温度值（摄氏度），支持负温和小数
 */
float ds18b20_raw_to_celsius(uint8_t raw_lsb, uint8_t raw_msb);

/* ============================================================================
 * 驱动初始化
 * ============================================================================
 */

/**
 * @brief 初始化 DS18B20 上下文
 *
 * @param ctx 上下文指针
 * @param default_temp 默认温度（设备未就绪时使用）
 */
void ds18b20_init(DS18B20Context *ctx, float default_temp);

/* ============================================================================
 * 非阻塞采样状态机
 * ============================================================================
 */

/**
 * @brief 启动温度转换
 *
 * 发送 SKIP_ROM + CONVERT_T 命令，进入 CONVERTING 状态
 *
 * @param ctx 上下文指针
 * @param hw 硬件操作回调
 * @return true=成功启动，false=设备无响应
 */
bool ds18b20_start_conversion(DS18B20Context *ctx, const DS18B20HwOps *hw);

/**
 * @brief 检查转换是否完成
 *
 * @param ctx 上下文指针
 * @param hw 硬件操作回调
 * @return true=转换完成，可调用 read_temperature
 */
bool ds18b20_is_conversion_done(DS18B20Context *ctx, const DS18B20HwOps *hw);

/**
 * @brief 读取温度值
 *
 * 发送 READ_SCRATCHPAD 命令，读取 9 字节并校验 CRC
 *
 * @param ctx 上下文指针
 * @param hw 硬件操作回调
 * @param out_temp [输出] 温度值
 * @return true=读取成功，false=CRC 错误或设备无响应
 */
bool ds18b20_read_temperature(DS18B20Context *ctx, const DS18B20HwOps *hw,
                              float *out_temp);

/**
 * @brief 获取当前温度（带兜底）
 *
 * 返回上次有效温度，若从未成功读取则返回默认值
 *
 * @param ctx 上下文指针
 * @return 温度值（摄氏度）
 */
float ds18b20_get_temperature(const DS18B20Context *ctx);

/**
 * @brief 获取当前状态
 */
DS18B20State ds18b20_get_state(const DS18B20Context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_DS18B20_H */
