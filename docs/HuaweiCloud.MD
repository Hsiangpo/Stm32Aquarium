# 华为云 IoTDA 配置文档

本文档定义智能水族箱在华为云 IoT 设备接入服务（IoTDA）上的所有配置参数，供设备侧和应用侧开发对齐使用。

> **编码说明**：本文档使用 UTF-8 编码，请用支持 UTF-8 的编辑器打开。

---

## 1. 实例与接入信息

| 配置项   | 值                                     |
| -------- | -------------------------------------- |
| 区域     | `cn-north-4`（华北-北京四）            |
| 实例 ID  | `8145412f-8cb1-46a1-8755-d692c4dc1b65` |
| 实例名称 | IOT                                    |

### 1.1 设备接入地址（STM32/ESP32 使用）

| 协议  | 地址                                                       | 端口 |
| ----- | ---------------------------------------------------------- | ---- |
| MQTT  | `8cee850016.st1.iotda-device.cn-north-4.myhuaweicloud.com` | 1883 |
| MQTTS | `8cee850016.st1.iotda-device.cn-north-4.myhuaweicloud.com` | 8883 |
| HTTPS | `8cee850016.st1.iotda-device.cn-north-4.myhuaweicloud.com` | 443  |

### 1.2 应用接入地址（鸿蒙 App REST API 使用）

| 协议  | 地址                                                    | 端口 |
| ----- | ------------------------------------------------------- | ---- |
| HTTPS | `8cee850016.st1.iotda-app.cn-north-4.myhuaweicloud.com` | 443  |
| AMQPS | `8cee850016.st1.iotda-app.cn-north-4.myhuaweicloud.com` | 5671 |
| MQTTS | `8cee850016.st1.iotda-app.cn-north-4.myhuaweicloud.com` | 8883 |

---

## 2. 产品信息

| 配置项   | 值                         |
| -------- | -------------------------- |
| 产品名称 | 智能水族箱                 |
| 产品 ID  | `690237639798273cc4fd09cb` |
| 协议类型 | MQTT                       |
| 数据格式 | JSON                       |

---

## 3. 设备信息

| 配置项   | 值                                       |
| -------- | ---------------------------------------- |
| 设备 ID  | `690237639798273cc4fd09cb_MyAquarium_01` |
| 设备标识 | `MyAquarium_01`                          |
| 设备密钥 | `<YOUR_DEVICE_SECRET>`（敏感信息：不要提交到仓库） |

### 3.1 MQTT 鉴权参数

| 参数      | 格式/值                                 |
| --------- | --------------------------------------- |
| clientId  | `{device_id}_0_{sign_type}_{timestamp}` |
| username  | `{device_id}`                           |
| password  | HMACSHA256 签名（详见下方）             |
| keepalive | 120 秒（推荐）                          |
| MQTT 版本 | 3.1.1                                   |

**ClientId 格式说明**（4 段，以 `_` 分隔）：

- **device_id**：设备 ID（`690237639798273cc4fd09cb_MyAquarium_01`）
- **0**：固定值，表示设备 ID 类型
- **sign_type**：密码签名类型，`0`=不校验时间戳，`1`=校验时间戳
- **timestamp**：UTC 时间，格式 `YYYYMMDDHH`（如 `2025121320` 表示 2025 年 12 月 13 日 20 时）

**Password 签名算法**：

> 说明：华为云 IoTDA 文档中 `HMACSHA256(secret, timestamp)` 表示“以 `secret` 为消息内容、`timestamp` 为密钥”。如果你使用常见的 HMAC API（通常签名函数参数为 `key,msg`），等价写法为：`HMAC_SHA256(key=timestamp, msg=secret)`。
>
> 官方校验例（来自华为云文档）：`secret=12345678`, `timestamp=2025041401`，结果应为 `c75150e6cb841417396819e4d2ee4358a416344a03a083e3a8567074ddec820a`。

```
password = HMACSHA256(message=secret, key=timestamp)
```

- `secret`：设备密钥（敏感信息：不要提交到仓库）
- `timestamp`：UTC 时间戳，格式 `YYYYMMDDHH`（如 `2025121320`）

**示例**（假设当前 UTC 时间为 2025 年 12 月 13 日 12:00）：

```
clientId  = 690237639798273cc4fd09cb_MyAquarium_01_0_1_2025121312
username  = 690237639798273cc4fd09cb_MyAquarium_01
password  = HMACSHA256("<device_secret>", "2025121312")
```

### 3.2 TLS CA 证书

使用 TLS 连接时需下载 DigiCert Global Root CA：

- 下载地址：华为云控制台 → 设备接入 → 帮助文档 → 证书下载

---

## 4. 物模型定义

### 4.1 Aquarium 属性服务（服务 ID: `Aquarium`）

设备周期性上报的状态属性（13 个字段）：

