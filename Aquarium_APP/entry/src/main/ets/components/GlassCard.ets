import { UiConstants } from './UiConstants';

@Component
export struct GlassCard {
  @BuilderParam content: () => void;
  @State isPressed: boolean = false;
  private onClickHandler?: () => void;

  build() {
    Column() {
      this.content()
    }
    .width('100%')
    .padding(16)
    .backgroundColor(UiConstants.COLOR_CARD_BG)
    .backdropBlur(20) // 毛玻璃效果
    .borderRadius(24) // 更圆润的角
    .shadow(this.isPressed ? ShadowStyle.OUTER_DEFAULT_SM : UiConstants.SHADOW_S)
    .scale({ x: this.isPressed ? 0.98 : 1, y: this.isPressed ? 0.98 : 1 })
    .animation({ duration: 200, curve: Curve.FastOutSlowIn })
    .onTouch((e: TouchEvent) => {
      if (e.type === TouchType.Down) {
        this.isPressed = true;
      } else if (e.type === TouchType.Up || e.type === TouchType.Cancel) {
        this.isPressed = false;
      }
    })
  }
}
