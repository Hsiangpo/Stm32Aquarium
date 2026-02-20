#include "app_config.h"
#include "aquarium_ds18b20.h"
#include "aquarium_firmware.h"
#include "aquarium_oled.h"
#include "aquarium_sensors.h"
#include "aquarium_storage.h"
#include "board_pins.h"
#if __has_include("secrets_local.h")
#include "secrets_local.h"
#else
#include "secrets.h"
#endif
#include <stdio.h>
#include <string.h>

ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim3;
UART_HandleTypeDef huart2;

/* 固件编排器相关 */
static AtClient g_at;
static AquariumApp g_app;
static MqttClient g_mqtt;
static AquaFirmware g_fw;
static StorageContext g_storage;
static DS18B20Context g_ds18b20;
static OledContext g_oled;

#define AP_PASSWORD_LEN 8
static const char g_ap_ssid[] = "Aquarium_Setup";
static char g_ap_password[AP_PASSWORD_LEN + 1];
static uint32_t g_prng_state;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART2_UART_Init(void);

static void Error_Handler(void);

/* AT 命令写回调：通过 USART2 发送 */
static size_t at_write_cb(const uint8_t *data, size_t len) {
  HAL_UART_Transmit(&huart2, (uint8_t *)data, (uint16_t)len, 100);
  return len;
}

/* 时间获取回调 */
static uint32_t get_tick_ms(void) { return HAL_GetTick(); }

/* UART 接收缓冲区（单字节中断模式） */
static uint8_t g_uart_rx_byte;

/* UART RX RingBuffer：ISR 只收集字节，主循环中再喂给 AT 引擎，避免
 * ISR/主线程并发访问 AtClient */
#define UART_RX_RING_SIZE 2048
static uint8_t g_uart_rx_ring[UART_RX_RING_SIZE];
static volatile uint16_t g_uart_rx_head = 0;
static volatile uint16_t g_uart_rx_tail = 0;

static bool uart_rx_push(uint8_t b) {
  uint16_t next = (uint16_t)((g_uart_rx_head + 1U) % UART_RX_RING_SIZE);
  if (next == g_uart_rx_tail) {
    return false; /* full */
  }
  g_uart_rx_ring[g_uart_rx_head] = b;
  g_uart_rx_head = next;
  return true;
}

static bool uart_rx_pop(uint8_t *out) {
  if (g_uart_rx_tail == g_uart_rx_head) {
    return false; /* empty */
  }
  *out = g_uart_rx_ring[g_uart_rx_tail];
  g_uart_rx_tail = (uint16_t)((g_uart_rx_tail + 1U) % UART_RX_RING_SIZE);
  return true;
}

/* ========================================================================== */
/* Flash 持久化后端（DeviceConfig） */
/* ========================================================================== */

/* STM32F103RB Flash: 0x08000000 ~ 0x0801FFFF（128KB），最后 1KB 页从 0x0801FC00
 * 开始 */
#define STORAGE_FLASH_BASE 0x0801FC00U
#define STORAGE_FLASH_SIZE 1024U

static size_t stm32_storage_read(uint32_t offset, void *buf, size_t len) {
  if (!buf)
    return 0;
  if (offset + len > STORAGE_FLASH_SIZE)
    return 0;
  memcpy(buf, (const void *)(STORAGE_FLASH_BASE + offset), len);
  return len;
}

static bool stm32_storage_erase(void) {
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0;

  if (HAL_FLASH_Unlock() != HAL_OK)
    return false;

  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.PageAddress = STORAGE_FLASH_BASE;
  erase.NbPages = 1;

  HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &page_error);

  (void)HAL_FLASH_Lock();
  return st == HAL_OK;
}

static size_t stm32_storage_write(uint32_t offset, const void *buf,
                                  size_t len) {
  if (!buf)
    return 0;
  if (offset + len > STORAGE_FLASH_SIZE)
    return 0;
  if ((offset & 1U) != 0)
    return 0; /* Flash 半字写需要 2 字节对齐 */

  if (HAL_FLASH_Unlock() != HAL_OK)
    return 0;

  uint32_t address = STORAGE_FLASH_BASE + offset;
  const uint8_t *src = (const uint8_t *)buf;
  size_t written = 0;

  while (written < len) {
    uint16_t halfword = 0xFFFF;
    halfword = (uint16_t)src[written];
    if (written + 1 < len) {
      halfword |= (uint16_t)src[written + 1] << 8;
    }

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, halfword) !=
        HAL_OK) {
      break;
    }

    address += 2;
    written += (written + 1 < len) ? 2 : 1;
  }

  (void)HAL_FLASH_Lock();
  return written;
}

