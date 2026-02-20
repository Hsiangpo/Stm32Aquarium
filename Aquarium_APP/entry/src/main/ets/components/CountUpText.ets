import { UiConstants } from './UiConstants';

@Component
export struct CountUpText {
  @Prop targetValue: number;
  @State currentValue: number = 0;
  @Prop fractionDigits: number = 0;
  @Prop suffix: string = '';
  @Prop fontSize: number = 24;
  @Prop fontColor: string = UiConstants.COLOR_TEXT_PRIMARY;

  private timer: number = 0;

  aboutToAppear() {
    this.animateToTarget();
  }

  onDidBuild() {
    // 监听 targetValue 变化
    this.animateToTarget();
  }

  animateToTarget() {
    // 简单的帧动画
    clearInterval(this.timer);
    if (Math.abs(this.targetValue - this.currentValue) < 0.001) {
        this.currentValue = this.targetValue;
        return;
    }

    const start = this.currentValue;
    const end = this.targetValue;
    const duration = 800;
    const startTime = Date.now();

    this.timer = setInterval(() => {
      const now = Date.now();
      const progress = Math.min((now - startTime) / duration, 1);
      // EaseOutCubic
      const ease = 1 - Math.pow(1 - progress, 3);
      
      this.currentValue = start + (end - start) * ease;

      if (progress >= 1) {
        this.currentValue = end;
        clearInterval(this.timer);
      }
    }, 16);
  }

  build() {
    Row({ space: 2 }) {
      Text(this.currentValue.toFixed(this.fractionDigits))
        .fontSize(this.fontSize)
        .fontWeight(FontWeight.Bold)
        .fontColor(this.fontColor)
      
      if (this.suffix) {
        Text(this.suffix)
          .fontSize(this.fontSize * 0.5)
          .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
          .margin({ bottom: 4 })
          .alignSelf(ItemAlign.End)
      }
    }
    .alignItems(VerticalAlign.Bottom)
  }
}
