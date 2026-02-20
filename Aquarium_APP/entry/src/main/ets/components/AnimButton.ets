import { UiConstants } from './UiConstants';

@Component
export struct AnimButton {
  @Prop text: string;
  @Prop primary: boolean = true;
  @Prop isEnabled: boolean = true;
  @State isPressed: boolean = false;

  build() {
    Stack() {
      // 按钮背景
      Rect()
        .width('100%')
        .height('100%')
        .fill(this.isEnabled 
          ? (this.primary ? UiConstants.COLOR_PRIMARY : '#FFFFFF') 
          : '#E0E0E0')
        .radius(18)
        .shadow(this.isPressed ? ShadowStyle.OUTER_DEFAULT_XS : UiConstants.SHADOW_S)
        .animation({ duration: 200 })

      // 文字
      Text(this.text)
        .fontSize(16)
        .fontWeight(FontWeight.Medium)
        .fontColor(this.isEnabled 
          ? (this.primary ? '#FFFFFF' : UiConstants.COLOR_PRIMARY) 
          : '#999999')
    }
    .width('100%')
    .height(48)
    .enabled(this.isEnabled)
    .scale({ x: this.isPressed ? 0.96 : 1, y: this.isPressed ? 0.96 : 1 })
    .animation({ duration: 100 })
    .onTouch((e: TouchEvent) => {
      if (!this.isEnabled) return;
      if (e.type === TouchType.Down) {
        this.isPressed = true;
      } else if (e.type === TouchType.Up || e.type === TouchType.Cancel) {
        this.isPressed = false;
      }
    })
  }
}
