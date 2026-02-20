import { UiConstants } from './UiConstants';

@Component
export struct WarmBackground {
  private settings: RenderingContextSettings = new RenderingContextSettings(true);
  private context: CanvasRenderingContext2D = new CanvasRenderingContext2D(this.settings);
  @State private bubbles: Bubble[] = [];
  private timerId: number = 0;
  private lastTime: number = 0;

  aboutToAppear() {
    for (let i = 0; i < 15; i++) {
      this.bubbles.push(new Bubble());
    }
  }

  aboutToDisappear() {
    clearInterval(this.timerId);
    this.timerId = 0;
  }

  build() {
    Stack() {
      // 动态气泡层
      Canvas(this.context)
        .width('100%')
        .height('100%')
        .onReady(() => {
          this.startAnimation();
        })
        .hitTestBehavior(HitTestMode.None) // 不拦截点击事件
    }
    .width('100%')
    .height('100%')
  }

  startAnimation() {
    if (this.timerId) {
      clearInterval(this.timerId);
      this.timerId = 0;
    }
    this.lastTime = Date.now();
    this.timerId = setInterval(() => {
      const now = Date.now();
      const dt = now - this.lastTime;
      this.lastTime = now;
      this.draw(dt);
    }, 32) // ~30fps is enough for background
  }

  draw(dt: number) {
    // 假设画布尺寸，动态获取更好但这里简化处理
    const width = this.context.width;
    const height = this.context.height;
    
    // 绘制渐变背景（避免引入 LinearGradient 组件语法差异）
    const gradient = this.context.createLinearGradient(0, 0, 0, height);
    gradient.addColorStop(0, UiConstants.COLOR_BG_1);
    gradient.addColorStop(1, UiConstants.COLOR_BG_2);
    this.context.fillStyle = gradient;
    this.context.fillRect(0, 0, width, height);

    this.bubbles.forEach(b => {
      b.update(dt, width, height);
      this.context.beginPath();
      this.context.arc(b.x, b.y, b.radius, 0, 6.28);
      this.context.fillStyle = b.color;
      this.context.fill();
    });
  }
}

class Bubble {
  x: number = 0;
  y: number = 0;
  radius: number = 0;
  speedY: number = 0;
  opacity: number = 0;
  color: string = '';

  constructor() {
    this.reset(true);
  }

  reset(randomY: boolean = false) {
    this.x = Math.random() * 360; // 假设屏幕宽度约360vp，Canvas会自动缩放
    this.y = randomY ? Math.random() * 800 : 850;
    this.radius = 5 + Math.random() * 15;
    this.speedY = 0.02 + Math.random() * 0.05;
    this.opacity = 0.1 + Math.random() * 0.2;
    // 柔和的白色/淡蓝色气泡
    this.color = `rgba(255, 255, 255, ${this.opacity})`;
  }

  update(dt: number, w: number, h: number) {
    this.y -= this.speedY * dt;
    // 简单的左右摇摆
    this.x += Math.sin(this.y * 0.02) * 0.1;

    if (this.y < -50) {
      this.reset();
    }
  }
}
