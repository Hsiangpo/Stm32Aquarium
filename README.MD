# stm32Aquarium

基于 `STM32 NUCLEO-F103RB (STM32F103RB)` 的智能鱼缸控制器固件工程（VSCode + PlatformIO），通过 `ESP32-WROOM (AT)` 联网对接华为云 IoTDA。

## 目录说明

- `Aquarium_Device/`：STM32 固件（PlatformIO 工程入口，含 `platformio.ini`）
- `docs/PRD.MD`：需求文档
- `docs/HuaweiCloud.MD`：华为云 IoTDA 平台参数/物模型/Topic/注册流程
- `docs/Interface.MD`：设备侧 MQTT & 应用侧 REST 接口
- `docs/TECH.MD`：实现细节与开发环境说明
- `docs/Wiring_Guide.md`：接线与物料建议

## 快速开始（固件）

1. 安装 VSCode 扩展：`PlatformIO IDE`
2. 安装 `STM32CubeProgrammer`（提供 ST-LINK 驱动/升级工具）
3. 用 VSCode 打开 `Aquarium_Device/`，在 PlatformIO 面板执行 Build/Upload（或用终端 `pio run -t upload`）

首次烧录成功后，板载 LED（PA5/LD2）会闪烁用于验证工程可运行。

> 注意：`PB4` 默认是 JTAG `TRST`，固件已禁用 JTAG（保留 SWD）。如使用 `USART2 (PA2/PA3)` 连接 ESP32，需避免与 ST-LINK VCP 冲突（详见 `docs/TECH.MD`）。

## 快速开始（App：HarmonyOS 5.0.5 / API 17）

应用侧工程位于 `Aquarium_APP/`，用于通过 IoTDA REST API 拉取设备影子并下发命令（接口约定见 `docs/Interface.MD`）。

1. 用 DevEco Studio **5.0.5（API 17）** 打开 `Aquarium_APP/`
2. 连接真机或启动模拟器运行安装
3. 在首页填写 `Base URL / Project ID / Device ID / AK / SK`，点击“刷新影子/状态”即可看到 13 个核心属性与在线/告警状态

安全提示：

- 仓库内不写死 AK/SK；如仅用于本机调试，可在 App 内开启“本机保存 AK/SK”开关后再保存（默认不落盘）。
- 端到端验收与截图点位清单见 `docs/Acceptance_Checklist.md`；App 侧更详细运行说明见 `Aquarium_APP/README.md`。
