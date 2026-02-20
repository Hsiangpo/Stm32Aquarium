/**
 * @file aquarium_ds18b20.c
 * @brief DS18B20 温度传感器驱动实现
 */

#include "aquarium_ds18b20.h"

/* ============================================================================
 * 1-Wire 底层时序（通过回调实现）
 * ============================================================================
 */

/**
 * @brief 1-Wire 复位并检测存在脉冲
 * @return true=设备存在，false=无设备
 */
static bool onewire_reset(const DS18B20HwOps *hw) {
  bool presence = false;

  hw->set_pin_output();
  hw->write_pin(false); /* 拉低 480us */
  hw->delay_us(480);

  hw->set_pin_input(); /* 释放总线 */
  hw->delay_us(70);    /* 等待 15-60us 后读取 */

  presence = !hw->read_pin(); /* 设备拉低表示存在 */
  hw->delay_us(410);          /* 等待复位结束 */

  return presence;
}

/**
 * @brief 写一个 bit
 */
static void onewire_write_bit(const DS18B20HwOps *hw, bool bit) {
  hw->set_pin_output();
  hw->write_pin(false);       /* 拉低开始 */
  hw->delay_us(bit ? 6 : 60); /* 写1: 短拉低，写0: 长拉低 */
  hw->write_pin(true);        /* 释放 */
  hw->delay_us(bit ? 64 : 10);
}

/**
 * @brief 读一个 bit
 */
static bool onewire_read_bit(const DS18B20HwOps *hw) {
  bool bit;

  hw->set_pin_output();
  hw->write_pin(false); /* 拉低 1us 启动读 */
  hw->delay_us(3);
  hw->set_pin_input(); /* 释放总线 */
  hw->delay_us(10);    /* 等待数据稳定 */
  bit = hw->read_pin();
  hw->delay_us(53); /* 等待时隙结束 */

  return bit;
}

/**
 * @brief 写一个字节
 */
static void onewire_write_byte(const DS18B20HwOps *hw, uint8_t byte) {
  for (int i = 0; i < 8; i++) {
    onewire_write_bit(hw, (byte >> i) & 0x01);
  }
}

/**
 * @brief 读一个字节
 */
static uint8_t onewire_read_byte(const DS18B20HwOps *hw) {
  uint8_t byte = 0;
  for (int i = 0; i < 8; i++) {
    if (onewire_read_bit(hw)) {
      byte |= (1 << i);
    }
  }
  return byte;
}

/* ============================================================================
 * 纯函数实现
 * ============================================================================
 */

uint8_t ds18b20_crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;

  for (uint8_t i = 0; i < len; i++) {
    uint8_t inbyte = data[i];
    for (uint8_t j = 0; j < 8; j++) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) {
        crc ^= 0x8C; /* 多项式 x^8 + x^5 + x^4 + 1 的反转 */
      }
      inbyte >>= 1;
    }
  }

  return crc;
}

float ds18b20_raw_to_celsius(uint8_t raw_lsb, uint8_t raw_msb) {
  /*
   * DS18B20 温度格式（12位精度）：
   * - MSB: SSSSS MMM（S=符号位，M=整数高位）
   * - LSB: LLLL FFFF（L=整数低位，F=小数）
   *
   * 16位有符号整数，单位 0.0625°C
   */
  int16_t raw = (int16_t)((raw_msb << 8) | raw_lsb);
  return (float)raw * 0.0625f;
}

/* ============================================================================
 * 驱动初始化
 * ============================================================================
 */

void ds18b20_init(DS18B20Context *ctx, float default_temp) {
  ctx->state = DS18B20_STATE_IDLE;
  ctx->convert_start_ms = 0;
  ctx->last_temp = default_temp;
  ctx->has_valid_temp = false;
}

/* ============================================================================
 * 非阻塞采样状态机
 * ============================================================================
 */

bool ds18b20_start_conversion(DS18B20Context *ctx, const DS18B20HwOps *hw) {
  /* 复位总线 */
  if (!onewire_reset(hw)) {
    ctx->state = DS18B20_STATE_ERROR;
    return false;
  }

  /* 发送 SKIP_ROM + CONVERT_T */
  onewire_write_byte(hw, DS18B20_CMD_SKIP_ROM);
  onewire_write_byte(hw, DS18B20_CMD_CONVERT_T);

  /* 记录开始时间 */
  ctx->convert_start_ms = hw->get_tick_ms();
  ctx->state = DS18B20_STATE_CONVERTING;

  return true;
}

bool ds18b20_is_conversion_done(DS18B20Context *ctx, const DS18B20HwOps *hw) {
  if (ctx->state != DS18B20_STATE_CONVERTING) {
    return false;
  }

  uint32_t now = hw->get_tick_ms();
  uint32_t elapsed = now - ctx->convert_start_ms;

  if (elapsed >= DS18B20_CONVERT_TIME_MS) {
    ctx->state = DS18B20_STATE_READY;
    return true;
  }

  return false;
}

bool ds18b20_read_temperature(DS18B20Context *ctx, const DS18B20HwOps *hw,
                              float *out_temp) {
  uint8_t scratchpad[9];

  /* 复位总线 */
  if (!onewire_reset(hw)) {
    ctx->state = DS18B20_STATE_ERROR;
    return false;
  }

  /* 发送 SKIP_ROM + READ_SCRATCHPAD */
  onewire_write_byte(hw, DS18B20_CMD_SKIP_ROM);
  onewire_write_byte(hw, DS18B20_CMD_READ_SCRATCH);

  /* 读取 9 字节 scratchpad */
  for (int i = 0; i < 9; i++) {
    scratchpad[i] = onewire_read_byte(hw);
  }

  /* CRC 校验 */
  if (ds18b20_crc8(scratchpad, 8) != scratchpad[8]) {
    ctx->state = DS18B20_STATE_ERROR;
    return false;
  }

  /* 转换温度 */
  float temp = ds18b20_raw_to_celsius(scratchpad[0], scratchpad[1]);
  *out_temp = temp;

  /* 更新缓存 */
  ctx->last_temp = temp;
  ctx->has_valid_temp = true;
  ctx->state = DS18B20_STATE_IDLE;

  return true;
}

float ds18b20_get_temperature(const DS18B20Context *ctx) {
  return ctx->last_temp;
}

DS18B20State ds18b20_get_state(const DS18B20Context *ctx) { return ctx->state; }
