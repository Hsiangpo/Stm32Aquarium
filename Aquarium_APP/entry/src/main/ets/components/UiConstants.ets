export class UiConstants {
  // 核心配色 - 温馨、治愈、水族箱风格
  static readonly COLOR_PRIMARY = '#0A7D3B'; // 保持原有的绿色，作为品牌色
  static readonly COLOR_ACCENT = '#2D9CDB'; // 辅助蓝
  static readonly COLOR_WARNING = '#F2994A'; // 柔和橙
  static readonly COLOR_DANGER = '#EB5757'; // 柔和红
  
  static readonly COLOR_BG_1 = '#F0F9FF'; // 浅蓝背景起始
  static readonly COLOR_BG_2 = '#F5FBEF'; // 浅绿背景结束
  
  static readonly COLOR_CARD_BG = '#80FFFFFF'; // 半透明白
  static readonly COLOR_TEXT_PRIMARY = '#2C3E50';
  static readonly COLOR_TEXT_SECONDARY = '#7F8C8D';
  
  // 阴影
  static readonly SHADOW_S = ShadowStyle.OUTER_DEFAULT_XS;
  static readonly SHADOW_M = ShadowStyle.OUTER_DEFAULT_MD;
  
  // 动画曲线
  static readonly ANIM_CURVE = Curve.FastOutSlowIn;
  static readonly ANIM_DURATION = 400;
}