/* 读取指定 ADC 通道（单次转换模式） */
static uint16_t read_adc_channel(uint32_t channel) {
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Channel = channel;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 10);
  uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  return val;
}

static uint32_t prng_next(void) {
  uint32_t x = g_prng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_prng_state = x;
  return x;
}

static void prng_seed(uint32_t seed) {
  if (seed == 0) {
    seed = 0xA5A5A5A5u;
  }
  g_prng_state = seed;
}

static void generate_ap_password(char *out, size_t out_size) {
  if (!out || out_size < AP_PASSWORD_LEN + 1) {
    return;
  }
  uint32_t seed = HAL_GetTick();
  seed ^= ((uint32_t)read_adc_channel(ADC_PH_CHANNEL) << 16);
  seed ^= ((uint32_t)read_adc_channel(ADC_TDS_CHANNEL) << 8);
  seed ^= (uint32_t)read_adc_channel(ADC_TURBIDITY_CHANNEL);
  seed ^= (uint32_t)read_adc_channel(ADC_WATER_LEVEL_CH);
  prng_seed(seed);
  for (size_t i = 0; i < AP_PASSWORD_LEN; i++) {
    out[i] = (char)('0' + (prng_next() % 10));
  }
  out[AP_PASSWORD_LEN] = '\0';
}

static void print_ap_credentials_serial(const char *ssid,
                                        const char *password) {
  if (!ssid || !password) {
    return;
  }
  char msg[96];
  int len = snprintf(msg, sizeof(msg), "[AP] SSID=%s PWD=%s\r\n", ssid,
                     password);
  if (len <= 0) {
    return;
  }
  if (len > (int)sizeof(msg)) {
    len = (int)sizeof(msg);
  }
  HAL_UART_Transmit(&huart2, (uint8_t *)msg, (uint16_t)len, 100);
}

static void show_ap_credentials_oled(const char *ssid, const char *password) {
  if (!ssid || !password) {
    return;
  }
  oled_clear(&g_oled);
  oled_draw_string(&g_oled, 0, 0, "AP SSID");
  oled_draw_string(&g_oled, 0, 10, ssid);
  oled_draw_string(&g_oled, 0, 20, "AP PWD:");
  oled_draw_string(&g_oled, 42, 20, password);
  oled_render(&g_oled);
  HAL_Delay(1500);
}

/* 整数转字符串（简易实现，用于 OLED 显示） */
static void int_to_str(int val, char *buf, int buf_size) {
  if (buf_size < 2)
    return;
  int neg = val < 0;
  if (neg)
    val = -val;
  int i = 0;
  do {
    buf[i++] = '0' + (val % 10);
    val /= 10;
  } while (val && i < buf_size - 2);
  if (neg)
    buf[i++] = '-';
  buf[i] = '\0';
  for (int j = 0; j < i / 2; j++) {
    char t = buf[j];
    buf[j] = buf[i - 1 - j];
    buf[i - 1 - j] = t;
  }
}

/* 浮点转字符串（1位小数，避免 printf float） */
static void float1_to_str(float val, char *buf, int buf_size) {
  if (buf_size < 5) {
    buf[0] = '\0';
    return;
  }
  int neg = val < 0;
  if (neg)
    val = -val;
  int whole = (int)val;
  int frac = (int)((val - whole) * 10 + 0.5f) % 10;
  int i = 0;
  if (neg)
    buf[i++] = '-';
  char tmp[8];
  int_to_str(whole, tmp, 8);
  for (int j = 0; tmp[j] && i < buf_size - 3; j++)
    buf[i++] = tmp[j];
  buf[i++] = '.';
  buf[i++] = '0' + frac;
  buf[i] = '\0';
}

/* ========================================================================== */
/* OLED I2C 硬件回调                                                         */
/* ========================================================================== */

static bool oled_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len) {
  return HAL_I2C_Master_Transmit(&hi2c1, addr << 1, (uint8_t *)data, len,
                                 100) == HAL_OK;
}

static const OledHwOps g_oled_hw_ops = {.i2c_write = oled_i2c_write};

