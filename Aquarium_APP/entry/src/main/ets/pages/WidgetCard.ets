import { CardSnapshot, loadCardSnapshot } from '../utils/cardSnapshotStore';

@Entry
@Component
struct WidgetCard {
  @State snapshot: CardSnapshot | null = null;
  @State errorText: string = '';

  aboutToAppear(): void {
    this.refresh();
  }

  private refresh(): void {
    loadCardSnapshot()
      .then((s) => {
        this.snapshot = s;
        this.errorText = s ? '' : '暂无数据：请先在 App 内刷新影子/状态或开启实时订阅。';
      })
      .catch((e: Error) => {
        this.snapshot = null;
        this.errorText = e?.message ?? String(e ?? '读取失败');
      });
  }

  private alarmColor(level: number): string {
    if (level >= 2) return '#D0021B';
    if (level === 1) return '#F2994A';
    return '#0A7D3B';
  }

  build() {
    Column() {
      Row() {
        Text('水族箱')
          .fontSize(16)
          .fontWeight(FontWeight.Bold);
        if (this.snapshot) {
          Text(this.snapshot.deviceStatus || 'UNKNOWN')
            .fontSize(12)
            .fontColor('#666666')
            .margin({ left: 8 });
        }
      }
      .width('100%');

      if (this.snapshot) {
        Text(`${this.snapshot.temperature.toFixed(1)}°C   pH ${this.snapshot.ph.toFixed(2)}`)
          .fontSize(18)
          .fontColor('#333333');
        Text(
          `TDS ${this.snapshot.tds.toFixed(0)}  Turb ${this.snapshot.turbidity.toFixed(0)}  Level ${this.snapshot.waterLevel.toFixed(0)}%`
        )
          .fontSize(12)
          .fontColor('#666666')
          .width('100%');
        Text(`告警等级 ${this.snapshot.alarmLevel}`)
          .fontSize(12)
          .fontColor(this.alarmColor(this.snapshot.alarmLevel));
        Text(`更新：${this.snapshot.updatedAt}`)
          .fontSize(10)
          .fontColor('#999999')
          .width('100%');
      } else {
        Text(this.errorText || '暂无数据')
          .fontSize(12)
          .fontColor('#999999')
          .width('100%');
        Button('刷新')
          .onClick(() => this.refresh())
          .height(32);
      }
    }
    .padding(12)
    .borderRadius(12)
    .backgroundColor('#FFFFFF')
    .width('100%')
    .height('100%');
  }
}
