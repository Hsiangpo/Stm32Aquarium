import router from '@ohos.router';
import promptAction from '@ohos.promptAction';
import { IOTDA_DEFAULTS } from '../config/iotdaDefaults';
import { AppSettings, loadAppSettings, saveAppSettings } from '../utils/appSettings';
import { UiConstants } from '../components/UiConstants';
import { WarmBackground } from '../components/WarmBackground';
import { GlassCard } from '../components/GlassCard';
import { AnimButton } from '../components/AnimButton';

@Entry
@Component
struct Settings {
  @State baseUrl: string = IOTDA_DEFAULTS.baseUrl;
  @State projectId: string = IOTDA_DEFAULTS.projectId;
  @State deviceId: string = IOTDA_DEFAULTS.deviceId;

  @State ak: string = IOTDA_DEFAULTS.ak;
  @State sk: string = IOTDA_DEFAULTS.sk;
  @State saveCredentials: boolean = false;

  @State deviceSecret: string = IOTDA_DEFAULTS.deviceSecret;
  @State saveDeviceSecret: boolean = false;

  @State autoRefreshEnabled: boolean = true;
  @State alarmNotifyEnabled: boolean = true;
  @State distributedSyncEnabled: boolean = false;

  @State noticeText: string = '';
  @State noticeIsError: boolean = false;
  @State isSaving: boolean = false;

  aboutToAppear(): void {
    this.restore();
  }

  private applyHardcodedDefaults(): void {
    this.baseUrl = IOTDA_DEFAULTS.baseUrl;
    this.projectId = IOTDA_DEFAULTS.projectId;
    this.deviceId = IOTDA_DEFAULTS.deviceId;
    this.ak = IOTDA_DEFAULTS.ak;
    this.sk = IOTDA_DEFAULTS.sk;
    this.deviceSecret = IOTDA_DEFAULTS.deviceSecret;
  }

  private restore(): void {
    loadAppSettings()
      .then((saved) => {
        if (typeof saved.saveCredentials === 'boolean') this.saveCredentials = saved.saveCredentials;
        if (typeof saved.saveDeviceSecret === 'boolean') this.saveDeviceSecret = saved.saveDeviceSecret;
        if (typeof saved.autoRefreshEnabled === 'boolean') this.autoRefreshEnabled = saved.autoRefreshEnabled;
        if (typeof saved.alarmNotifyEnabled === 'boolean') this.alarmNotifyEnabled = saved.alarmNotifyEnabled;
        if (typeof saved.distributedSyncEnabled === 'boolean') this.distributedSyncEnabled = saved.distributedSyncEnabled;

        if (saved.ak) this.ak = saved.ak;
        if (saved.sk) this.sk = saved.sk;
        if (saved.deviceSecret) this.deviceSecret = saved.deviceSecret;
        this.applyHardcodedDefaults();

        this.noticeText = '';
        this.noticeIsError = false;
      })
      .catch((e: Error) => {
        this.noticeText = e?.message ?? String(e);
        this.noticeIsError = true;
      });
  }

  private async toast(message: string): Promise<void> {
    try {
      const opts: promptAction.ShowToastOptions = { message, duration: 2000 };
      promptAction.showToast(opts);
    } catch {
      // ignore
    }
  }

  private async saveAndBack(): Promise<void> {
    if (this.isSaving) {
      return;
    }
    this.isSaving = true;
    this.noticeText = '';
    this.noticeIsError = false;
    try {
      this.applyHardcodedDefaults();
      const settings: AppSettings = {
        baseUrl: this.baseUrl.trim(),
        projectId: this.projectId.trim(),
        deviceId: this.deviceId.trim(),
        saveCredentials: this.saveCredentials,
        saveDeviceSecret: this.saveDeviceSecret,
        autoRefreshEnabled: this.autoRefreshEnabled,
        alarmNotifyEnabled: this.alarmNotifyEnabled,
        distributedSyncEnabled: this.distributedSyncEnabled,
        ak: this.ak.trim(),
        sk: this.sk.trim(),
        deviceSecret: this.deviceSecret,
      };

      await saveAppSettings(settings);
      await this.toast('已保存');
      router.back();
    } catch (e) {
      this.noticeText = e?.message ?? String(e);
      this.noticeIsError = true;
      await this.toast('保存失败');
    } finally {
      this.isSaving = false;
    }
  }