| 属性名                | 数据类型 | 说明                     |
| --------------------- | -------- | ------------------------ |
| `temperature`         | decimal  | 水温 ℃                   |
| `ph`                  | decimal  | pH 值                    |
| `tds`                 | decimal  | TDS 值 ppm               |
| `turbidity`           | decimal  | 浊度 NTU                 |
| `water_level`         | decimal  | 水位 %                   |
| `heater`              | boolean  | 加热棒状态               |
| `pump_in`             | boolean  | 进水泵状态               |
| `pump_out`            | boolean  | 出水泵状态               |
| `auto_mode`           | boolean  | 自动模式                 |
| `feed_countdown`      | int      | 距离下一次投喂的倒计时（自动或一次性预约） |
| `feeding_in_progress` | boolean  | 设备是否正在执行投喂     |
| `alarm_level`         | int      | 当前的报警级别           |
| `alarm_muted`         | boolean  | 报警是否被静音           |

---

### 4.2 aquarium_control 命令服务

| 配置项   | 值                 |
| -------- | ------------------ |
| 服务 ID  | `aquarium_control` |
| 命令名称 | `control`          |

**下行参数（云 → 设备）**：

| 参数名        | 数据类型 | 说明              |
| ------------- | -------- | ----------------- |
| `heater`      | bool     | 开启/关闭加热棒   |
| `pump_in`     | bool     | 开启/关闭进水泵   |
| `pump_out`    | bool     | 开启/关闭排水泵   |
| `mute`        | bool     | 静音告警          |
| `auto_mode`   | bool     | 切换自动/手动模式 |
| `feed`        | bool     | 立即投喂一次      |
| `feed_once_delay` | int  | 一次性投喂倒计时（秒） |
| `target_temp` | float    | 目标温度 ℃        |

> 备注：`feed_once_delay` 建议范围 1~86400 秒（一次性预约投喂）。

---

### 4.3 aquarium_threshold 命令服务

| 配置项   | 值                   |
| -------- | -------------------- |
| 服务 ID  | `aquarium_threshold` |
| 命令名称 | `set_thresholds`     |

**下行参数（云 → 设备）**：

| 参数名               | 数据类型 | 说明                 |
| -------------------- | -------- | -------------------- |
| `temp_min`           | float    | 温度下限 ℃           |
| `temp_max`           | float    | 温度上限 ℃           |
| `ph_min`             | float    | pH 下限              |
| `ph_max`             | float    | pH 上限              |
| `tds_warn`           | int      | TDS 警告阈值 ppm     |
| `tds_critical`       | int      | TDS 严重阈值 ppm     |
| `turbidity_warn`     | int      | 浊度警告阈值 NTU     |
| `turbidity_critical` | int      | 浊度严重阈值 NTU     |
| `level_min`          | int      | 水位下限 %           |
| `level_max`          | int      | 水位上限 %           |
| `feed_interval`      | int      | 自动投喂间隔（小时） |
| `feed_amount`        | int      | 投喂量（档位）       |

---

### 4.4 aquariumConfig 命令服务

| 配置项   | 值               |
| -------- | ---------------- |
| 服务 ID  | `aquariumConfig` |
| 命令名称 | `set_config`     |

**下行参数（云 → 设备）**：

| 参数名          | 数据类型 | 说明          |
| --------------- | -------- | ------------- |
| `wifi_ssid`     | string   | Wi-Fi 名称    |
| `wifi_password` | string   | Wi-Fi 密码    |
| `ph_offset`     | decimal  | pH 校准偏移量 |
| `tds_factor`    | decimal  | TDS 校准系数  |

---

## 5. MQTT Topic 格式

### 5.1 属性上报（设备 → 云）

```
Topic: $oc/devices/{device_id}/sys/properties/report
```

**示例**：

```
$oc/devices/690237639798273cc4fd09cb_MyAquarium_01/sys/properties/report
```

### 5.2 命令下发（云 → 设备）

```
Topic: $oc/devices/{device_id}/sys/commands/request_id={request_id}
```

### 5.3 命令响应（设备 → 云）

```
Topic: $oc/devices/{device_id}/sys/commands/response/request_id={request_id}
```

---

## 6. 应用侧 API 凭证

> 说明：IoTDA REST API 使用华为云 AK/SK 签名认证（常被称为 V11），Authorization 算法标识为 `SDK-HMAC-SHA256`。签名示例见 `docs/Interface.MD` 的“AK/SK 签名”章节。

### 6.1 AK/SK 凭证

| 配置项          | 值                                         |
| --------------- | ------------------------------------------ |
| Access Key (AK) | `<YOUR_AK>`                                |
| Secret Key (SK) | `<YOUR_SK>`                                |
| Project ID      | `79d1f76b8f0e47fd934f3eb393bf2e7f`         |

> **安全提示**：生产环境中 AK/SK 应通过环境变量获取，禁止硬编码在代码中。

### 6.2 Base URL

```
https://8cee850016.st1.iotda-app.cn-north-4.myhuaweicloud.com
```

### 6.3 常用接口

| 接口         | 方法 | 路径                                                      |
| ------------ | ---- | --------------------------------------------------------- |
| 查询设备影子 | GET  | `/v5/iot/{project_id}/devices/{device_id}/shadow`         |
| 下发同步命令 | POST | `/v5/iot/{project_id}/devices/{device_id}/commands`       |
| 下发异步命令 | POST | `/v5/iot/{project_id}/devices/{device_id}/async-commands` |

---

_文档版本：v1.2 | 更新日期：2025-12-13_