/* ========================================================================== */
/* DWT 微秒延时（精确时序，Cortex-M3）                                       */
/* ========================================================================== */

/**
 * @brief 初始化 DWT 周期计数器
 *
 * 必须在 SystemClock_Config() 之后调用一次
 */
static void dwt_delay_init(void) {
  /* 启用 DWT（Data Watchpoint and Trace）访问 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  /* 重置 CYCCNT */
  DWT->CYCCNT = 0;
  /* 启用 CYCCNT 计数器 */
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* ========================================================================== */
/* DS18B20 硬件回调（1-Wire on PA8）                                         */
/* ========================================================================== */

static void ds_set_pin_output(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = PIN_DS18B20_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD; /* 开漏模式，1-Wire 总线规范 */
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(PIN_DS18B20_GPIO, &GPIO_InitStruct);
}

static void ds_set_pin_input(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = PIN_DS18B20_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PIN_DS18B20_GPIO, &GPIO_InitStruct);
}

static void ds_write_pin(bool level) {
  HAL_GPIO_WritePin(PIN_DS18B20_GPIO, PIN_DS18B20_PIN,
                    level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool ds_read_pin(void) {
  return HAL_GPIO_ReadPin(PIN_DS18B20_GPIO, PIN_DS18B20_PIN) == GPIO_PIN_SET;
}

/**
 * @brief 使用 DWT 周期计数器实现精确微秒延时
 *
 * 注意：需要先调用 dwt_delay_init() 初始化 DWT
 */
static void ds_delay_us(uint32_t us) {
  uint32_t clk_freq = HAL_RCC_GetHCLKFreq();  /* 获取 HCLK 频率（如 72MHz） */
  uint32_t ticks = (clk_freq / 1000000) * us; /* 计算需要的周期数 */
  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < ticks) {
    /* 忙等待 */
  }
}

static uint32_t ds_get_tick_ms(void) { return HAL_GetTick(); }

static const DS18B20HwOps g_ds_hw_ops = {
    .set_pin_output = ds_set_pin_output,
    .set_pin_input = ds_set_pin_input,
    .write_pin = ds_write_pin,
    .read_pin = ds_read_pin,
    .delay_us = ds_delay_us,
    .get_tick_ms = ds_get_tick_ms,
};

/* 执行器控制回调：映射到 GPIO */
static void actuator_callback(const ActuatorDesired *act, void *user_data) {
  (void)user_data;
  HAL_GPIO_WritePin(PIN_RELAY_HEATER_GPIO, PIN_RELAY_HEATER_PIN,
                    act->heater ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PIN_RELAY_PUMP_IN_GPIO, PIN_RELAY_PUMP_IN_PIN,
                    act->pump_in ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PIN_RELAY_PUMP_OUT_GPIO, PIN_RELAY_PUMP_OUT_PIN,
                    act->pump_out ? GPIO_PIN_SET : GPIO_PIN_RESET);
  /* 蜂鸣器：告警时开启（受 alarm_muted 影响，已由 logic 层处理） */
  HAL_GPIO_WritePin(PIN_BUZZER_GPIO, PIN_BUZZER_PIN,
                    act->buzzer ? GPIO_PIN_SET : GPIO_PIN_RESET);
  /* LED：告警时亮起（优先于心跳闪烁） */
  HAL_GPIO_WritePin(PIN_LED_GPIO, PIN_LED_PIN,
                    act->led ? GPIO_PIN_SET : GPIO_PIN_RESET);
  /* 喂食舵机：根据 feeding_in_progress 控制 PWM 脉宽 */
  const AquariumState *st = aqua_app_get_state(&g_app);
  if (st->props.feeding_in_progress) {
    /* 投喂进行中：根据 feed_amount 映射到舵机角度 */
    /* feed_amount 档位 1~5，映射到脉宽 1000~2000us（0~180度） */
    int32_t amount = st->thresholds.feed_amount;
    if (amount < 1)
      amount = 1;
    if (amount > 5)
      amount = 5;
    uint32_t pulse = 1000 + (uint32_t)(amount - 1) * 250; /* 1:1000, 5:2000 */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, pulse);
  } else {
    /* 非投喂：舵机回到中位（1500us） */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 1500);
  }
}

