import router from '@ohos.router';
import { IOTDA_DEFAULTS } from '../config/iotdaDefaults';
import { UiConstants } from '../components/UiConstants';
import { WarmBackground } from '../components/WarmBackground';
import { GlassCard } from '../components/GlassCard';
import { AnimButton } from '../components/AnimButton';

@Entry
@Component
struct About {
  private back(): void {
    router.back();
  }

  build() {
    Stack() {
      WarmBackground()

      Column({ space: 16 }) {
        Column() {
          GlassCard() {
            Row({ space: 12 }) {
              AnimButton({ text: '返回', primary: false, isEnabled: true })
                .width(72)
                .onClick(() => this.back())

              Text('关于')
                .fontSize(20)
                .fontWeight(FontWeight.Bold)
                .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
                .layoutWeight(1)
                .textAlign(TextAlign.Center)
              
              Blank().width(72) // 占位
            }
            .alignItems(VerticalAlign.Center)
          }
        }
        .margin({ top: 12, left: 12, right: 12 })

        Scroll() {
          Column({ space: 16 }) {
            GlassCard() {
              Column({ space: 8 }) {
                Text('Aquarium Assistant')
                  .fontSize(24)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(UiConstants.COLOR_PRIMARY)
                
                Text('温馨、治愈的陪伴式水族箱助手')
                  .fontSize(14)
                  .fontColor(UiConstants.COLOR_ACCENT)
                  .fontWeight(FontWeight.Medium)

                Text('用于毕业设计演示：拉取设备影子 / 下发三类命令 / 记录历史与导出实验数据。')
                  .fontSize(14)
                  .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
                  .lineHeight(20)
                  .width('100%')
                  .margin({ top: 8 })
              }
            }

            GlassCard() {
              Column({ space: 12 }) {
                Text('快速开始')
                  .fontSize(18)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
                
                Text('1. 打开“设置”：填写 Base URL / Project ID / Device ID，并输入 AK/SK。')
                  .fontSize(14)
                  .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
                
                Text('2. 回到首页点击“刷新”：查看 13 个核心水质属性与设备在线状态。')
                  .fontSize(14)
                  .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
                
                Text('3. 在“控制”页下发命令，或在“配置”页调整阈值。')
                  .fontSize(14)
                  .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
                
                Text('4. 在“历史”页查看趋势图，并可一键导出 JSON 实验数据。')
                  .fontSize(14)
                  .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
              }
            }

            GlassCard() {
              Column({ space: 12 }) {
                Text('安全提示')
                  .fontSize(18)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
                
                Text('AK/SK 与 Device Secret 默认仅保存在内存中；仅在设置页开启“本机保存”时写入加密存储。请勿将凭证提交到代码仓库。')
                  .fontSize(14)
                  .fontColor(UiConstants.COLOR_DANGER)
                  .lineHeight(20)
              }
            }

            GlassCard() {
              Column({ space: 8 }) {
                Text('默认信息')
                  .fontSize(16)
                  .fontWeight(FontWeight.Medium)
                  .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
                
                Text(`MQTT Endpoint: ${IOTDA_DEFAULTS.deviceMqttHost}:${IOTDA_DEFAULTS.deviceMqttPort}`)
                  .fontSize(13)
                  .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
              }
            }
          }
          .padding({ top: 4, bottom: 32, left: 12, right: 12 })
        }
        .layoutWeight(1)
      }
    }
    .width('100%')
    .height('100%')
  }
}
