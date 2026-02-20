# 智能水族箱接口文档

本文档定义智能水族箱系统的所有接口，包括设备侧（STM32 + ESP32）与华为云的通信协议，以及应用侧（鸿蒙 App）与华为云的 REST API 接口。当前课设演示版采用固定内置云参数，前端不提供云端配置入口。

---

## 1. 设备侧接口（MQTT）

### 1.1 连接参数

| 参数      | 值                                                            |
| --------- | ------------------------------------------------------------- |
| Broker    | `8cee850016.st1.iotda-device.cn-north-4.myhuaweicloud.com`    |
| Port      | 默认 1883 (MQTT)，可扩展为 8883 (MQTTS)                       |
| Client ID | `{device_id}_0_{sign_type}_{YYYYMMDDHH}`                      |
| Username  | `690237639798273cc4fd09cb_MyAquarium_01`                      |
| Password  | `HMACSHA256(secret, timestamp)`（timestamp 为 UTC `YYYYMMDDHH`） |

> 说明：当前仓库设备端默认走 1883；若切换 8883，需要在 ESP32 AT 链路增加 TLS 连接配置。

> 说明：华为云 IoTDA 文档中 `HMACSHA256(secret, timestamp)` 表示“以 `secret` 为消息内容、`timestamp` 为密钥”。如果你使用常见的 HMAC API（通常签名函数参数为 `key,msg`），等价写法为：`HMAC_SHA256(key=timestamp, msg=secret)`。

**示例**（UTC 时间 2025-12-13 12:00）：

```
Client ID = 690237639798273cc4fd09cb_MyAquarium_01_0_1_2025121312
```

---

### 1.2 属性上报

**Topic**: `$oc/devices/690237639798273cc4fd09cb_MyAquarium_01/sys/properties/report`

**Payload**（13 个属性字段）:

```json
{
  "services": [
    {
      "service_id": "Aquarium",
      "properties": {
        "temperature": 26.5,
        "ph": 7.2,
        "tds": 350,
        "turbidity": 15,
        "water_level": 85,
        "heater": true,
        "pump_in": false,
        "pump_out": false,
        "auto_mode": true,
        "feed_countdown": 0,
        "feeding_in_progress": false,
        "alarm_level": 0,
        "alarm_muted": false
      }
    }
  ]
}
```

> 说明：`feed_countdown` 表示“距离下一次投喂”的倒计时（自动周期投喂或一次性预约投喂）。

**上报频率**: 建议 30 秒一次

---

### 1.3 命令接收

**Subscribe Topic**: `$oc/devices/{device_id}/sys/commands/#`

**命令请求 Topic**（云平台下发）：

```
$oc/devices/{device_id}/sys/commands/request_id={request_id}
```

设备收到的命令包含 `request_id`，用于响应时匹配。

#### 1.3.1 control 命令

```json
{
  "object_device_id": "690237639798273cc4fd09cb_MyAquarium_01",
  "service_id": "aquarium_control",
  "command_name": "control",
  "paras": {
    "heater": true,
    "pump_in": false,
    "pump_out": false,
    "mute": false,
    "auto_mode": true,
    "feed": false,
    "feed_once_delay": 600,
    "target_temp": 26.0
  }
}
```

> 说明：`feed_once_delay` 单位为秒，表示一次性投喂倒计时（建议范围 1~86400）。  
> 云端产品模型已于 2026-02-20 对齐：`feed:boolean`，并已配置 `feed_once_delay:int`。

#### 1.3.2 set_thresholds 命令

```json
{
  "object_device_id": "690237639798273cc4fd09cb_MyAquarium_01",
  "service_id": "aquarium_threshold",
  "command_name": "set_thresholds",
  "paras": {
    "temp_min": 24.0,
    "temp_max": 28.0,
    "ph_min": 6.5,
    "ph_max": 7.5,
    "tds_warn": 500,
    "tds_critical": 800,
    "turbidity_warn": 30,
    "turbidity_critical": 50,
    "level_min": 20,
    "level_max": 95,
    "feed_interval": 12,
    "feed_amount": 2
  }
}
```

#### 1.3.3 set_config 命令

```json
{
  "object_device_id": "690237639798273cc4fd09cb_MyAquarium_01",
  "service_id": "aquariumConfig",
  "command_name": "set_config",
  "paras": {
    "wifi_ssid": "MyWiFi",
    "wifi_password": "password123",
    "ph_offset": 0.15,
    "tds_factor": 1.02
  }
}
```