int main(void) {
  HAL_Init();
  SystemClock_Config();

  /* 初始化 DWT 微秒延时（必须在 SystemClock_Config 后调用） */
  dwt_delay_init();

  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();

  // Start PWM for servo (TIM3 CH2 -> PC7 when full remap enabled)
  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2) != HAL_OK) {
    Error_Handler();
  }

  /* 初始化 AT 引擎 */
  aqua_at_init(&g_at, at_write_cb, get_tick_ms);

  /* 初始化应用层 */
  aqua_app_init(&g_app, IOTDA_DEVICE_ID);

  /* 初始化 MQTT 客户端 */
  aqua_mqtt_init(&g_mqtt, &g_at, &g_app);
  generate_ap_password(g_ap_password, sizeof(g_ap_password));
  aqua_mqtt_set_ap_credentials(&g_mqtt, g_ap_ssid, g_ap_password);
  print_ap_credentials_serial(g_ap_ssid, g_ap_password);

  /* 配置 MQTT 连接参数 */
  MqttConfig mqtt_cfg = {0};
  strncpy(mqtt_cfg.wifi_ssid, WIFI_SSID, sizeof(mqtt_cfg.wifi_ssid) - 1);
  strncpy(mqtt_cfg.wifi_password, WIFI_PASSWORD,
          sizeof(mqtt_cfg.wifi_password) - 1);
  strncpy(mqtt_cfg.broker_host, IOTDA_HOST, sizeof(mqtt_cfg.broker_host) - 1);
  mqtt_cfg.broker_port = IOTDA_PORT;
  strncpy(mqtt_cfg.device_id, IOTDA_DEVICE_ID, sizeof(mqtt_cfg.device_id) - 1);
  strncpy(mqtt_cfg.device_secret, IOTDA_SECRET,
          sizeof(mqtt_cfg.device_secret) - 1);

  /* 配置持久化：初始化 Flash 后端并尝试加载 */
  aqua_storage_init(&g_storage, stm32_storage_read, stm32_storage_write,
                    stm32_storage_erase);

  DeviceConfig stored_cfg = {0};
  if (aqua_storage_load(&g_storage, &stored_cfg) == STORAGE_OK) {
    /* 校准参数总是从 Flash 覆盖 */
    g_app.state.config.ph_offset = stored_cfg.ph_offset;
    g_app.state.config.tds_factor = stored_cfg.tds_factor;

    /* WiFi 仅在 Flash 中存在时覆盖（避免空 SSID 破坏默认联网） */
    if (stored_cfg.wifi_ssid[0] != '\0') {
      strncpy(mqtt_cfg.wifi_ssid, stored_cfg.wifi_ssid,
              sizeof(mqtt_cfg.wifi_ssid) - 1);
      strncpy(mqtt_cfg.wifi_password, stored_cfg.wifi_password,
              sizeof(mqtt_cfg.wifi_password) - 1);
      strncpy(g_app.state.config.wifi_ssid, stored_cfg.wifi_ssid,
              sizeof(g_app.state.config.wifi_ssid) - 1);
      strncpy(g_app.state.config.wifi_password, stored_cfg.wifi_password,
              sizeof(g_app.state.config.wifi_password) - 1);
    }
  }

  /* 让 app 内部 config 的 WiFi 与当前实际使用保持一致（便于后续持久化/展示） */
  if (g_app.state.config.wifi_ssid[0] == '\0') {
    strncpy(g_app.state.config.wifi_ssid, mqtt_cfg.wifi_ssid,
            sizeof(g_app.state.config.wifi_ssid) - 1);
  }
  if (g_app.state.config.wifi_password[0] == '\0') {
    strncpy(g_app.state.config.wifi_password, mqtt_cfg.wifi_password,
            sizeof(g_app.state.config.wifi_password) - 1);
  }

  aqua_mqtt_set_config(&g_mqtt, &mqtt_cfg);
  aqua_mqtt_set_timestamp(&g_mqtt, IOTDA_TIMESTAMP);

  /* 初始化固件编排器 */
  aqua_fw_init(&g_fw, &g_app, &g_mqtt);

  /* 注册执行器回调 */
  aqua_fw_set_actuator_callback(&g_fw, actuator_callback, NULL);

  /* 初始化 DS18B20 温度传感器（默认 25.0°C） */
  ds18b20_init(&g_ds18b20, 25.0f);

  /* 初始化 OLED 显示（I2C1, 地址 0x3C） */
  oled_init(&g_oled, &g_oled_hw_ops, OLED_I2C_ADDR_DEFAULT);
  show_ap_credentials_oled(g_ap_ssid, g_ap_password);

  /* 启动 UART 接收中断（单字节模式） */
  HAL_UART_Receive_IT(&huart2, &g_uart_rx_byte, 1);

  /* 启动 MQTT 连接 */
  aqua_mqtt_start(&g_mqtt);

  while (1) {
    /* 先处理 UART RX 缓冲，把数据喂给 AT 引擎（避免在 ISR 中直接操作 AtClient）
     */
    uint8_t b;
    while (uart_rx_pop(&b)) {
      aqua_at_feed_rx(&g_at, &b, 1);
    }

    /* 每秒采样 ADC 传感器并更新 */
    static uint32_t last_adc_ms = 0;
    uint32_t now_ms = HAL_GetTick();
    if (now_ms - last_adc_ms >= 1000) {
      last_adc_ms = now_ms;
      /* 读取 4 个 ADC 通道 */
      uint16_t adc_ph = read_adc_channel(ADC_PH_CHANNEL);
      uint16_t adc_tds = read_adc_channel(ADC_TDS_CHANNEL);
      uint16_t adc_turb = read_adc_channel(ADC_TURBIDITY_CHANNEL);
      uint16_t adc_level = read_adc_channel(ADC_WATER_LEVEL_CH);
      /* 转换为电压再换算为物理量 */
      float v_ph = aqua_sensor_adc_to_voltage(adc_ph);
      float v_tds = aqua_sensor_adc_to_voltage(adc_tds);
      float v_turb = aqua_sensor_adc_to_voltage(adc_turb);
      float v_level = aqua_sensor_adc_to_voltage(adc_level);
      float ph = aqua_sensor_ph_from_voltage(v_ph);
      float tds = aqua_sensor_tds_from_voltage(v_tds);
      float turb = aqua_sensor_turbidity_from_voltage(v_turb);
      float level = aqua_sensor_water_level_from_voltage(v_level);
      /* 获取 DS18B20 温度（使用缓存值，若从未成功则返回默认值） */
      float temp = ds18b20_get_temperature(&g_ds18b20);
      /* 更新固件状态 */
      aqua_fw_update_sensors(&g_fw, temp, ph, tds, turb, level);
    }

    /* DS18B20 非阻塞状态机：启动转换 / 检查完成 / 读取结果 */
    DS18B20State ds_state = ds18b20_get_state(&g_ds18b20);
    if (ds_state == DS18B20_STATE_IDLE || ds_state == DS18B20_STATE_ERROR) {
      /* 空闲或出错时尝试启动新的转换 */
      ds18b20_start_conversion(&g_ds18b20, &g_ds_hw_ops);
    } else if (ds_state == DS18B20_STATE_CONVERTING) {
      /* 检查转换是否完成（750ms后） */
      if (ds18b20_is_conversion_done(&g_ds18b20, &g_ds_hw_ops)) {
        /* 转换完成，读取温度 */
        float new_temp;
        if (ds18b20_read_temperature(&g_ds18b20, &g_ds_hw_ops, &new_temp)) {
          /* 读取成功，温度已缓存到 g_ds18b20.last_temp */
        }
        /* 失败时保持上次有效值或默认值，状态机会在下轮重试 */
      }
    }

    /* 推进固件状态机 */
    aqua_fw_step(&g_fw, HAL_GetTick());

    /* 配置变更：落盘（Flash） */
    if (g_app.state.config_dirty) {
      if (aqua_storage_save(&g_storage, &g_app.state.config) == STORAGE_OK) {
        g_app.state.config_dirty = false;
      }
    }

    /* LED 心跳（仅在无告警时闪烁，告警时由 actuator_callback 控制） */
    static uint32_t last_led_ms = 0;
    if (g_app.state.props.alarm_level == 0) {
      if (HAL_GetTick() - last_led_ms >= 1000) {
        HAL_GPIO_TogglePin(PIN_LED_GPIO, PIN_LED_PIN);
        last_led_ms = HAL_GetTick();
      }
    }

    /* OLED 页面轮播（每 2s 切换，每 500ms 刷新） */
    static uint32_t last_oled_ms = 0;
    static uint32_t last_page_ms = 0;
    static uint8_t oled_page = 0;
    if (HAL_GetTick() - last_page_ms >= 2000) {
      last_page_ms = HAL_GetTick();
      oled_page = (oled_page + 1) % 3;
    }
    if (HAL_GetTick() - last_oled_ms >= 500) {
      last_oled_ms = HAL_GetTick();
      oled_clear(&g_oled);
      char buf[12];
      const AquariumState *st = aqua_app_get_state(&g_app);
      if (oled_page == 0) {
        /* 第1行: T=25.1 PH=7.2 */
        float1_to_str(st->props.temperature, buf, 12);
        oled_draw_string(&g_oled, 0, 0, "T=");
        oled_draw_string(&g_oled, 12, 0, buf);
        float1_to_str(st->props.ph, buf, 12);
        oled_draw_string(&g_oled, 48, 0, "PH=");
        oled_draw_string(&g_oled, 66, 0, buf);
        /* 第2行: TDS=500 TURB=80 */
        int_to_str((int)st->props.tds, buf, 12);
        oled_draw_string(&g_oled, 0, 10, "TDS=");
        oled_draw_string(&g_oled, 24, 10, buf);
        int_to_str((int)st->props.turbidity, buf, 12);
        oled_draw_string(&g_oled, 60, 10, "TB=");
        oled_draw_string(&g_oled, 78, 10, buf);
        /* 第3行: LVL=85% */
        int_to_str((int)st->props.water_level, buf, 12);
        oled_draw_string(&g_oled, 0, 20, "LVL=");
        oled_draw_string(&g_oled, 24, 20, buf);
        oled_draw_string(&g_oled, 48, 20, "%");
      } else if (oled_page == 1) {
        /* 第1行: AUTO/MANUAL HTR:ON */
        oled_draw_string(&g_oled, 0, 0, st->props.auto_mode ? "AUTO" : "MANU");
        oled_draw_string(&g_oled, 30, 0, st->props.heater ? "HTR:1" : "HTR:0");
        /* 第2行: PUMP_IN PUMP_OUT */
        oled_draw_string(&g_oled, 0, 10, st->props.pump_in ? "PI:1" : "PI:0");
        oled_draw_string(&g_oled, 30, 10, st->props.pump_out ? "PO:1" : "PO:0");
        oled_draw_string(&g_oled, 60, 10,
                         st->props.feeding_in_progress ? "FEED" : "");
        /* 第3行: FEED CD */
        int_to_str(st->props.feed_countdown, buf, 12);
        oled_draw_string(&g_oled, 0, 20, "FCD=");
        oled_draw_string(&g_oled, 24, 20, buf);
      } else {
        /* 第1行: NET=ONLINE */
        int ns = aqua_mqtt_get_net_status(&g_mqtt);
        const char *nstr = ns == 2   ? "ONLINE"
                           : ns == 1 ? "CONN"
                           : ns == 3 ? "AP"
                                     : "OFF";
        oled_draw_string(&g_oled, 0, 0, "NET=");
        oled_draw_string(&g_oled, 24, 0, nstr);
        /* 第2行: ALM=2 MUTE=0 */
        int_to_str(st->props.alarm_level, buf, 12);
        oled_draw_string(&g_oled, 0, 10, "ALM=");
        oled_draw_string(&g_oled, 24, 10, buf);
        oled_draw_string(&g_oled, 60, 10, st->props.alarm_muted ? "MUTE" : "");
        /* 第3行: 留空或显示版本 */
        oled_draw_string(&g_oled, 0, 20, "AQUARIUM V1");
      }
      oled_render(&g_oled);
    }

    HAL_Delay(10); /* 10ms 循环周期 */
  }
}

