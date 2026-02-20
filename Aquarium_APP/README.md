# Aquarium_APP（HarmonyOS 5.0.5 / API 17）

本目录是“智能水族箱”项目的 HarmonyOS 应用侧工程，用于通过华为云 IoTDA REST API：

- 查询设备影子（13 个核心属性）
- 查询设备在线状态
- 下发命令：`control` / `set_thresholds`（async）/ `set_config`
- （可演示）直连 IoTDA Device MQTT(1883) 实时订阅属性上报

## 运行环境

- DevEco Studio：**5.0.5（API 17）**
- 设备：HarmonyOS 手机/模拟器（需联网）
- 权限：已在 `entry/src/main/module.json5` 申请 `ohos.permission.INTERNET`

## 快速开始

1. 用 DevEco Studio 打开本工程目录：`Aquarium_APP/`
2. 连接真机或启动模拟器，运行安装
3. 项目已内置演示参数（`Base URL / Project ID / Device ID / AK / SK / Device Secret`）
4. 回到首页点击“**刷新**”
   - 成功后：仪表盘展示在线状态、告警状态与 13 项属性
   - 可开启“自动刷新（30s/次）”与设备上报频率保持一致（开关会持久化）
5. 在下方 Tabs 中操作：
   - **仪表盘**：维护最近刷新历史（默认展示最近 10 条，最多 50 条），可清空；导出实验记录(JSON)会包含 `history`，便于论文实验数据截图/导出
   - **控制**：下发 `control`（全参数）或“投喂一次（feed=true）”，并查看返回/错误
     - `control`/`set_config` 成功后会自动刷新影子/状态（延迟约 1~2s）
   - **阈值&配置**：
     - 下发 `set_thresholds`（走 `/async-commands`）
     - 下发 `set_config`（走 `/commands`，支持 WiFi 与校准参数；含 WiFi 变更时会二次确认）
   - **历史**：趋势图 + 全量历史列表（最多 50 条）

## MQTT 实时订阅（最小可演示）

App 可直连 IoTDA **Device Endpoint**（TCP MQTT `1883`，不是 App Endpoint）订阅属性上报 Topic：

- 订阅：`$oc/devices/{device_id}/sys/properties/report`

使用方式：

1. 使用内置 `Device Secret`（演示模式固定值）
2. 打开“实时订阅”开关
3. 设备上报一次属性后，App 会：
   - 无需手动刷新即可更新仪表盘数值
   - 历史记录追加 1 条（最多 50 条）
   - `lastRefreshResult` 显示为 `REALTIME`

鉴权与实现位置：

- MQTT 鉴权：`entry/src/main/ets/api/iotdaMqttAuth.ts`
  - `clientId={device_id}_0_0_2000010100`（sign_type=0，避免与设备端 sign_type=1 冲突）
  - `username=device_id`
  - `password=HMAC_SHA256(key=timestamp, msg=deviceSecret)`（见 `docs/HuaweiCloud.MD`）
- MQTT 客户端（TCP CONNECT/SUBSCRIBE/PINGREQ + QoS0 PUBLISH 解析 + 指数退避重连）：
  - `entry/src/main/ets/api/realtimeMqttClient.ts`

注意事项：

- 1883 为明文 TCP，可能被网络/路由策略拦截；如需更安全的传输应改为 8883（本任务范围外）。
- 部分平台/协议栈可能限制同一 `device_id` 仅允许一个在线连接；开启 App “实时订阅”可能与设备端 MQTT 连接互相影响（掉线/互踢）。建议默认用 REST 自动刷新，演示实时订阅时尽量避免与设备端同时在线。
- 关闭“实时订阅”后，现有 REST 自动刷新/命令功能不受影响。

## 趋势图（折线）与多端适配

- 仪表盘新增“趋势图”折线（基于 `history` 最近 N 条刷新/实时上报记录），可切换指标：温度 / pH / TDS / 浊度 / 水位。
- 当窗口宽度 ≥ 600vp 时自动启用“宽屏布局”：三个分区（仪表盘 / 控制 / 阈值&配置）并排显示，适配平板/横屏。

实现位置：

- 趋势图 + 宽屏布局：`entry/src/main/ets/pages/Index.ets`

## 告警通知（系统通知）

- 当 `alarm_level` 发生变化时（升级/恢复），App 可发送一条系统通知并 Toast 提示。
- 开关在首页“连接配置”中：`告警通知（系统通知）`（会持久化）。

实现位置：

- 通知封装：`entry/src/main/ets/utils/alarmNotifier.ts`
- 触发逻辑：`entry/src/main/ets/pages/Index.ets`

## 桌面服务卡片（Widget）

提供一个最小可展示的桌面服务卡片，显示“最近一次快照（本机缓存）”：温度/pH/TDS/浊度/水位/告警等级/更新时间。

- 卡片数据来源：App 在“刷新影子/状态”或“实时订阅收到上报”后，会把快照写入 Preferences；卡片读取该快照展示。
- 这样可避免在卡片侧存放 AK/SK 或进行签名请求（更安全、也更符合毕设演示）。

实现位置：

- 快照持久化：`entry/src/main/ets/utils/cardSnapshotStore.ts`
- 卡片 UI：`entry/src/main/ets/pages/WidgetCard.ets`
- Form Extension：`entry/src/main/ets/formextensionability/EntryFormAbility.ets`
- 卡片配置：`entry/src/main/resources/base/profile/form_config.json`

## 分布式流转与账号同步（加分项）

本工程提供“最小可演示”的分布式能力：

- **账号同步（分布式KV）**：将非敏感连接配置（`Base URL / Project ID / Device ID / 自动刷新 / 告警通知`）发布到分布式KV，同账号的可信设备可拉取/自动接收更新。
- **分布式流转**：在 A 设备点击“分布式流转到其他设备”，选择目标设备后，B 设备将以携带的参数启动 App（仅包含非敏感配置）。

实现位置：

- 分布式KV与设备列表/流转：`entry/src/main/ets/utils/distributedSync.ts`
- UI 入口：`entry/src/main/ets/pages/Index.ets`

注意：

- 只同步 **非敏感** 配置，AK/SK 与 Device Secret 均不会被分布式同步。
- 流转/同步需要系统层完成“同账号设备互信/组网”（可信设备列表为空时属正常）。

## 演示模式说明

- 本仓库为单人课设演示版，连接参数采用固定内置值。
- 设置页仅保留演示说明与行为开关，不提供华为云连接参数编辑入口。
- 分布式同步仍可同步“自动刷新/告警通知”等非敏感开关，不改变固定连接参数。

## 主要代码位置

- V11（SDK-HMAC-SHA256）签名：`entry/src/main/ets/api/iotdaSigner.ts`
- IoTDA REST 客户端：`entry/src/main/ets/api/iotdaRestClient.ts`
- 首页 UI（仪表盘/控制/阈值&配置）：`entry/src/main/ets/pages/Index.ets`
- 接口约定：`docs/Interface.MD`

## 常见问题

- `HTTP 401 ...`：通常是 AK/SK、签名、`Base URL`（endpoint）或设备/项目 ID 填写错误。
- `HTTP 404 ...`：多为 `project_id` / `device_id` 不存在或路径拼写错误。
- `HTTP 408/5xx` 或 “命令超时”：设备离线、网络抖动、或设备正在重连（例如下发 WiFi 配置后短暂离线）。