---

### 1.4 命令响应

**Publish Topic**: `$oc/devices/{device_id}/sys/commands/response/request_id={request_id}`

**成功响应**:

```json
{
  "result_code": 0,
  "response_name": "control_response",
  "paras": {
    "result": "success"
  }
}
```

**失败响应**:

```json
{
  "result_code": 1,
  "response_name": "control_response",
  "paras": {
    "result": "failed",
    "error": "heater malfunction"
  }
}
```

---

## 2. 应用侧接口（REST API）

### 2.1 公共参数

| 参数       | 值                                                              |
| ---------- | --------------------------------------------------------------- |
| Base URL   | `https://8cee850016.st1.iotda-app.cn-north-4.myhuaweicloud.com` |
| Project ID | `79d1f76b8f0e47fd934f3eb393bf2e7f`                              |
| Device ID  | `690237639798273cc4fd09cb_MyAquarium_01`                        |
| AK         | `HPUA4JX3YFP88FSKLZ8N`                                           |
| SK         | `vZ2ggGBLX6ekyeZWDxopQM9J5DXmMSbUI5JNfgDg`                       |
| 签名方式   | AK/SK 签名                                                      |

> 说明：演示版参数固定内置在 `Aquarium_APP/entry/src/main/ets/config/iotdaDefaults.ts`，设置页无云端参数输入项。

---

### 2.2 查询设备影子

获取设备最新状态。

**请求**:

```
GET /v5/iot/{project_id}/devices/{device_id}/shadow
```

**响应**（包含 13 个属性字段）:

```json
{
  "device_id": "690237639798273cc4fd09cb_MyAquarium_01",
  "shadow": [
    {
      "service_id": "Aquarium",
      "desired": null,
      "reported": {
        "properties": {
          "temperature": 26.5,
          "ph": 7.2,
          "tds": 350,
          "turbidity": 15,
          "water_level": 85,
          "heater": true,
          "pump_in": false,
          "pump_out": false,
          "auto_mode": true,
          "feed_countdown": 0,
          "feeding_in_progress": false,
          "alarm_level": 0,
          "alarm_muted": false
        },
        "event_time": "20251213T120000Z"
      },
      "version": 10
    }
  ]
}
```

---

### 2.3 下发同步命令

下发命令并等待设备响应。

> **注意**：同步命令平台默认超时时间约 **20 秒**，如设备未及时响应则返回超时错误。

**请求**:

```
POST /v5/iot/{project_id}/devices/{device_id}/commands
Content-Type: application/json
```

**请求体**:

```json
{
  "service_id": "aquarium_control",
  "command_name": "control",
  "paras": {
    "heater": true
  }
}
```

**成功响应**:

```json
{
  "command_id": "cmd-xxxx-xxxx",
  "response": {
    "result_code": 0,
    "response_name": "control_response",
    "paras": {
      "result": "success"
    }
  }
}
```

**失败响应**（设备离线/超时）:

```json
{
  "error_code": "IOTDA.014002",
  "error_msg": "The device is not online."
}
```

---

### 2.4 下发异步命令

下发命令不等待响应，适用于设备可能离线的场景。

**请求**:

```
POST /v5/iot/{project_id}/devices/{device_id}/async-commands
Content-Type: application/json
```

**请求体**:

```json
{
  "service_id": "aquarium_threshold",
  "command_name": "set_thresholds",
  "paras": {
    "temp_min": 24.0,
    "temp_max": 28.0
  },
  "expire_time": 86400,
  "send_strategy": "immediately"
}
```

---

### 2.5 查询设备状态

**请求**:

```
GET /v5/iot/{project_id}/devices/{device_id}
```

**响应**:

```json
{
  "device_id": "690237639798273cc4fd09cb_MyAquarium_01",
  "node_id": "MyAquarium_01",
  "device_name": "MyAquarium_01",
  "product_id": "690237639798273cc4fd09cb",
  "status": "ONLINE",
  "create_time": "20251213T100000Z"
}
```

---

## 3. 错误码

| 错误码 | 说明         |
| ------ | ------------ |
| 0      | 成功         |
| 1      | 设备执行失败 |
| 2      | 参数错误     |
| 3      | 设备离线     |
| 4      | 命令超时     |

