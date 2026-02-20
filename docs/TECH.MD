# 技术实现文档（TECH）

本文档描述本项目的开发环境、工程结构与关键实现约定，用于开工前统一技术口径。

---

## 1. STM32 开发环境（VSCode + PlatformIO）

目标板卡：**STM32 NUCLEO-F103RB（STM32F103RB，F1 系列）**

### 1.1 必备软件

- VSCode
- VSCode 扩展：`PlatformIO IDE`
- ST-LINK 驱动/烧录工具（推荐安装其一）：
  - `STM32CubeProgrammer`（会安装 ST-LINK 相关驱动）
  - 或已安装 `STM32CubeIDE`（通常也会带齐工具链/驱动）

### 1.2 建议安装的 VSCode 扩展

- `C/C++`（代码补全/跳转）
- `Cortex-Debug`（调试体验更好；PlatformIO 也会用到）

### 1.3 打开与编译

1. 用 VSCode 打开 `Aquarium_Device/`（该目录内包含 `platformio.ini`）
2. 等 PlatformIO 初始化完成后：
   - 编译：`pio run`
   - 下载/烧录：`pio run -t upload`
   - 调试：`pio debug`

> 也可以完全不装命令行，只用 VSCode 左侧的 PlatformIO 面板执行 Build/Upload/Debug。

### 1.4 重要硬件注意事项

- `PB4` 默认是 JTAG 的 `TRST` 引脚；**不禁用 JTAG 会导致继电器 IN2（PB4）不可用**。
- 固件已在 `main.c` 中通过 `__HAL_AFIO_REMAP_SWJ_NOJTAG()` 禁用 JTAG（保留 SWD 调试）。
- ESP32 AT 使用 `USART2 (PA2/PA3)`，需断开 ST-LINK VCP 连接避免总线冲突。

---

## 2. 代码工程结构（PlatformIO）

当前 STM32 工程位于 `Aquarium_Device/`：

| 文件/目录                 | 说明                   |
| ------------------------- | ---------------------- |
| `platformio.ini`          | PlatformIO 工程配置    |
| `include/board_pins.h`    | 引脚映射               |
| `include/secrets.h`       | 固定演示配置（WiFi/IoTDA） |
| `src/main.c`              | 固件入口               |
| `lib/`                    | 分层库                 |
| `test/`                   | 单元测试               |

---

## 3. 引脚映射

详细见 `docs/Wiring_Guide.md`。关键点：

| 功能                    | 引脚               |
| ----------------------- | ------------------ |
| ADC（pH/TDS/浊度/水位） | PA0/PA1/PA4/PB0    |
| I2C OLED                | PB8(SCL), PB9(SDA) |
| 舵机 PWM                | PC7 (TIM3_CH2)     |
| 继电器                  | PB5/PB4/PB10       |
| DS18B20 温度            | PA8                |
| ESP32 AT                | PA2(TX), PA3(RX)   |
| 蜂鸣器/LED              | PA9/PA5            |

---

## 4. SNTP 时间同步

WiFi 连接成功后自动通过 SNTP 获取网络时间，用于 IoTDA MQTT 鉴权。

1. 配置 SNTP：`AT+CIPSNTPCFG=1,0,"ntp.aliyun.com"`
2. 查询时间：`AT+CIPSNTPTIME?`
3. 转换为 `YYYYMMDDHH` 格式用于签名

---

## 5. 配置持久化（Flash）

- 存储位置：Flash 最后 1 页 (0x0801FC00)
- 格式：Magic + Version + DeviceConfig + CRC32
- 擦写策略：写前擦除整页，仅 `config_dirty=true` 时触发

---

## 6. AP 配网 + 自动重连

- WiFi 连接失败 ≥3 次时，自动进入 AP 配网模式
- AP SSID: `Aquarium_Setup`，密码随机生成（显示在 OLED/串口）
- HTTP 端点：`GET /config?ssid=XXX&pwd=YYY`

---

## 7. ADC 换算

- ADC：12位，参考电压 3.3V
- pH：`pH = -5.70 × voltage + 21.34`
- TDS：`TDS = voltage × 500`
- 浊度：`turbidity = 3000 × (1 - voltage / 4.0)`
- 水位：`level = (voltage - 0.5) / 2.5 × 100`

---

## 8. DS18B20 温度驱动

- 引脚：PA8，外部 4.7kΩ 上拉
- 精度：12 位（0.0625℃），转换时间 750ms
- 非阻塞状态机：IDLE → CONVERTING → READY → IDLE
- 使用 DWT 周期计数器实现精确 μs 延时

---

## 9. OLED 显示

- 芯片：SSD1306，128x64，I2C 地址 0x3C
- 刷新：500ms，页面切换 2s 轮播
- 页面0：温度/pH/TDS/浊度/水位
- 页面1：自动模式/执行器状态/投喂倒计时
- 页面2：网络状态/告警级别/版本

---

## 10. 应用侧实现（HarmonyOS App）

> 适用版本：DevEco Studio 5.0.5（API 17）

- 工程目录：`Aquarium_APP/`
- 权限：`ohos.permission.INTERNET`
- REST API：查询影子、下发命令（同步/异步）
- 签名：AK/SK + SDK-HMAC-SHA256（课设演示版采用固定内置参数）
- 云端参数：固定内置于 `entry/src/main/ets/config/iotdaDefaults.ts`
- 前端约定：不提供华为云参数配置入口（仅保留业务控制与阈值配置）
- 端口约定：默认 MQTT `1883`；若改为 `8883` 需同步启用 TLS 证书配置

### 主要功能

- 仪表盘：实时展示 13 个属性
- 控制：加热/泵/投喂/静音/自动模式
- 阈值配置：温度/pH/TDS/浊度/水位阈值
- 历史趋势：折线图展示

---

_文档版本：v2.1 | 更新日期：2026-02-20_
