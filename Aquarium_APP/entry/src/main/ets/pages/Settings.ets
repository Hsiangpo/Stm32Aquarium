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
  @State autoRefreshEnabled: boolean = true;
  @State alarmNotifyEnabled: boolean = true;
  @State distributedSyncEnabled: boolean = false;

  @State noticeText: string = '';
  @State noticeIsError: boolean = false;
  @State isSaving: boolean = false;

  aboutToAppear(): void {
    this.restore();
  }

  private restore(): void {
    loadAppSettings()
      .then((saved) => {
        if (typeof saved.autoRefreshEnabled === 'boolean') this.autoRefreshEnabled = saved.autoRefreshEnabled;
        if (typeof saved.alarmNotifyEnabled === 'boolean') this.alarmNotifyEnabled = saved.alarmNotifyEnabled;
        if (typeof saved.distributedSyncEnabled === 'boolean') this.distributedSyncEnabled = saved.distributedSyncEnabled;

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
      const settings: AppSettings = {
        baseUrl: IOTDA_DEFAULTS.baseUrl,
        projectId: IOTDA_DEFAULTS.projectId,
        deviceId: IOTDA_DEFAULTS.deviceId,
        saveCredentials: false,
        saveDeviceSecret: false,
        autoRefreshEnabled: this.autoRefreshEnabled,
        alarmNotifyEnabled: this.alarmNotifyEnabled,
        distributedSyncEnabled: this.distributedSyncEnabled,
        ak: IOTDA_DEFAULTS.ak,
        sk: IOTDA_DEFAULTS.sk,
        deviceSecret: IOTDA_DEFAULTS.deviceSecret,
      };

      await saveAppSettings(settings);
      await this.toast('Saved');
      router.back();
    } catch (e) {
      this.noticeText = e?.message ?? String(e);
      this.noticeIsError = true;
      await this.toast('Save failed');
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

  @Builder
  private InfoRow(label: string, value: string) {
    Row({ space: 12 }) {
      Text(label)
        .fontSize(13)
        .fontColor('#666666')
        .layoutWeight(1);
      Text(value)
        .fontSize(13)
        .fontColor('#111111')
        .fontWeight(FontWeight.Medium)
        .textAlign(TextAlign.End);
    }
    .width('100%')
    .padding({ top: 10, bottom: 10 })
    .border({ width: { bottom: 1 }, color: '#F5F5F5' });
  }

  build() {
    Stack() {
      WarmBackground();

      Column({ space: 16 }) {
        Column() {
          GlassCard() {
            Row({ space: 12 }) {
              AnimButton({ text: 'Back', primary: false, isEnabled: true })
                .width(72)
                .onClick(() => this.back());

              Text('Settings')
                .fontSize(20)
                .fontWeight(FontWeight.Bold)
                .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
                .layoutWeight(1)
                .textAlign(TextAlign.Center);

              AnimButton({ text: this.isSaving ? '...' : 'Save', primary: true, isEnabled: !this.isSaving })
                .width(72)
                .onClick(() => {
                  this.saveAndBack().catch(() => {
                    // ignore
                  });
                });
            }
            .alignItems(VerticalAlign.Center);
          }
        }
        .margin({ top: 12, left: 12, right: 12 });

        if (this.noticeText) {
          Column() {
            GlassCard() {
              Text(this.noticeText)
                .fontSize(13)
                .fontColor(this.noticeIsError ? UiConstants.COLOR_DANGER : UiConstants.COLOR_PRIMARY)
                .width('100%');
            }
          }
          .margin({ left: 12, right: 12 });
        }

        Scroll() {
          Column({ space: 16 }) {
            GlassCard() {
              Column({ space: 8 }) {
                this.SectionTitle('Connection');
                Text('Connection parameters are hardcoded in demo mode and cannot be edited in UI.')
                  .fontSize(12)
                  .fontColor('#666666')
                  .width('100%');
                this.InfoRow('Base URL', IOTDA_DEFAULTS.baseUrl);
                this.InfoRow('Project ID', IOTDA_DEFAULTS.projectId);
                this.InfoRow('Device ID', IOTDA_DEFAULTS.deviceId);
              }
            }

            GlassCard() {
              Column({ space: 8 }) {
                this.SectionTitle('Behavior');
                this.ToggleRow('Auto refresh (30s)', this.autoRefreshEnabled, (v) => (this.autoRefreshEnabled = v));
                this.ToggleRow('Alarm notifications', this.alarmNotifyEnabled, (v) => (this.alarmNotifyEnabled = v));
                this.ToggleRow('Distributed sync', this.distributedSyncEnabled, (v) => (this.distributedSyncEnabled = v));
              }
            }

            GlassCard() {
              Column({ space: 8 }) {
                this.SectionTitle('Help');
                AnimButton({ text: 'About', primary: false })
                  .onClick(() => this.openAbout());
              }
            }
          }
          .padding({ top: 4, bottom: 32, left: 12, right: 12 });
        }
        .layoutWeight(1);
      }
    }
    .width('100%')
    .height('100%');
  }
}