---

## 4. AK/SK 签名示例（鸿蒙/TypeScript）

```typescript
// 华为云 AK/SK 签名认证在部分文档/SDK里也被称为“V11 签名”。在 HTTP 请求头 Authorization 中，算法标识为 SDK-HMAC-SHA256。
//
// 签名流程（与《API签名指南》一致）：
// 1) CanonicalRequest = HTTPMethod + '\n' + CanonicalURI + '\n' + CanonicalQueryString + '\n' +
//                      CanonicalHeaders + '\n' + SignedHeaders + '\n' + HexEncode(Hash(RequestPayload))
// 2) StringToSign     = Algorithm + '\n' + X-Sdk-Date + '\n' + HexEncode(Hash(CanonicalRequest))
// 3) Signature        = HexEncode(HMAC_SHA256(key=SK, msg=StringToSign))
// 4) Authorization    = `${Algorithm} Access=${AK}, SignedHeaders=${SignedHeaders}, Signature=${Signature}`
//
// 注意：
// - X-Sdk-Date 必须为 UTC，格式 YYYYMMDDTHHMMSSZ（例如 20251213T120000Z）
// - SignedHeaders 必须与 CanonicalHeaders 中的 header 名完全一致（小写、排序、分号分隔）

import { cryptoFramework } from "@kit.CryptoArchitectureKit";
import { buffer } from "@kit.ArkTS";

const ALGORITHM = "SDK-HMAC-SHA256";

function utf8Bytes(input: string): Uint8Array {
  return new Uint8Array(buffer.from(input, "utf-8").buffer);
}

function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes)
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

async function sha256Hex(payload: string): Promise<string> {
  const md = cryptoFramework.createMd("SHA256");
  await md.update({ data: utf8Bytes(payload) });
  const digest = await md.digest();
  return bytesToHex(digest.data);
}

async function hmacSha256Hex(key: string, data: string): Promise<string> {
  const keyGen = cryptoFramework.createSymKeyGenerator("HMAC");
  const symKey = await keyGen.convertKey({ data: utf8Bytes(key) });

  const mac = cryptoFramework.createMac("SHA256");
  await mac.init(symKey);
  await mac.update({ data: utf8Bytes(data) });
  const macResult = await mac.doFinal();
  return bytesToHex(macResult.data);
}

function canonicalHeaders(headers: Record<string, string>) {
  const normalized = Object.entries(headers)
    .map(([k, v]) => [k.toLowerCase().trim(), String(v).trim()] as const)
    .sort(([a], [b]) => (a === b ? 0 : a < b ? -1 : 1));

  const signedHeaders = normalized.map(([k]) => k).join(";");
  const canonical = normalized.map(([k, v]) => `${k}:${v}\n`).join("");

  return { canonical, signedHeaders };
}

export async function buildAuthorization(params: {
  method: string;
  canonicalUri: string; // 例如：/v5/iot/{project_id}/devices/{device_id}/shadow
  canonicalQueryString?: string; // 例如：limit=2&marker=xxx（无则空串）
  host: string;
  xSdkDate: string; // YYYYMMDDTHHMMSSZ
  contentType?: string; // application/json
  body?: string; // GET 可为空串
  ak: string;
  sk: string;
}) {
  const method = params.method.toUpperCase();
  const canonicalQueryString = params.canonicalQueryString ?? "";
  const body = params.body ?? "";

  const headersToSign: Record<string, string> = {
    host: params.host,
    "x-sdk-date": params.xSdkDate,
  };
  if (params.contentType) headersToSign["content-type"] = params.contentType;

  const { canonical: canonicalHeadersText, signedHeaders } =
    canonicalHeaders(headersToSign);

  const payloadHash = await sha256Hex(body);
  const canonicalRequest = [
    method,
    params.canonicalUri,
    canonicalQueryString,
    canonicalHeadersText,
    signedHeaders,
    payloadHash,
  ].join("\n");

  const stringToSign = [
    ALGORITHM,
    params.xSdkDate,
    await sha256Hex(canonicalRequest),
  ].join("\n");

  const signature = await hmacSha256Hex(params.sk, stringToSign);

  return `${ALGORITHM} Access=${params.ak}, SignedHeaders=${signedHeaders}, Signature=${signature}`;
}
```

---

_文档版本：v1.3 | 更新日期：2026-02-20_