  private back(): void {
    router.back();
  }

  private openAbout(): void {
    const options: router.RouterOptions = { url: 'pages/About' };
    router.pushUrl(options);
  }

  @Builder
  private SectionTitle(title: string) {
    Row({ space: 8 }) {
      Rect()
        .width(4)
        .height(16)
        .fill('#0A7D3B')
        .radius(2);
      Text(title)
        .fontSize(16)
        .fontWeight(FontWeight.Bold)
        .fontColor('#111111');
    }
    .margin({ top: 16, bottom: 8 })
    .width('100%')
    .alignItems(VerticalAlign.Center);
  }

  @Builder
  private ToggleRow(label: string, isOn: boolean, onChange: (v: boolean) => void) {
    Row({ space: 12 }) {
      Text(label)
        .fontSize(14)
        .fontColor('#333333')
        .fontWeight(FontWeight.Medium)
        .layoutWeight(1);
      Toggle({ type: ToggleType.Switch, isOn })
        .selectedColor('#0A7D3B')
        .switchPointColor('#FFFFFF')
        .onChange(onChange);
    }
    .width('100%')
    .padding({ top: 12, bottom: 12 })
    .border({ width: { bottom: 1 }, color: '#F5F5F5' });
  }

  build() {
    Stack() {
      WarmBackground()

      Column({ space: 16 }) {
        // Header
        Column() {
          GlassCard() {
            Row({ space: 12 }) {
              AnimButton({ text: '返回', primary: false, isEnabled: true })
                .width(72)
                .onClick(() => this.back())
              
              Text('设置')
                .fontSize(20)
                .fontWeight(FontWeight.Bold)
                .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
                .layoutWeight(1)
                .textAlign(TextAlign.Center)
              
              AnimButton({ text: this.isSaving ? '...' : '保存', primary: true, isEnabled: !this.isSaving })
                .width(72)
                .onClick(() => {
                  this.saveAndBack().catch(() => {
                    // ignore
                  });
                })
            }
            .alignItems(VerticalAlign.Center)
          }
        }
        .margin({ top: 12, left: 12, right: 12 })

        if (this.noticeText) {
          Column() {
            GlassCard() {
              Text(this.noticeText)
                .fontSize(13)
                .fontColor(this.noticeIsError ? UiConstants.COLOR_DANGER : UiConstants.COLOR_PRIMARY)
                .width('100%')
            }
          }
          .margin({ left: 12, right: 12 })
        }

        Scroll() {
          Column({ space: 16 }) {
            GlassCard() {
              Column({ space: 8 }) {
                this.SectionTitle('连接参数')
                Text('演示模式：华为云区域/端点/设备ID/密钥均为内置固定值，前端不提供配置入口。')
                  .fontSize(12)
                  .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
                  .width('100%')
                  .lineHeight(20)
              }
            }

            GlassCard() {
              Column({ space: 8 }) {
                this.SectionTitle('行为与偏好')
                this.ToggleRow('自动刷新 (30s/次)', this.autoRefreshEnabled, (v) => (this.autoRefreshEnabled = v))
                this.ToggleRow('告警通知 (系统通知)', this.alarmNotifyEnabled, (v) => (this.alarmNotifyEnabled = v))
                this.ToggleRow('分布式同步 (多设备)', this.distributedSyncEnabled, (v) => (this.distributedSyncEnabled = v))
              }
            }

            GlassCard() {
              Column({ space: 8 }) {
                this.SectionTitle('帮助')
                AnimButton({ text: '查看关于 / 使用说明', primary: false })
                  .onClick(() => this.openAbout())
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