static void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  // HSE (8MHz) -> PLL x9 = 72MHz
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // Free PB3/PB4/PA15 by disabling JTAG (keep SWD for debugging).
  __HAL_AFIO_REMAP_SWJ_NOJTAG();

  // I2C1 remap to PB8/PB9 (NUCLEO Arduino D15/D14).
#ifdef __HAL_AFIO_REMAP_I2C1_ENABLE
  __HAL_AFIO_REMAP_I2C1_ENABLE();
#else
  // Fallback: set AFIO MAPR I2C1_REMAP bit (typical position = 1)
  AFIO->MAPR |= (1U << 1);
#endif

  // TIM3 full remap to PC6/7/8/9 so that CH2 outputs on PC7 (Arduino D9).
  // Fallback uses the common definition TIM3_REMAP bits at AFIO->MAPR[11:10] =
  // 0b11.
#if defined(AFIO_MAPR_TIM3_REMAP)
  MODIFY_REG(AFIO->MAPR, AFIO_MAPR_TIM3_REMAP, (0x3U << 10));
#else
  AFIO->MAPR = (AFIO->MAPR & ~(0x3U << 10)) | (0x3U << 10);
#endif

  // ADC analog inputs
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;

  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // I2C1 pins (PB8/PB9, remapped): alternate function open-drain
  GPIO_InitStruct.Pin = PIN_OLED_I2C_SCL_PIN | PIN_OLED_I2C_SDA_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // USART2 pins (PA2/PA3): TX AF push-pull, RX input
  GPIO_InitStruct.Pin = PIN_ESP32_TX_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PIN_ESP32_RX_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // Outputs: relays, buzzer, LED
  HAL_GPIO_WritePin(PIN_LED_GPIO, PIN_LED_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PIN_RELAY_PUMP_IN_GPIO, PIN_RELAY_PUMP_IN_PIN,
                    GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PIN_RELAY_PUMP_OUT_GPIO, PIN_RELAY_PUMP_OUT_PIN,
                    GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PIN_RELAY_HEATER_GPIO, PIN_RELAY_HEATER_PIN,
                    GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PIN_BUZZER_GPIO, PIN_BUZZER_PIN, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = PIN_LED_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PIN_LED_GPIO, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PIN_RELAY_PUMP_IN_PIN;
  HAL_GPIO_Init(PIN_RELAY_PUMP_IN_GPIO, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PIN_RELAY_PUMP_OUT_PIN;
  HAL_GPIO_Init(PIN_RELAY_PUMP_OUT_GPIO, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PIN_RELAY_HEATER_PIN;
  HAL_GPIO_Init(PIN_RELAY_HEATER_GPIO, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PIN_BUZZER_PIN;
  HAL_GPIO_Init(PIN_BUZZER_GPIO, &GPIO_InitStruct);

  // Optional: ESP32 reset pin
  GPIO_InitStruct.Pin = PIN_ESP32_RST_PIN;
  HAL_GPIO_Init(PIN_ESP32_RST_GPIO, &GPIO_InitStruct);

  // DS18B20 pin defaults to input (external pull-up).
  GPIO_InitStruct.Pin = PIN_DS18B20_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PIN_DS18B20_GPIO, &GPIO_InitStruct);
}

static void MX_ADC1_Init(void) {
  ADC_ChannelConfTypeDef sConfig = {0};

  __HAL_RCC_ADC1_CLK_ENABLE();

  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) {
    Error_Handler();
  }

  // ADC calibration (recommended on F1).
#ifdef HAL_ADCEx_Calibration_Start
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) {
    Error_Handler();
  }
#endif

  // Default channel (A0 / pH). Other channels can be selected at runtime via
  // HAL_ADC_ConfigChannel before each read.
  sConfig.Channel = ADC_PH_CHANNEL;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }
}

static void MX_I2C1_Init(void) {
  __HAL_RCC_I2C1_CLK_ENABLE();

  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
    Error_Handler();
  }
}

static void MX_TIM3_Init(void) {
  TIM_OC_InitTypeDef sConfigOC = {0};

  __HAL_RCC_TIM3_CLK_ENABLE();

  // Timer tick = 1MHz (72MHz / (71+1))
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 71;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 20000 - 1; // 20ms -> 50Hz
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) {
    Error_Handler();
  }

  // CH2 pulse default = 1500us (neutral)
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 1500;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK) {
    Error_Handler();
  }

  // Configure PC7 as alternate function push-pull for TIM3_CH2 output.
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = PIN_SERVO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(PIN_SERVO_GPIO, &GPIO_InitStruct);
}

static void MX_USART2_UART_Init(void) {
  __HAL_RCC_USART2_CLK_ENABLE();

  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) {
    Error_Handler();
  }
}

static void Error_Handler(void) {
  while (1) {
    HAL_GPIO_TogglePin(PIN_LED_GPIO, PIN_LED_PIN);
    HAL_Delay(100);
  }
}

/* UART 接收完成回调（中断触发） */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART2) {
    /* ISR 中仅入队字节，避免与主循环并发访问 AtClient */
    (void)uart_rx_push(g_uart_rx_byte);
    /* 继续开启接收下一个字节 */
    HAL_UART_Receive_IT(&huart2, &g_uart_rx_byte, 1);
  }
}
