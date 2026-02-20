import { IotdaRestClient } from '../api/iotdaRestClient';
import { buildIotdaMqttAuth } from '../api/iotdaMqttAuth';
import { RealtimeMqttClient, RealtimeMqttState } from '../api/realtimeMqttClient';
import { IOTDA_DEFAULTS } from '../config/iotdaDefaults';
import {
  AquariumProperties,
  AquariumShadowSnapshot,
  ConfigCommandParas,
  ControlCommandParas,
  IotdaAsyncCommandResponse,
  IotdaDeviceInfo,
  IotdaSyncCommandResponse,
  ThresholdCommandParas,
} from '../model/aquariumModels';
import { publishAlarmNotification } from '../utils/alarmNotifier';
import { loadAppSettings, saveAlarmNotifyEnabled, saveAppSettings, saveAutoRefreshEnabled, saveDistributedSyncEnabled } from '../utils/appSettings';
import { saveCardSnapshot } from '../utils/cardSnapshotStore';
import { GlobalContext } from '../utils/globalContext';
import {
  DistributedDeviceInfo,
  loadSharedSettings,
  publishSharedSettings,
  startRemoteFlow,
  subscribeSharedSettingsChanged,
  listTrustedDevices,
} from '../utils/distributedSync';
import promptAction from '@ohos.promptAction';
import pasteboard from '@ohos.pasteboard';
import mediaquery from '@ohos.mediaquery';
import router from '@ohos.router';
import { UiConstants } from '../components/UiConstants';
import { WarmBackground } from '../components/WarmBackground';
import { GlassCard } from '../components/GlassCard';
import { CountUpText } from '../components/CountUpText';
import { AnimButton } from '../components/AnimButton';

interface RefreshHistoryItem {
  at: string;
  device_status: string;
  alarm_level: number;
  temperature: number;
  ph: number;
  tds: number;
  turbidity: number;
  water_level: number;
}

type TrendMetricKey = 'temperature' | 'ph' | 'tds' | 'turbidity' | 'water_level';

interface TrendPath {
  commands: string;
  min: number;
  max: number;
  count: number;
}

interface RefreshShadowOptions {
  showNotice?: boolean;
  resetResponses?: boolean;
}

interface SharedSettingsPayload {
  updatedAt: string;
  baseUrl: string;
  projectId: string;
  deviceId: string;
  autoRefreshEnabled: boolean;
  alarmNotifyEnabled: boolean;
}

interface MediaQueryChangeEvent {
  matches?: boolean;
}

interface MediaQueryLike {
  matches?: boolean;
  on?: (event: string, listener: (res: MediaQueryChangeEvent) => void) => void;
  off?: (event: string, listener: (res: MediaQueryChangeEvent) => void) => void;
}

type JsonValue = string | number | boolean | null | undefined | JsonObject | JsonValue[];

interface JsonObject {
  temperature?: JsonValue;
  ph?: JsonValue;
  tds?: JsonValue;
  turbidity?: JsonValue;
  water_level?: JsonValue;
  heater?: JsonValue;
  pump_in?: JsonValue;
  pump_out?: JsonValue;
  auto_mode?: JsonValue;
  feed_countdown?: JsonValue;
  feeding_in_progress?: JsonValue;
  alarm_level?: JsonValue;
  alarm_muted?: JsonValue;
  wifi_ssid?: JsonValue;
  wifi_password?: JsonValue;
  ph_offset?: JsonValue;
  tds_factor?: JsonValue;
  command?: JsonValue;
  paras?: JsonValue;
  error?: JsonValue;
  services?: JsonValue;
  properties?: JsonValue;
  event_time?: JsonValue;
}

interface ParseOk<T> {
  ok: true;
  paras: T;
}

interface ParseFail {
  ok: false;
  message: string;
}

type ParseResult<T> = ParseOk<T> | ParseFail;

interface ExperimentRecordDevice {
  baseUrl: string;
  projectId: string;
  deviceId: string;
}

interface ExperimentRecordRefresh {
  last_at: string;
  last_result: string;
}

interface ExperimentRecordLastCommand {
  at: string;
  name: string;
  result: string;
  payload: JsonValue;
  response: JsonValue;
}

interface ExperimentRecord {
  export_time: string;
  device: ExperimentRecordDevice;
  refresh: ExperimentRecordRefresh;
  history: RefreshHistoryItem[];
  status: IotdaDeviceInfo | null;
  shadow: AquariumShadowSnapshot | null;
  last_command: ExperimentRecordLastCommand;
}

interface CommandRecordPayloadControl {
  command: 'control';
  paras: ControlCommandParas;
}

interface CommandRecordPayloadThreshold {
  command: 'set_thresholds';
  paras: ThresholdCommandParas;
}

interface CommandRecordPayloadConfig {
  command: 'set_config';
  paras: JsonObject;
}

interface ErrorRecordPayload {
  error: string;
}

@Entry
@Component
struct Index {
  @State baseUrl: string = IOTDA_DEFAULTS.baseUrl;
  @State projectId: string = IOTDA_DEFAULTS.projectId;
  @State deviceId: string = IOTDA_DEFAULTS.deviceId;

  @State ak: string = IOTDA_DEFAULTS.ak;
  @State sk: string = IOTDA_DEFAULTS.sk;
  @State saveCredentials: boolean = false;
  @State autoRefreshEnabled: boolean = true;
  @State alarmNotifyEnabled: boolean = true;
  @State distributedSyncEnabled: boolean = false;
  @State isWifiConfirming: boolean = false;

  @State deviceSecret: string = IOTDA_DEFAULTS.deviceSecret;
  @State saveDeviceSecret: boolean = false;
  @State realtimeEnabled: boolean = false;
  @State realtimeState: RealtimeMqttState = 'DISCONNECTED';
  @State realtimeStateMessage: string = '';
  @State isWideLayout: boolean = false;
  @State trendMetric: TrendMetricKey = 'temperature';

  private readonly trendMetricKeys: TrendMetricKey[] = ['temperature', 'ph', 'tds', 'turbidity', 'water_level'];

  private autoRefreshTimerId: number | undefined = undefined;
  private readonly autoRefreshIntervalMs: number = 30000;
  private readonly refreshHistoryMaxSize: number = 50;
  private readonly refreshHistoryPreviewCount: number = 10;
  private realtimeClient: RealtimeMqttClient | null = null;
  private realtimeSessionId: number = 0;
  private mediaQuery: MediaQueryLike | null = null;
  private mediaQueryListener: ((res: MediaQueryChangeEvent) => void) | null = null;
  private distributedUnsubscribe: (() => void) | null = null;

  @State isLoading: boolean = false;
  @State noticeText: string = '';
  @State noticeIsError: boolean = false;
  @State noticeHint: string = '';
  @State lastRefreshAt: string = '-';
  @State lastRefreshResult: string = '-';

  @State lastCommandAt: string = '-';
  @State lastCommandName: string = '-';
  @State lastCommandResult: string = '-';
  @State lastCommandPayload: string = '';
  @State lastCommandResponse: string = '';

  @State exportedExperimentRecord: string = '';
  @State refreshHistory: RefreshHistoryItem[] = [];

  @State deviceInfo: IotdaDeviceInfo | null = null;
  @State shadowSnapshot: AquariumShadowSnapshot | null = null;

  @State controlResp: IotdaSyncCommandResponse | null = null;
  @State thresholdResp: IotdaAsyncCommandResponse | null = null;
  @State configResp: IotdaSyncCommandResponse | null = null;

  /* control 表单 */
  @State ctl_heater: boolean = false;
  @State ctl_pump_in: boolean = false;
  @State ctl_pump_out: boolean = false;
  @State ctl_auto_mode: boolean = true;
  @State ctl_mute: boolean = false;
  @State ctl_target_temp: string = '26.0';
  @State ctl_feed_once_delay: string = '';

  /* threshold 表单（默认值与设备侧默认阈值保持一致） */
  @State th_temp_min: string = '24.0';
  @State th_temp_max: string = '28.0';
  @State th_ph_min: string = '6.5';
  @State th_ph_max: string = '7.5';
  @State th_tds_warn: string = '500';
  @State th_tds_critical: string = '800';
  @State th_turbidity_warn: string = '30';
  @State th_turbidity_critical: string = '50';
  @State th_level_min: string = '20';
  @State th_level_max: string = '95';
  @State th_feed_interval: string = '12';
  @State th_feed_amount: string = '2';

  /* config 表单 */
  @State cfg_wifi_ssid: string = '';
  @State cfg_wifi_password: string = '';
  @State cfg_ph_offset: string = '';
  @State cfg_tds_factor: string = '';

  @State currentIndex: number = 0;
  private tabsController: TabsController = new TabsController();

  @Builder
  private TabBarItem(index: number, name: string) {
    Column() {
      Text(name)
        .fontSize(this.currentIndex === index ? 16 : 14)
        .fontWeight(this.currentIndex === index ? FontWeight.Bold : FontWeight.Normal)
        .fontColor(this.currentIndex === index ? UiConstants.COLOR_PRIMARY : UiConstants.COLOR_TEXT_SECONDARY)
        .animation({ duration: 300, curve: Curve.FastOutSlowIn })
        .margin({ bottom: 4 })
      
      // 指示器
      Rect()
        .width(this.currentIndex === index ? 20 : 0)
        .height(4)
        .radius(2)
        .fill(UiConstants.COLOR_PRIMARY)
        .animation({ duration: 300, curve: Curve.FastOutSlowIn })
    }
    .width('100%')
    .height(56)
    .justifyContent(FlexAlign.Center)
    .onClick(() => {
      this.tabsController.changeIndex(index);
      this.currentIndex = index;
    })
  }

  private buildClient(): IotdaRestClient {
    this.applyHardcodedDefaults();
    return new IotdaRestClient(
      {
        baseUrl: this.baseUrl,
        projectId: this.projectId,
        deviceId: this.deviceId,
      },
      {
        ak: this.ak,
        sk: this.sk,
      }
    );
  }

  private openSettings(): void {
    const options: router.RouterOptions = { url: 'pages/Settings' };
    router.pushUrl(options);
  }

  aboutToAppear(): void {
    this.setupMediaQuery();
    this.restoreAppSettings()
      .then(() => {
        this.applyPendingWantParams();
        this.updateAutoRefreshTimer();
        this.updateRealtimeSubscription();
        this.updateDistributedSyncSubscription();
      })
      .catch((e: Error) => {
        this.setError(e?.message ?? String(e));
      });
  }

  aboutToDisappear(): void {
    this.disposeMediaQuery();
    this.stopAutoRefreshTimer();
    this.stopRealtimeSubscription();
    this.stopDistributedSyncSubscription();
  }

  private setupMediaQuery(): void {
    this.disposeMediaQuery();
    try {
      const mq = mediaquery.matchMediaSync('(min-width: 600vp)') as MediaQueryLike;
      this.isWideLayout = !!mq?.matches;
      const listener = (res: MediaQueryChangeEvent) => {
        this.isWideLayout = !!res?.matches;
      };
      mq?.on?.('change', listener);
      this.mediaQuery = mq;
      this.mediaQueryListener = listener;
    } catch {
      this.isWideLayout = false;
    }
  }

  private disposeMediaQuery(): void {
    const mq = this.mediaQuery;
    const listener = this.mediaQueryListener;
    this.mediaQuery = null;
    this.mediaQueryListener = null;
    try {
      if (mq && listener) {
        mq?.off?.('change', listener);
      }
    } catch {
      // ignore
    }
  }

  private setError(msg: string): void {
    this.noticeText = msg;
    this.noticeIsError = true;
    this.noticeHint = this.explainErrorForUi(msg);
  }

  private setInfo(msg: string): void {
    this.noticeText = msg;
    this.noticeIsError = false;
    this.noticeHint = '';
  }

  private clearNotice(): void {
    this.noticeText = '';
    this.noticeIsError = false;
    this.noticeHint = '';
  }

  private explainErrorForUi(rawMessage: string): string {
    const msg = String(rawMessage ?? '');
    if (!msg) {
      return '';
    }

    if (msg.includes('IOTDA.014002')) {
      return '设备不在线：请确认设备已联网并上线（MQTT ONLINE），再刷新或下发命令。';
    }

    if (msg.includes('HTTP 401')) {
      return '鉴权失败：请检查 AK/SK、Base URL 是否为 IoTDA App Endpoint，并确保设备/项目 ID 正确（X-Sdk-Date 需 UTC）。';
    }

    if (msg.includes('HTTP 404')) {
      return '资源不存在：请检查 Project ID / Device ID 是否正确，且 baseUrl 区域 endpoint 匹配。';
    }

    if (msg.includes('HTTP 408') || msg.toLowerCase().includes('timeout')) {
      return '请求超时：网络不稳定或设备离线/重连中，稍后重试。';
    }

    if (msg.match(/HTTP\s+5\d\d/)) {
      return '服务端异常：请稍后重试。';
    }

    const code = msg.match(/IOTDA\.\d+/)?.[0];
    if (code) {
      return `IoTDA 返回错误码 ${code}：请检查设备在线状态、权限与参数。`;
    }

    return '';
  }

  private async restoreAppSettings(): Promise<void> {
    const saved = await loadAppSettings();

    if (typeof saved.saveCredentials === 'boolean') {
      this.saveCredentials = saved.saveCredentials;
    }
    if (this.saveCredentials) {
      if (saved.ak) this.ak = saved.ak;
      if (saved.sk) this.sk = saved.sk;
    }

    if (typeof saved.saveDeviceSecret === 'boolean') {
      this.saveDeviceSecret = saved.saveDeviceSecret;
    }
    if (this.saveDeviceSecret) {
      if (saved.deviceSecret) this.deviceSecret = saved.deviceSecret;
    }

    this.applyHardcodedDefaults();

    if (typeof saved.autoRefreshEnabled === 'boolean') {
      this.autoRefreshEnabled = saved.autoRefreshEnabled;
    }
    if (typeof saved.alarmNotifyEnabled === 'boolean') {
      this.alarmNotifyEnabled = saved.alarmNotifyEnabled;
    }
    if (typeof saved.distributedSyncEnabled === 'boolean') {
      this.distributedSyncEnabled = saved.distributedSyncEnabled;
    }
  }

  private async persistAppSettings(): Promise<void> {
    this.applyHardcodedDefaults();
    await saveAppSettings({
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
      deviceSecret: this.deviceSecret.trim(),
    });

    if (this.distributedSyncEnabled) {
      this.publishDistributedSettings().catch(() => {
        // ignore
      });
    }
  }

  private applyPendingWantParams(): void {
    const params = GlobalContext.consumePendingWantParams();
    if (!params) {
      return;
    }
    void params;
    this.applyHardcodedDefaults();
    this.setInfo('已收到流转参数；演示模式下连接参数固定为内置值');
  }

  private syncSessionSecrets(): void {
    this.applyHardcodedDefaults();
  }

  private applyHardcodedDefaults(): void {
    this.baseUrl = IOTDA_DEFAULTS.baseUrl;
    this.projectId = IOTDA_DEFAULTS.projectId;
    this.deviceId = IOTDA_DEFAULTS.deviceId;
    this.ak = IOTDA_DEFAULTS.ak;
    this.sk = IOTDA_DEFAULTS.sk;
    this.deviceSecret = IOTDA_DEFAULTS.deviceSecret;
  }

  private updateDistributedSyncSubscription(): void {
    if (this.distributedSyncEnabled) {
      this.startDistributedSyncSubscription().catch((e: Error) => {
        this.setError(e?.message ?? String(e));
      });
    } else {
      this.stopDistributedSyncSubscription();
    }
  }

  private stopDistributedSyncSubscription(): void {
    if (this.distributedUnsubscribe) {
      try {
        this.distributedUnsubscribe();
      } catch {
        // ignore
      }
      this.distributedUnsubscribe = null;
    }
  }

  private async startDistributedSyncSubscription(): Promise<void> {
    this.stopDistributedSyncSubscription();
    this.distributedUnsubscribe = await subscribeSharedSettingsChanged((s) => {
      if (!this.distributedSyncEnabled) {
        return;
      }
      this.autoRefreshEnabled = !!s.autoRefreshEnabled;
      this.alarmNotifyEnabled = !!s.alarmNotifyEnabled;
      this.applyHardcodedDefaults();
      this.updateAutoRefreshTimer();
      this.setInfo(`收到分布式同步更新（${s.updatedAt || 'unknown'}）`);
    });
  }

  private buildSharedSettingsPayload(): SharedSettingsPayload {
    return {
      updatedAt: this.formatLocalDateTime(new Date()),
      baseUrl: this.baseUrl.trim(),
      projectId: this.projectId.trim(),
      deviceId: this.deviceId.trim(),
      autoRefreshEnabled: this.autoRefreshEnabled,
      alarmNotifyEnabled: this.alarmNotifyEnabled,
    };
  }

  private async publishDistributedSettings(): Promise<void> {
    const payload = this.buildSharedSettingsPayload();
    if (!payload.baseUrl || !payload.projectId || !payload.deviceId) {
      throw new Error('分布式同步参数缺失，请检查内置配置');
    }
    await publishSharedSettings(payload);
    await this.showToast('已发布到分布式同步（同账号设备可拉取）');
  }

  private async pullDistributedSettings(): Promise<void> {
    const s = await loadSharedSettings();
    if (!s) {
      throw new Error('未找到分布式配置：请先在任意设备“发布到分布式”');
    }
    this.autoRefreshEnabled = !!s.autoRefreshEnabled;
    this.alarmNotifyEnabled = !!s.alarmNotifyEnabled;
    this.applyHardcodedDefaults();
    this.updateAutoRefreshTimer();
    await this.showToast(`已从分布式拉取配置（${s.updatedAt || 'unknown'}）`);
  }

  private async pickDistributedDevice(devices: DistributedDeviceInfo[]): Promise<DistributedDeviceInfo | null> {
    if (devices.length === 0) {
      return null;
    }

    const maxDevices = Math.min(devices.length, 5); // ActionMenu 最多 6 个按钮（含取消）
    const buttonsRaw: promptAction.Button[] = [];
    const cancelBtn: promptAction.Button = { text: '取消', color: '#666666' };
    buttonsRaw.push(cancelBtn);
    for (let i = 0; i < maxDevices; i += 1) {
      const d = devices[i];
      const btn: promptAction.Button = { text: d.deviceName || d.deviceId, color: '#0A7D3B' };
      buttonsRaw.push(btn);
    }
    const buttons = buttonsRaw as [
      promptAction.Button,
      promptAction.Button?,
      promptAction.Button?,
      promptAction.Button?,
      promptAction.Button?,
      promptAction.Button?,
    ];

    const menuOptions: promptAction.ActionMenuOptions = { title: '选择目标设备', buttons };
    const res: promptAction.ActionMenuSuccessResponse = await promptAction.showActionMenu(menuOptions);

    if (res.index <= 0) {
      return null;
    }
    const idx = res.index - 1;
    if (idx < 0 || idx >= maxDevices) {
      return null;
    }
    return devices[idx];
  }

  private async startDistributedFlow(): Promise<void> {
    const devices = await listTrustedDevices();
    if (devices.length === 0) {
      throw new Error('未发现可信设备：请先在系统中完成同账号设备互信/组网');
    }
    const picked = await this.pickDistributedDevice(devices);
    if (!picked) {
      return;
    }

    const payload = this.buildSharedSettingsPayload();
    if (!payload.baseUrl || !payload.projectId || !payload.deviceId) {
      throw new Error('流转参数缺失，请检查内置配置');
    }
    await startRemoteFlow(picked.deviceId, payload);
    await this.showToast(`已发起流转：${picked.deviceName || picked.deviceId}`);
  }

  private alarmText(level: number): string {
    if (level >= 2) return '严重(2)';
    if (level === 1) return '警告(1)';
    return '正常(0)';
  }

  private notifyAlarmIfChanged(prevLevel: number, nextLevel: number, snapshot: AquariumShadowSnapshot, source: 'REST' | 'REALTIME'): void {
    if (!this.alarmNotifyEnabled) {
      return;
    }
    if (prevLevel === nextLevel) {
      return;
    }

    const title = nextLevel >= 2 ? '水族箱严重告警' : nextLevel === 1 ? '水族箱告警' : '水族箱告警恢复';
    const p = snapshot.properties;
    const detail = `alarm=${this.alarmText(nextLevel)}  T=${(p.temperature ?? 0).toFixed(1)}°C  pH=${(p.ph ?? 0).toFixed(2)}  TDS=${(p.tds ?? 0).toFixed(0)}  Turb=${(p.turbidity ?? 0).toFixed(0)}  Level=${(p.water_level ?? 0).toFixed(0)}%`;
    const text = `${snapshot.device_id}（${source}）\\n${detail}`;

    void publishAlarmNotification({ title, text }).catch(() => {
      // ignore
    });
    void this.showToast(`${title}：${this.alarmText(nextLevel)}`);
  }

  private persistCardSnapshot(nowText: string, snapshot: AquariumShadowSnapshot, deviceStatus: string): void {
    const p = snapshot.properties;
    void saveCardSnapshot({
      updatedAt: nowText,
      deviceId: snapshot.device_id,
      deviceStatus: deviceStatus || 'UNKNOWN',
      alarmLevel: p.alarm_level ?? 0,
      temperature: p.temperature ?? 0,
      ph: p.ph ?? 0,
      tds: p.tds ?? 0,
      turbidity: p.turbidity ?? 0,
      waterLevel: p.water_level ?? 0,
    }).catch(() => {
      // ignore
    });
  }

  private statusText(status?: string): string {
    if (!status) return 'UNKNOWN';
    return status;
  }

  private formatLocalDateTime(date: Date = new Date()): string {
    const pad2 = (n: number) => String(n).padStart(2, '0');
    return `${date.getFullYear()}-${pad2(date.getMonth() + 1)}-${pad2(date.getDate())} ${pad2(date.getHours())}:${pad2(date.getMinutes())}:${pad2(date.getSeconds())}`;
  }

  private appendRefreshHistory(nowText: string, shadow: AquariumShadowSnapshot, deviceStatus: string): void {
    const p = shadow.properties;
    const item: RefreshHistoryItem = {
      at: nowText,
      device_status: deviceStatus || 'UNKNOWN',
      alarm_level: p.alarm_level ?? 0,
      temperature: p.temperature ?? 0,
      ph: p.ph ?? 0,
      tds: p.tds ?? 0,
      turbidity: p.turbidity ?? 0,
      water_level: p.water_level ?? 0,
    };

    this.refreshHistory = [item, ...this.refreshHistory].slice(0, this.refreshHistoryMaxSize);
  }

  private trendMetricLabel(key: TrendMetricKey): string {
    switch (key) {
      case 'temperature':
        return '温度';
      case 'ph':
        return 'pH';
      case 'tds':
        return 'TDS';
      case 'turbidity':
        return '浊度';
      case 'water_level':
        return '水位';
      default:
        return key;
    }
  }

  private trendMetricUnit(key: TrendMetricKey): string {
    switch (key) {
      case 'temperature':
        return '°C';
      case 'ph':
        return '';
      case 'tds':
        return 'ppm';
      case 'turbidity':
        return 'NTU';
      case 'water_level':
        return '%';
      default:
        return '';
    }
  }

  private trendMetricColor(key: TrendMetricKey): string {
    switch (key) {
      case 'temperature':
        return '#FF6B00';
      case 'ph':
        return '#2D9CDB';
      case 'tds':
        return '#9B51E0';
      case 'turbidity':
        return '#F2C94C';
      case 'water_level':
        return '#27AE60';
      default:
        return '#333333';
    }
  }

  private historyMetricValue(item: RefreshHistoryItem, metric: TrendMetricKey): number {
    switch (metric) {
      case 'temperature':
        return item.temperature;
      case 'ph':
        return item.ph;
      case 'tds':
        return item.tds;
      case 'turbidity':
        return item.turbidity;
      case 'water_level':
        return item.water_level;
      default:
        return 0;
    }
  }

  private buildTrendPath(metric: TrendMetricKey): TrendPath {
    const points = this.refreshHistory.slice(0, this.refreshHistoryMaxSize).reverse();
    const values: number[] = [];
    for (const item of points) {
      const v = this.historyMetricValue(item, metric);
      if (Number.isFinite(v)) {
        values.push(v);
      }
    }

    if (values.length < 2) {
      return { commands: '', min: 0, max: 0, count: values.length };
    }

    let min = Math.min(...values);
    let max = Math.max(...values);
    if (!Number.isFinite(min) || !Number.isFinite(max)) {
      return { commands: '', min: 0, max: 0, count: 0 };
    }
    if (min === max) {
      const pad = min === 0 ? 1 : Math.abs(min) * 0.1;
      min -= pad;
      max += pad;
    }

    const n = values.length;
    const coords: string[] = [];
    for (let i = 0; i < n; i += 1) {
      const x = (i / (n - 1)) * 100;
      const t = (values[i] - min) / (max - min);
      const y = 100 - Math.max(0, Math.min(1, t)) * 100;
      coords.push(`${x.toFixed(2)} ${y.toFixed(2)}`);
    }

    const commands = `M ${coords.join(' L ')}`;
    return { commands, min, max, count: n };
  }

  @Builder
  private TrendChartSection() {
    GlassCard() {
      Column({ space: 12 }) {
        this.SectionTitle(`趋势图 (${Math.min(this.refreshHistory.length, this.refreshHistoryMaxSize)}条)`)

        // 简单的 Chip 选择器
        Scroll() {
          Row({ space: 8 }) {
            ForEach(
              this.trendMetricKeys,
              (k: TrendMetricKey) => {
                Text(this.trendMetricLabel(k))
                  .fontSize(12)
                  .fontColor(this.trendMetric === k ? '#FFFFFF' : UiConstants.COLOR_TEXT_PRIMARY)
                  .fontWeight(this.trendMetric === k ? FontWeight.Bold : FontWeight.Normal)
                  .padding({ top: 6, bottom: 6, left: 12, right: 12 })
                  .backgroundColor(this.trendMetric === k ? this.trendMetricColor(k) : '#F0F0F0')
                  .borderRadius(14)
                  .onClick(() => {
                    this.trendMetric = k
                  })
                  .animation({ duration: 200 })
              },
              (k: TrendMetricKey) => k
            )
          }
        }
        .scrollable(ScrollDirection.Horizontal)
        .scrollBar(BarState.Off)
        .width('100%')

        if (this.buildTrendPath(this.trendMetric).count < 2 || !this.buildTrendPath(this.trendMetric).commands) {
          Column({ space: 8 }) {
            // 空态图示 (使用 Path 画一个淡淡的波浪)
            Path()
              .commands('M0 50 Q 25 20 50 50 T 100 50')
              .stroke('#E0E0E0')
              .strokeWidth(2)
              .fill('none')
              .width(60)
              .height(40)
            
            Text('暂无足够数据绘制趋势\n请等待自动刷新或手动操作')
              .fontSize(12)
              .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
              .textAlign(TextAlign.Center)
          }
          .width('100%')
          .height(160)
          .justifyContent(FlexAlign.Center)
          .backgroundColor('#FAFAFA')
          .borderRadius(16)
        } else {
          Column() {
            Row() {
              Text(`Min: ${this.buildTrendPath(this.trendMetric).min.toFixed(2)}${this.trendMetricUnit(this.trendMetric)}`)
                .fontSize(10)
                .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
              Blank()
              Text(`Max: ${this.buildTrendPath(this.trendMetric).max.toFixed(2)}${this.trendMetricUnit(this.trendMetric)}`)
                .fontSize(10)
                .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
            }
            .width('100%')
            .padding({ left: 4, right: 4, bottom: 4 })

            Stack() {
              // 背景网格线
              Column() {
                Divider().color('#F0F0F0').strokeWidth(1)
                Blank()
                Divider().color('#F0F0F0').strokeWidth(1)
                Blank()
                Divider().color('#F0F0F0').strokeWidth(1)
              }
              .width('100%')
              .height(160)

              // 趋势线
              Shape() {
                Path()
                  .commands(this.buildTrendPath(this.trendMetric).commands)
                  .stroke(this.trendMetricColor(this.trendMetric))
                  .strokeWidth(3)
                  .fill('none')
                  // 增加一点阴影让线条浮起来
                  .shadow({ radius: 4, color: this.trendMetricColor(this.trendMetric) + '40', offsetY: 4 })
              }
              .viewPort({ x: 0, y: 0, width: 100, height: 100 })
              .width('100%')
              .height(160)
            }
            .width('100%')
            .height(160)
          }
        }
      }
    }
  }

  private applyShadowToControlForm(shadow: AquariumShadowSnapshot): void {
    this.ctl_heater = shadow.properties.heater;
    this.ctl_pump_in = shadow.properties.pump_in;
    this.ctl_pump_out = shadow.properties.pump_out;
    this.ctl_auto_mode = shadow.properties.auto_mode;
    this.ctl_mute = shadow.properties.alarm_muted;
  }

  private parseNumber(input: string): number | undefined {
    const trimmed = input.trim();
    if (!trimmed) {
      return undefined;
    }
    const value = Number(trimmed);
    if (!Number.isFinite(value)) {
      return undefined;
    }
    return value;
  }

  private parseInt(input: string): number | undefined {
    const value = this.parseNumber(input);
    if (value === undefined) {
      return undefined;
    }
    return Math.trunc(value);
  }

  private asObject(value: JsonValue): JsonObject | null {
    if (!value || typeof value !== 'object') {
      return null;
    }
    return value as JsonObject;
  }

  private toNumberSafe(value: JsonValue): number | undefined {
    if (typeof value === 'number' && Number.isFinite(value)) {
      return value;
    }
    if (typeof value === 'string') {
      const parsed = Number(value);
      if (Number.isFinite(parsed)) {
        return parsed;
      }
    }
    return undefined;
  }

  private toBoolSafe(value: JsonValue): boolean | undefined {
    if (typeof value === 'boolean') {
      return value;
    }
    if (typeof value === 'string') {
      if (value.toLowerCase() === 'true') {
        return true;
      }
      if (value.toLowerCase() === 'false') {
        return false;
      }
    }
    return undefined;
  }

  private buildPropertiesFromPartial(partial: JsonObject): AquariumProperties {
    const prev = this.shadowSnapshot?.properties;
    return {
      temperature: this.toNumberSafe(partial.temperature) ?? prev?.temperature ?? 0,
      ph: this.toNumberSafe(partial.ph) ?? prev?.ph ?? 0,
      tds: this.toNumberSafe(partial.tds) ?? prev?.tds ?? 0,
      turbidity: this.toNumberSafe(partial.turbidity) ?? prev?.turbidity ?? 0,
      water_level: this.toNumberSafe(partial.water_level) ?? prev?.water_level ?? 0,
      heater: this.toBoolSafe(partial.heater) ?? prev?.heater ?? false,
      pump_in: this.toBoolSafe(partial.pump_in) ?? prev?.pump_in ?? false,
      pump_out: this.toBoolSafe(partial.pump_out) ?? prev?.pump_out ?? false,
      auto_mode: this.toBoolSafe(partial.auto_mode) ?? prev?.auto_mode ?? false,
      feed_countdown: this.toNumberSafe(partial.feed_countdown) ?? prev?.feed_countdown ?? 0,
      feeding_in_progress: this.toBoolSafe(partial.feeding_in_progress) ?? prev?.feeding_in_progress ?? false,
      alarm_level: this.toNumberSafe(partial.alarm_level) ?? prev?.alarm_level ?? 0,
      alarm_muted: this.toBoolSafe(partial.alarm_muted) ?? prev?.alarm_muted ?? false,
    };
  }

  private ensureRealtimeConfig(): boolean {
    this.syncSessionSecrets();
    this.applyHardcodedDefaults();
    if (!this.deviceId.trim() || !this.deviceSecret.trim()) {
      this.setError('实时订阅关键配置缺失，请检查 iotdaDefaults.ts');
      return false;
    }
    return true;
  }

  private realtimeTopic(): string {
    return `$oc/devices/${this.deviceId.trim()}/sys/properties/report`;
  }

  private updateRealtimeSubscription(): void {
    if (this.realtimeEnabled) {
      const sessionId = ++this.realtimeSessionId;
      this.startRealtimeSubscription(sessionId).catch((e: Error) => {
        this.realtimeState = 'ERROR';
        this.realtimeStateMessage = e?.message ?? String(e);
      });
    } else {
      this.stopRealtimeSubscription();
    }
  }

  private stopRealtimeSubscriptionInternal(): void {
    if (this.realtimeClient) {
      try {
        this.realtimeClient.stop();
      } catch {
        // ignore
      }
      this.realtimeClient = null;
    }
    this.realtimeState = 'DISCONNECTED';
    this.realtimeStateMessage = '';
  }

  private stopRealtimeSubscription(): void {
    this.realtimeSessionId += 1;
    this.stopRealtimeSubscriptionInternal();
  }

  private async startRealtimeSubscription(sessionId: number): Promise<void> {
    if (!this.ensureRealtimeConfig()) {
      this.realtimeEnabled = false;
      this.stopRealtimeSubscription();
      return;
    }
    if (!this.realtimeEnabled || sessionId !== this.realtimeSessionId) {
      return;
    }

    const deviceId = this.deviceId.trim();
    const auth = await buildIotdaMqttAuth({
      deviceId,
      deviceSecret: this.deviceSecret,
      fixedTimestamp: '2000010100',
    });
    if (!this.realtimeEnabled || sessionId !== this.realtimeSessionId) {
      return;
    }

    this.stopRealtimeSubscriptionInternal();

    this.realtimeClient = new RealtimeMqttClient({
      host: IOTDA_DEFAULTS.deviceMqttHost,
      port: IOTDA_DEFAULTS.deviceMqttPort,
      clientId: auth.clientId,
      username: auth.username,
      password: auth.password,
      topic: this.realtimeTopic(),
      keepAliveSec: 120,
      onStateChange: (state, message) => {
        this.realtimeState = state;
        this.realtimeStateMessage = message ?? '';
      },
      onPublish: (topic, payloadText) => {
        if (!this.realtimeEnabled) {
          return;
        }
        if (sessionId !== this.realtimeSessionId) {
          return;
        }
        if (topic !== this.realtimeTopic()) {
          return;
        }
        this.applyRealtimeReport(payloadText);
      },
    });

    this.realtimeClient.start();
  }

  private applyRealtimeReport(payloadText: string): void {
    let root: JsonObject | null = null;
    try {
      root = this.asObject(JSON.parse(payloadText) as JsonValue);
    } catch {
      return;
    }
    if (!root) {
      return;
    }

    const servicesVal = root.services;
    if (!Array.isArray(servicesVal) || servicesVal.length === 0) {
      return;
    }

    const services = servicesVal as JsonValue[];
    const serviceObj = services
      .map((s: JsonValue) => this.asObject(s))
      .find((s: JsonObject | null) => !!this.asObject(s?.properties));
    if (!serviceObj) {
      return;
    }

    const propsObj = this.asObject(serviceObj.properties);
    if (!propsObj) {
      return;
    }

    const nowText = this.formatLocalDateTime(new Date());
    const snapshot: AquariumShadowSnapshot = {
      device_id: this.deviceId.trim(),
      event_time: typeof serviceObj.event_time === 'string' ? serviceObj.event_time : undefined,
      properties: this.buildPropertiesFromPartial(propsObj),
    };

    const prevAlarm = this.shadowSnapshot?.properties.alarm_level ?? 0;
    this.shadowSnapshot = snapshot;
    this.applyShadowToControlForm(snapshot);
    this.lastRefreshAt = nowText;
    this.lastRefreshResult = 'REALTIME';
    this.appendRefreshHistory(nowText, snapshot, 'ONLINE');
    this.persistCardSnapshot(nowText, snapshot, 'ONLINE');
    this.notifyAlarmIfChanged(prevAlarm, snapshot.properties.alarm_level ?? 0, snapshot, 'REALTIME');
  }

  private hasBasicConfig(): boolean {
    this.syncSessionSecrets();
    this.applyHardcodedDefaults();
    return !!(
      this.baseUrl.trim() &&
      this.projectId.trim() &&
      this.deviceId.trim() &&
      this.ak.trim() &&
      this.sk.trim()
    );
  }

  private ensureBasicConfig(): boolean {
    this.syncSessionSecrets();
    this.applyHardcodedDefaults();
    if (!this.baseUrl.trim() || !this.projectId.trim() || !this.deviceId.trim() || !this.ak.trim() || !this.sk.trim()) {
      this.setError('连接关键配置缺失，请检查 iotdaDefaults.ts');
      return false;
    }
    return true;
  }

  private updateAutoRefreshTimer(): void {
    if (this.autoRefreshEnabled) {
      this.startAutoRefreshTimer();
    } else {
      this.stopAutoRefreshTimer();
    }
  }

  private startAutoRefreshTimer(): void {
    this.stopAutoRefreshTimer();
    this.autoRefreshTimerId = setInterval(() => {
      if (!this.autoRefreshEnabled) {
        return;
      }
      if (this.isLoading) {
        return;
      }
      if (!this.hasBasicConfig()) {
        return;
      }
      this.refreshShadowAndStatus({ showNotice: false, resetResponses: false }).catch(() => {
        // 自动刷新失败时不打断用户操作（下一轮会重试）
      });
    }, this.autoRefreshIntervalMs) as number;
  }

  private stopAutoRefreshTimer(): void {
    if (this.autoRefreshTimerId === undefined) {
      return;
    }
    clearInterval(this.autoRefreshTimerId);
    this.autoRefreshTimerId = undefined;
  }

  private async refreshShadowAndStatus(options?: RefreshShadowOptions): Promise<void> {
    const showNotice = options?.showNotice ?? true;
    const resetResponses = options?.resetResponses ?? false;

    if (showNotice) {
      this.clearNotice();
    }
    if (resetResponses) {
      this.controlResp = null;
      this.thresholdResp = null;
      this.configResp = null;
    }

    if (!this.hasBasicConfig()) {
      if (showNotice) {
        this.ensureBasicConfig();
      }
      return;
    }

    this.isLoading = true;
    try {
      const client = this.buildClient();
      const settled = await Promise.allSettled([client.getDeviceShadow(), client.getDeviceInfo()]);
      const shadowRes = settled[0];
      const infoRes = settled[1];

      const nowText = this.formatLocalDateTime(new Date());

      if (shadowRes.status === 'fulfilled') {
        const prevAlarm = this.shadowSnapshot?.properties.alarm_level ?? 0;
        this.shadowSnapshot = shadowRes.value;
        this.applyShadowToControlForm(shadowRes.value);

        if (infoRes.status === 'fulfilled') {
          this.lastRefreshResult = 'SUCCESS';
        } else {
          this.lastRefreshResult = 'PARTIAL';
        }
        this.lastRefreshAt = nowText;

        const deviceStatus =
          infoRes.status === 'fulfilled' ? (infoRes.value.status ?? 'UNKNOWN') : 'UNKNOWN';
        this.appendRefreshHistory(nowText, shadowRes.value, deviceStatus);
        this.persistCardSnapshot(nowText, shadowRes.value, deviceStatus);
        this.notifyAlarmIfChanged(prevAlarm, shadowRes.value.properties.alarm_level ?? 0, shadowRes.value, 'REST');
      } else if (showNotice) {
        this.shadowSnapshot = null;
        this.lastRefreshResult = 'FAILED';
        this.setError(
          shadowRes.reason?.message ?? String(shadowRes.reason ?? '影子查询失败')
        );
      } else {
        this.lastRefreshResult = 'FAILED';
      }

      if (infoRes.status === 'fulfilled') {
        this.deviceInfo = infoRes.value;
      } else if (showNotice) {
        this.deviceInfo = null;
      }

      if (showNotice && !this.noticeIsError) {
        this.setInfo('刷新完成');
      }
    } catch (e) {
      this.lastRefreshResult = 'FAILED';
      if (showNotice) {
        this.shadowSnapshot = null;
        this.deviceInfo = null;
        this.setError(e?.message ?? String(e));
      }
    } finally {
      this.isLoading = false;
    }
  }

  private async autoRefreshAfterCommand(delayMs: number = 1500): Promise<void> {
    await new Promise<void>((resolve) => setTimeout(resolve, delayMs));
    if (this.isLoading) {
      return;
    }
    if (!this.hasBasicConfig()) {
      return;
    }
    await this.refreshShadowAndStatus({ showNotice: false, resetResponses: false });
  }

  private inRange(value: number, min: number, max: number): boolean {
    return value >= min && value <= max;
  }

  private async confirmWifiChange(): Promise<boolean> {
    try {
      const buttons: [promptAction.Button, promptAction.Button] = [
        { text: '取消', color: '#666666' },
        { text: '继续', color: '#0A7D3B' },
      ];
      const res: promptAction.ShowDialogSuccessResponse = await promptAction.showDialog({
        title: '确认 WiFi 变更',
        message: '下发 WiFi 配置后，设备会短暂离线并重连（属正常现象）。是否继续？',
        buttons,
      });
      return res.index === 1;
    } catch {
      return false;
    }
  }

  private async showToast(message: string): Promise<void> {
    try {
      promptAction.showToast({ message, duration: 2000 });
    } catch {
      // ignore
    }
  }

  private async copyTextToClipboard(text: string): Promise<void> {
    const systemPb = pasteboard.getSystemPasteboard();
    const data = pasteboard.createPlainTextData(text);
    await systemPb.setPasteData(data);
  }

  private tryParseJson(text: string): JsonValue {
    if (!text) {
      return null;
    }
    try {
      return JSON.parse(text) as JsonValue;
    } catch {
      return text;
    }
  }

  private buildExperimentRecord(): ExperimentRecord {
    return {
      export_time: this.formatLocalDateTime(new Date()),
      device: {
        baseUrl: this.baseUrl.trim(),
        projectId: this.projectId.trim(),
        deviceId: this.deviceId.trim(),
      },
      refresh: {
        last_at: this.lastRefreshAt,
        last_result: this.lastRefreshResult,
      },
      history: this.refreshHistory,
      status: this.deviceInfo,
      shadow: this.shadowSnapshot,
      last_command: {
        at: this.lastCommandAt,
        name: this.lastCommandName,
        result: this.lastCommandResult,
        payload: this.tryParseJson(this.lastCommandPayload),
        response: this.tryParseJson(this.lastCommandResponse),
      },
    };
  }

  private async exportExperimentRecord(): Promise<void> {
    const record = this.buildExperimentRecord();
    const jsonText = JSON.stringify(record, null, 2);
    this.exportedExperimentRecord = jsonText;

    try {
      await this.copyTextToClipboard(jsonText);
      this.setInfo('实验记录已导出并复制到剪贴板');
      await this.showToast('已复制实验记录 JSON');
    } catch (e) {
      this.setError(e?.message ?? String(e));
    }
  }

  private parseAndValidateThresholdParas(): ParseResult<ThresholdCommandParas> {
    const tempMin = this.parseNumber(this.th_temp_min);
    const tempMax = this.parseNumber(this.th_temp_max);
    const phMin = this.parseNumber(this.th_ph_min);
    const phMax = this.parseNumber(this.th_ph_max);
    const tdsWarn = this.parseInt(this.th_tds_warn);
    const tdsCritical = this.parseInt(this.th_tds_critical);
    const turbWarn = this.parseInt(this.th_turbidity_warn);
    const turbCritical = this.parseInt(this.th_turbidity_critical);
    const levelMin = this.parseInt(this.th_level_min);
    const levelMax = this.parseInt(this.th_level_max);
    const feedInterval = this.parseInt(this.th_feed_interval);
    const feedAmount = this.parseInt(this.th_feed_amount);

    if (
      tempMin === undefined ||
      tempMax === undefined ||
      phMin === undefined ||
      phMax === undefined ||
      tdsWarn === undefined ||
      tdsCritical === undefined ||
      turbWarn === undefined ||
      turbCritical === undefined ||
      levelMin === undefined ||
      levelMax === undefined ||
      feedInterval === undefined ||
      feedAmount === undefined
    ) {
      return { ok: false, message: '阈值表单存在无效输入，请检查所有字段是否为数字' };
    }

    const TEMP_PHYS_MIN = -55;
    const TEMP_PHYS_MAX = 125;
    const PH_PHYS_MIN = 0;
    const PH_PHYS_MAX = 14;
    const TDS_PHYS_MIN = 0;
    const TDS_PHYS_MAX = 5000;
    const TURB_PHYS_MIN = 0;
    const TURB_PHYS_MAX = 3000;
    const LEVEL_PHYS_MIN = 0;
    const LEVEL_PHYS_MAX = 100;

    if (!this.inRange(tempMin, TEMP_PHYS_MIN, TEMP_PHYS_MAX) || !this.inRange(tempMax, TEMP_PHYS_MIN, TEMP_PHYS_MAX)) {
      return { ok: false, message: `温度阈值范围需在 ${TEMP_PHYS_MIN}~${TEMP_PHYS_MAX}°C` };
    }
    if (tempMin >= tempMax) {
      return { ok: false, message: '温度上下限需满足 temp_min < temp_max' };
    }

    if (!this.inRange(phMin, PH_PHYS_MIN, PH_PHYS_MAX) || !this.inRange(phMax, PH_PHYS_MIN, PH_PHYS_MAX)) {
      return { ok: false, message: `pH 阈值范围需在 ${PH_PHYS_MIN}~${PH_PHYS_MAX}` };
    }
    if (phMin >= phMax) {
      return { ok: false, message: 'pH 上下限需满足 ph_min < ph_max' };
    }

    if (!this.inRange(tdsWarn, TDS_PHYS_MIN, TDS_PHYS_MAX) || !this.inRange(tdsCritical, TDS_PHYS_MIN, TDS_PHYS_MAX)) {
      return { ok: false, message: `TDS 阈值范围需在 ${TDS_PHYS_MIN}~${TDS_PHYS_MAX}` };
    }
    if (tdsWarn >= tdsCritical) {
      return { ok: false, message: 'TDS 阈值需满足 tds_warn < tds_critical' };
    }

    if (!this.inRange(turbWarn, TURB_PHYS_MIN, TURB_PHYS_MAX) || !this.inRange(turbCritical, TURB_PHYS_MIN, TURB_PHYS_MAX)) {
      return { ok: false, message: `浊度阈值范围需在 ${TURB_PHYS_MIN}~${TURB_PHYS_MAX}` };
    }
    if (turbWarn >= turbCritical) {
      return { ok: false, message: '浊度阈值需满足 turbidity_warn < turbidity_critical' };
    }

    if (!this.inRange(levelMin, LEVEL_PHYS_MIN, LEVEL_PHYS_MAX) || !this.inRange(levelMax, LEVEL_PHYS_MIN, LEVEL_PHYS_MAX)) {
      return { ok: false, message: `水位阈值范围需在 ${LEVEL_PHYS_MIN}~${LEVEL_PHYS_MAX}` };
    }
    if (levelMin >= levelMax) {
      return { ok: false, message: '水位上下限需满足 level_min < level_max' };
    }

    if (!this.inRange(feedInterval, 1, 168)) {
      return { ok: false, message: 'feed_interval(小时) 建议范围 1~168' };
    }
    if (!this.inRange(feedAmount, 1, 10)) {
      return { ok: false, message: 'feed_amount(档位) 建议范围 1~10' };
    }

    const paras: ThresholdCommandParas = {
      temp_min: tempMin,
      temp_max: tempMax,
      ph_min: phMin,
      ph_max: phMax,
      tds_warn: tdsWarn,
      tds_critical: tdsCritical,
      turbidity_warn: turbWarn,
      turbidity_critical: turbCritical,
      level_min: levelMin,
      level_max: levelMax,
      feed_interval: feedInterval,
      feed_amount: feedAmount,
    };

    return { ok: true, paras };
  }

  private parseAndValidateConfigParas(): ParseResult<ConfigCommandParas> {
    const paras: ConfigCommandParas = {};

    const wifiSsid = this.cfg_wifi_ssid.trim();
    const wifiPasswordTrimmed = this.cfg_wifi_password.trim();
    const hasWifiInput = !!(wifiSsid || wifiPasswordTrimmed);

    if ((wifiSsid && !wifiPasswordTrimmed) || (!wifiSsid && wifiPasswordTrimmed)) {
      return { ok: false, message: 'WiFi 变更需 SSID+PWD 成对提交' };
    }
    if (wifiSsid.length > 32) {
      return { ok: false, message: 'wifi_ssid 长度不能超过 32' };
    }
    if (wifiPasswordTrimmed.length > 64) {
      return { ok: false, message: 'wifi_password 长度不能超过 64' };
    }
    if (wifiSsid && wifiPasswordTrimmed) {
      paras.wifi_ssid = wifiSsid;
      paras.wifi_password = wifiPasswordTrimmed;
    }

    const phOffsetText = this.cfg_ph_offset.trim();
    const tdsFactorText = this.cfg_tds_factor.trim();

    if (phOffsetText) {
      const phOffset = this.parseNumber(phOffsetText);
      if (phOffset === undefined) {
        return { ok: false, message: 'ph_offset 请输入有效数字' };
      }
      if (!this.inRange(phOffset, -5, 5)) {
        return { ok: false, message: 'ph_offset 建议范围 -5~5' };
      }
      paras.ph_offset = phOffset;
    }

    if (tdsFactorText) {
      const tdsFactor = this.parseNumber(tdsFactorText);
      if (tdsFactor === undefined) {
        return { ok: false, message: 'tds_factor 请输入有效数字' };
      }
      if (!this.inRange(tdsFactor, 0.1, 10)) {
        return { ok: false, message: 'tds_factor 建议范围 0.1~10' };
      }
      paras.tds_factor = tdsFactor;
    }

    if (Object.keys(paras).length === 0 && !hasWifiInput) {
      return { ok: false, message: '请至少填写一个配置项（WiFi 或校准参数）' };
    }

    return { ok: true, paras };
  }

  private thresholdFormOk(): boolean {
    return this.parseAndValidateThresholdParas().ok;
  }

  private thresholdFormMsg(): string {
    const result = this.parseAndValidateThresholdParas();
    if (result.ok === false) {
      return result.message;
    }
    return '';
  }

  private configFormOk(): boolean {
    return this.parseAndValidateConfigParas().ok;
  }

  private configFormMsg(): string {
    const result = this.parseAndValidateConfigParas();
    if (result.ok === false) {
      return result.message;
    }
    return '';
  }

  private hasAnyConfigInput(): boolean {
    return !!(
      this.cfg_wifi_ssid.trim() ||
      this.cfg_wifi_password.trim() ||
      this.cfg_ph_offset.trim() ||
      this.cfg_tds_factor.trim()
    );
  }

  private async sendControlAll(): Promise<void> {
    this.clearNotice();
    this.controlResp = null;

    if (!this.ensureBasicConfig()) {
      return;
    }

    const targetTemp = this.parseNumber(this.ctl_target_temp);
    if (targetTemp === undefined) {
      this.setError('target_temp 请输入有效数字，例如 26 或 26.5');
      return;
    }

    const paras: ControlCommandParas = {
      heater: this.ctl_heater,
      pump_in: this.ctl_pump_in,
      pump_out: this.ctl_pump_out,
      mute: this.ctl_mute,
      auto_mode: this.ctl_auto_mode,
      feed: false,
      target_temp: targetTemp,
    };

    this.lastCommandAt = this.formatLocalDateTime(new Date());
    this.lastCommandName = 'control';
    this.lastCommandResult = 'PENDING';
    const recordPayload: CommandRecordPayloadControl = { command: 'control', paras };
    this.lastCommandPayload = JSON.stringify(recordPayload);
    this.lastCommandResponse = '';

    let shouldAutoRefresh = false;
    this.isLoading = true;
    try {
      const client = this.buildClient();
      this.controlResp = await client.sendControl(paras);
      shouldAutoRefresh = this.controlResp.response?.result_code === 0;
      this.lastCommandResult = shouldAutoRefresh ? 'SUCCESS' : 'FAILED';
      this.lastCommandResponse = JSON.stringify(this.controlResp);
      this.setInfo('control 命令已返回');
    } catch (e) {
      this.lastCommandResult = 'ERROR';
      const payload: ErrorRecordPayload = { error: e?.message ?? String(e) };
      this.lastCommandResponse = JSON.stringify(payload);
      this.setError(e?.message ?? String(e));
    } finally {
      this.isLoading = false;
    }

    if (shouldAutoRefresh) {
      this.autoRefreshAfterCommand(1500).catch(() => {
        // ignore
      });
    }
  }

  private async sendFeedOnce(): Promise<void> {
    this.clearNotice();
    this.controlResp = null;

    if (!this.ensureBasicConfig()) {
      return;
    }

    this.lastCommandAt = this.formatLocalDateTime(new Date());
    this.lastCommandName = 'control(feed=true)';
    this.lastCommandResult = 'PENDING';
    const feedParas: ControlCommandParas = { feed: true };
    const feedRecord: CommandRecordPayloadControl = { command: 'control', paras: feedParas };
    this.lastCommandPayload = JSON.stringify(feedRecord);
    this.lastCommandResponse = '';

    let shouldAutoRefresh = false;
    this.isLoading = true;
    try {
      const client = this.buildClient();
      this.controlResp = await client.sendControl(feedParas);
      shouldAutoRefresh = this.controlResp.response?.result_code === 0;
      this.lastCommandResult = shouldAutoRefresh ? 'SUCCESS' : 'FAILED';
      this.lastCommandResponse = JSON.stringify(this.controlResp);
      this.setInfo('投喂命令已返回');
    } catch (e) {
      this.lastCommandResult = 'ERROR';
      const payload: ErrorRecordPayload = { error: e?.message ?? String(e) };
      this.lastCommandResponse = JSON.stringify(payload);
      this.setError(e?.message ?? String(e));
    } finally {
      this.isLoading = false;
    }

    if (shouldAutoRefresh) {
      this.autoRefreshAfterCommand(1500).catch(() => {
        // ignore
      });
    }
  }

  private async sendFeedOnceDelay(): Promise<void> {
    this.clearNotice();
    this.controlResp = null;

    if (!this.ensureBasicConfig()) {
      return;
    }

    const delaySec = this.parseInt(this.ctl_feed_once_delay);
    if (delaySec === undefined || delaySec <= 0) {
      this.setError('一次性倒计时请输入有效正整数（秒）');
      return;
    }
    if (!this.inRange(delaySec, 1, 86400)) {
      this.setError('一次性倒计时建议范围 1~86400 秒');
      return;
    }

    const feedParas: ControlCommandParas = { feed_once_delay: delaySec };
    this.lastCommandAt = this.formatLocalDateTime(new Date());
    this.lastCommandName = `control(feed_once_delay=${delaySec})`;
    this.lastCommandResult = 'PENDING';
    const feedRecord: CommandRecordPayloadControl = { command: 'control', paras: feedParas };
    this.lastCommandPayload = JSON.stringify(feedRecord);
    this.lastCommandResponse = '';

    let shouldAutoRefresh = false;
    this.isLoading = true;
    try {
      const client = this.buildClient();
      this.controlResp = await client.sendControl(feedParas);
      shouldAutoRefresh = this.controlResp.response?.result_code === 0;
      this.lastCommandResult = shouldAutoRefresh ? 'SUCCESS' : 'FAILED';
      this.lastCommandResponse = JSON.stringify(this.controlResp);
      this.setInfo('一次性倒计时投喂已提交');
    } catch (e) {
      this.lastCommandResult = 'ERROR';
      const payload: ErrorRecordPayload = { error: e?.message ?? String(e) };
      this.lastCommandResponse = JSON.stringify(payload);
      this.setError(e?.message ?? String(e));
    } finally {
      this.isLoading = false;
    }

    if (shouldAutoRefresh) {
      this.autoRefreshAfterCommand(1500).catch(() => {
        // ignore
      });
    }
  }

  private async sendSetThresholds(): Promise<void> {
    this.clearNotice();
    this.thresholdResp = null;

    if (!this.ensureBasicConfig()) {
      return;
    }

    const parsed = this.parseAndValidateThresholdParas();
    if (parsed.ok === false) {
      this.setError(parsed.message);
      return;
    }
    const paras: ThresholdCommandParas = parsed.paras;

    this.lastCommandAt = this.formatLocalDateTime(new Date());
    this.lastCommandName = 'set_thresholds';
    this.lastCommandResult = 'PENDING';
    const recordPayload: CommandRecordPayloadThreshold = { command: 'set_thresholds', paras };
    this.lastCommandPayload = JSON.stringify(recordPayload);
    this.lastCommandResponse = '';

    this.isLoading = true;
    try {
      const client = this.buildClient();
      this.thresholdResp = await client.sendSetThresholdsAsync(paras);
      const ok = !!this.thresholdResp.command_id && !this.thresholdResp.error_code;
      this.lastCommandResult = ok ? 'SUBMITTED' : 'FAILED';
      this.lastCommandResponse = JSON.stringify(this.thresholdResp);
      this.setInfo('set_thresholds 已提交（async）');
    } catch (e) {
      this.lastCommandResult = 'ERROR';
      const payload: ErrorRecordPayload = { error: e?.message ?? String(e) };
      this.lastCommandResponse = JSON.stringify(payload);
      this.setError(e?.message ?? String(e));
    } finally {
      this.isLoading = false;
    }
  }

  private async sendSetConfig(): Promise<void> {
    this.clearNotice();
    this.configResp = null;

    if (!this.ensureBasicConfig()) {
      return;
    }

    const parsed = this.parseAndValidateConfigParas();
    if (parsed.ok === false) {
      this.setError(parsed.message);
      return;
    }
    const paras: ConfigCommandParas = parsed.paras;

    if (paras.wifi_ssid && paras.wifi_password) {
      if (this.isWifiConfirming) {
        return;
      }
      this.isWifiConfirming = true;
      const confirmed = await this.confirmWifiChange();
      this.isWifiConfirming = false;
      if (!confirmed) {
        this.setInfo('已取消下发 WiFi 变更');
        return;
      }
    }

    const recordParas: JsonObject = {};
    if (paras.wifi_ssid) recordParas.wifi_ssid = paras.wifi_ssid;
    if (paras.wifi_password) recordParas.wifi_password = '***';
    if (paras.ph_offset !== undefined) recordParas.ph_offset = paras.ph_offset;
    if (paras.tds_factor !== undefined) recordParas.tds_factor = paras.tds_factor;

    this.lastCommandAt = this.formatLocalDateTime(new Date());
    this.lastCommandName = 'set_config';
    this.lastCommandResult = 'PENDING';
    const recordPayload: CommandRecordPayloadConfig = { command: 'set_config', paras: recordParas };
    this.lastCommandPayload = JSON.stringify(recordPayload);
    this.lastCommandResponse = '';

    let shouldAutoRefresh = false;
    this.isLoading = true;
    try {
      const client = this.buildClient();
      this.configResp = await client.sendSetConfig(paras);
      shouldAutoRefresh = this.configResp.response?.result_code === 0;
      this.lastCommandResult = shouldAutoRefresh ? 'SUCCESS' : 'FAILED';
      this.lastCommandResponse = JSON.stringify(this.configResp);
      this.setInfo('set_config 命令已返回');
    } catch (e) {
      this.lastCommandResult = 'ERROR';
      const payload: ErrorRecordPayload = { error: e?.message ?? String(e) };
      this.lastCommandResponse = JSON.stringify(payload);
      this.setError(e?.message ?? String(e));
    } finally {
      this.isLoading = false;
    }

    if (shouldAutoRefresh) {
      this.autoRefreshAfterCommand(1500).catch(() => {
        // ignore
      });
    }
  }

  @Builder
  private InfoRow(label: string, value: string) {
    Row() {
      Text(label)
        .fontSize(14)
        .fontColor('#666666')
        .layoutWeight(1);
      Text(value)
        .fontSize(14)
        .fontColor('#111111')
        .fontWeight(FontWeight.Medium)
        .textAlign(TextAlign.End)
        .layoutWeight(1);
    }
    .width('100%')
    .padding({ top: 12, bottom: 12 })
    .border({ width: { bottom: 1 }, color: '#F5F5F5' });
  }

  @Builder
  private SectionTitle(title: string) {
    Row({ space: 8 }) {
      Rect()
        .width(6)
        .height(18)
        .fill(UiConstants.COLOR_PRIMARY)
        .radius(3)
        .shadow({ radius: 4, color: '#4D0A7D3B', offsetY: 2 })
      
      Text(title)
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
        .shadow({ radius: 10, color: '#FFFFFF', offsetY: 1 }) // 文字发光提升对比度
    }
    .margin({ top: 20, bottom: 12 })
    .width('100%')
    .alignItems(VerticalAlign.Center)
  }

  private formatNumber(value: number | undefined, fractionDigits: number): string {
    if (typeof value !== 'number' || !Number.isFinite(value)) {
      return '-';
    }
    return value.toFixed(fractionDigits);
  }

  @Builder
  private MetricCard(label: string, value: string, unit: string) {
    GlassCard() {
      Column({ space: 8 }) {
        Row() {
          Text(label)
            .fontSize(14)
            .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
            .fontWeight(FontWeight.Medium)
          Blank()
          // 小圆点装饰
          Circle({ width: 6, height: 6 })
            .fill(UiConstants.COLOR_ACCENT)
            .opacity(0.5)
        }
        .width('100%')

        Row({ space: 4 }) {
          if (Number.isFinite(Number(value))) {
            CountUpText({
              targetValue: Number(value),
              suffix: unit,
              fontSize: 28,
              fontColor: UiConstants.COLOR_TEXT_PRIMARY,
              fractionDigits: value.includes('.') ? 2 : 0
            })
          } else {
             Text(value)
              .fontSize(22)
              .fontWeight(FontWeight.Bold)
              .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
             if (unit) {
               Text(unit)
                 .fontSize(12)
                 .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
                 .margin({ bottom: 4 })
                 .alignSelf(ItemAlign.End)
             }
          }
        }
        .width('100%')
        .alignItems(VerticalAlign.Bottom)
      }
    }
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
  private NumberInputRow(label: string, value: string, placeholder: string, onChange: (v: string) => void) {
    Column({ space: 8 }) {
      Text(label)
        .fontSize(14)
        .fontColor('#333333')
        .fontWeight(FontWeight.Medium);
      TextInput({ text: value, placeholder })
        .type(InputType.Number)
        .onChange(onChange)
        .height(48)
        .backgroundColor('#F5F7FA')
        .borderRadius(12)
        .padding({ left: 16, right: 16 })
        .width('100%');
    }
    .width('100%')
    .padding({ top: 8, bottom: 8 });
  }

  @Builder
  private TextInputRow(label: string, value: string, placeholder: string, onChange: (v: string) => void) {
    Column({ space: 8 }) {
      Text(label)
        .fontSize(14)
        .fontColor('#333333')
        .fontWeight(FontWeight.Medium);
      TextInput({ text: value, placeholder })
        .onChange(onChange)
        .height(48)
        .backgroundColor('#F5F7FA')
        .borderRadius(12)
        .padding({ left: 16, right: 16 })
        .width('100%');
    }
    .width('100%')
    .padding({ top: 8, bottom: 8 });
  }

  @Builder
  private PasswordInputRow(label: string, value: string, placeholder: string, onChange: (v: string) => void) {
    Column({ space: 8 }) {
      Text(label)
        .fontSize(14)
        .fontColor('#333333')
        .fontWeight(FontWeight.Medium);
      TextInput({ text: value, placeholder })
        .type(InputType.Password)
        .onChange(onChange)
        .height(48)
        .backgroundColor('#F5F7FA')
        .borderRadius(12)
        .padding({ left: 16, right: 16 })
        .width('100%');
    }
    .width('100%')
    .padding({ top: 8, bottom: 8 });
  }

  @Builder
  private DashboardTab() {
    Scroll() {
      Column({ space: 16 }) {
        // 设备状态卡片
        GlassCard() {
          Column() {
            this.SectionTitle('设备状态')
            this.InfoRow('在线状态', this.statusText(this.deviceInfo?.status))
            this.InfoRow('设备ID', this.shadowSnapshot?.device_id || this.deviceId)
            this.InfoRow('更新时间', this.shadowSnapshot?.event_time || '-')
            this.InfoRow('最后刷新', `${this.lastRefreshAt}`) // 简化显示
          }
        }

        // 水质数据 - MetricCard 已经自带 GlassCard，所以直接排列即可
        Column() {
          this.SectionTitle('水质概览')
          
          Grid() {
            GridItem() { this.MetricCard('温度', this.formatNumber(this.shadowSnapshot?.properties.temperature, 2), '°C') }
            GridItem() { this.MetricCard('pH', this.formatNumber(this.shadowSnapshot?.properties.ph, 2), '') }
            GridItem() { this.MetricCard('TDS', this.formatNumber(this.shadowSnapshot?.properties.tds, 0), 'ppm') }
            GridItem() { this.MetricCard('浊度', this.formatNumber(this.shadowSnapshot?.properties.turbidity, 0), 'NTU') }
            GridItem() { this.MetricCard('水位', this.formatNumber(this.shadowSnapshot?.properties.water_level, 0), '%') }
            GridItem() { this.MetricCard('告警', this.alarmText(this.shadowSnapshot?.properties.alarm_level ?? 0), '') }
          }
          .columnsTemplate('1fr 1fr')
          .rowsGap(12)
          .columnsGap(12)
        }

        // 实验记录与趋势
        GlassCard() {
          Column({ space: 12 }) {
            this.SectionTitle('实验记录')
            this.InfoRow('最近命令', `${this.lastCommandName} (${this.lastCommandResult})`)
            
            AnimButton({ text: this.isLoading ? '请求中...' : '导出实验记录', primary: false, isEnabled: !this.isLoading })
              .onClick(() => {
                this.exportExperimentRecord().catch((e: Error) => this.setError(e?.message ?? String(e)));
              })
            
            if (this.exportedExperimentRecord) {
              Text('已复制到剪贴板')
                .fontSize(12)
                .fontColor(UiConstants.COLOR_PRIMARY)
                .alignSelf(ItemAlign.Center)
            }
          }
        }

        this.TrendChartSection()

        // 更多详情
        GlassCard() {
          Column() {
            this.SectionTitle('更多详情')
            this.InfoRow('加热棒', (this.shadowSnapshot?.properties.heater ?? false) ? 'ON' : 'OFF')
            this.InfoRow('进水泵', (this.shadowSnapshot?.properties.pump_in ?? false) ? 'ON' : 'OFF')
            this.InfoRow('排水泵', (this.shadowSnapshot?.properties.pump_out ?? false) ? 'ON' : 'OFF')
            this.InfoRow('自动模式', (this.shadowSnapshot?.properties.auto_mode ?? false) ? '是' : '否')
          }
        }
      }
      .padding({ top: 12, bottom: 80, left: 12, right: 12 })
    }
    .scrollBar(BarState.Off)
    .edgeEffect(EdgeEffect.Spring) // 弹性滚动
  }

  @Builder
  private ControlTab() {
    Scroll() {
      Column({ space: 16 }) {
        GlassCard() {
          Column({ space: 8 }) {
            this.SectionTitle('设备控制')
            this.ToggleRow('加热棒 (Heater)', this.ctl_heater, (v) => (this.ctl_heater = v))
            this.ToggleRow('进水泵 (Pump In)', this.ctl_pump_in, (v) => (this.ctl_pump_in = v))
            this.ToggleRow('排水泵 (Pump Out)', this.ctl_pump_out, (v) => (this.ctl_pump_out = v))
            this.ToggleRow('自动模式 (Auto)', this.ctl_auto_mode, (v) => (this.ctl_auto_mode = v))
            this.ToggleRow('静音 (Mute)', this.ctl_mute, (v) => (this.ctl_mute = v))
            this.NumberInputRow('目标温度 (°C)', this.ctl_target_temp, '例如 26.0', (v) => (this.ctl_target_temp = v))
            this.NumberInputRow('一次性倒计时 (秒)', this.ctl_feed_once_delay, '例如 60', (v) => (this.ctl_feed_once_delay = v))
          }
        }

        Column({ space: 12 }) {
          AnimButton({ text: this.isLoading ? '发送中...' : '下发控制指令', primary: true, isEnabled: !this.isLoading })
            .onClick(() => {
              this.sendControlAll().catch((e: Error) => this.setError(e?.message ?? String(e)));
            })

          AnimButton({ text: this.isLoading ? '请求中...' : '立即投喂一次', primary: false, isEnabled: !this.isLoading })
            .onClick(() => {
              this.sendFeedOnce().catch((e: Error) => this.setError(e?.message ?? String(e)));
            })

          AnimButton({ text: this.isLoading ? '请求中...' : '预约一次投喂', primary: false, isEnabled: !this.isLoading })
            .onClick(() => {
              this.sendFeedOnceDelay().catch((e: Error) => this.setError(e?.message ?? String(e)));
            })
        }

        if (this.controlResp) {
          GlassCard() {
            Column({ space: 4 }) {
              Text(this.controlResp?.response?.result_code === 0 ? '指令执行成功' : '指令执行失败')
                .fontSize(14)
                .fontWeight(FontWeight.Bold)
                .fontColor(this.controlResp?.response?.result_code === 0 ? UiConstants.COLOR_PRIMARY : UiConstants.COLOR_DANGER)
              Text(JSON.stringify(this.controlResp))
                .fontSize(12)
                .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
            }
          }
        }
        
        Text('提示：控制指令默认超时 20s，请确保设备在线。')
          .fontSize(12)
          .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
          .alignSelf(ItemAlign.Center)
          .margin({ top: 12 })
      }
      .padding({ top: 12, bottom: 80, left: 12, right: 12 })
    }
    .scrollBar(BarState.Off)
    .edgeEffect(EdgeEffect.Spring)
  }

  @Builder
  private ThresholdConfigTab() {
    Scroll() {
      Column({ space: 16 }) {
        GlassCard() {
          Column({ space: 8 }) {
            this.SectionTitle('报警阈值设置')
            this.NumberInputRow('最低温度 (°C)', this.th_temp_min, '24.0', (v) => (this.th_temp_min = v))
            this.NumberInputRow('最高温度 (°C)', this.th_temp_max, '28.0', (v) => (this.th_temp_max = v))
            this.NumberInputRow('pH 下限', this.th_ph_min, '6.5', (v) => (this.th_ph_min = v))
            this.NumberInputRow('pH 上限', this.th_ph_max, '7.5', (v) => (this.th_ph_max = v))
            this.NumberInputRow('TDS 警告值', this.th_tds_warn, '500', (v) => (this.th_tds_warn = v))
            this.NumberInputRow('TDS 危险值', this.th_tds_critical, '800', (v) => (this.th_tds_critical = v))
            this.NumberInputRow('浊度 警告值', this.th_turbidity_warn, '30', (v) => (this.th_turbidity_warn = v))
            this.NumberInputRow('浊度 危险值', this.th_turbidity_critical, '50', (v) => (this.th_turbidity_critical = v))
            this.NumberInputRow('最低水位 (%)', this.th_level_min, '20', (v) => (this.th_level_min = v))
            this.NumberInputRow('最高水位 (%)', this.th_level_max, '95', (v) => (this.th_level_max = v))
            this.NumberInputRow('投喂间隔 (小时)', this.th_feed_interval, '12', (v) => (this.th_feed_interval = v))
            this.NumberInputRow('投喂量 (档位)', this.th_feed_amount, '2', (v) => (this.th_feed_amount = v))
          
            if (this.thresholdFormMsg()) {
              Text(this.thresholdFormMsg())
                .fontSize(12)
                .fontColor(UiConstants.COLOR_DANGER)
                .margin({ top: 8 })
            }

            AnimButton({ text: this.isLoading ? '发送中...' : '更新阈值配置', primary: true, isEnabled: !this.isLoading && this.thresholdFormOk() })
              .onClick(() => {
                this.sendSetThresholds().catch((e: Error) => this.setError(e?.message ?? String(e)));
              })
              .margin({ top: 12 })
          }
        }

        if (this.thresholdResp) {
          GlassCard() {
            Column({ space: 4 }) {
              Text(!!this.thresholdResp?.command_id && !this.thresholdResp?.error_code ? '配置已提交' : '配置提交失败')
                .fontSize(14)
                .fontWeight(FontWeight.Bold)
                .fontColor(!!this.thresholdResp?.command_id && !this.thresholdResp?.error_code ? UiConstants.COLOR_PRIMARY : UiConstants.COLOR_DANGER)
              Text(JSON.stringify(this.thresholdResp))
                .fontSize(12)
                .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
            }
          }
        }

        GlassCard() {
          Column({ space: 8 }) {
            this.SectionTitle('网络与校准')
            this.TextInputRow('WiFi SSID', this.cfg_wifi_ssid, '例如 MyWiFi', (v) => (this.cfg_wifi_ssid = v))
            this.PasswordInputRow('WiFi Password', this.cfg_wifi_password, '请输入密码', (v) => (this.cfg_wifi_password = v))
            this.NumberInputRow('pH 校准偏移', this.cfg_ph_offset, '例如 0.15', (v) => (this.cfg_ph_offset = v))
            this.NumberInputRow('TDS 系数', this.cfg_tds_factor, '例如 1.02', (v) => (this.cfg_tds_factor = v))

            if (this.configFormMsg()) {
              Text(this.configFormMsg())
                .fontSize(12)
                .fontColor(UiConstants.COLOR_DANGER)
                .margin({ top: 8 })
            }

            AnimButton({ text: this.isLoading ? '发送中...' : '更新系统配置', primary: true, isEnabled: !this.isLoading && !this.isWifiConfirming && this.configFormOk() })
              .onClick(() => {
                this.sendSetConfig().catch((e: Error) => this.setError(e?.message ?? String(e)));
              })
              .margin({ top: 12 })
          }
        }

        if (this.configResp) {
          GlassCard() {
            Column({ space: 4 }) {
              Text(this.configResp?.response?.result_code === 0 ? '系统配置已更新' : '系统配置更新失败')
                .fontSize(14)
                .fontWeight(FontWeight.Bold)
                .fontColor(this.configResp?.response?.result_code === 0 ? UiConstants.COLOR_PRIMARY : UiConstants.COLOR_DANGER)
              Text(JSON.stringify(this.configResp))
                .fontSize(12)
                .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
            }
          }
        }

        Text('提示：WiFi 配置更新会导致设备短暂离线。')
          .fontSize(12)
          .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
          .alignSelf(ItemAlign.Center)
          .margin({ top: 8 })
      }
      .padding({ top: 12, bottom: 80, left: 12, right: 12 })
    }
    .scrollBar(BarState.Off)
    .edgeEffect(EdgeEffect.Spring)
  }

  @Builder
  private HistoryTab() {
    Scroll() {
      Column({ space: 16 }) {
        GlassCard() {
          Column({ space: 12 }) {
            this.SectionTitle('历史数据与趋势')
            this.InfoRow('记录条数', `${this.refreshHistory.length}/${this.refreshHistoryMaxSize}`)
            
            AnimButton({ text: this.isLoading ? '处理中...' : '清空历史记录', primary: false, isEnabled: this.refreshHistory.length > 0 && !this.isLoading })
              .onClick(() => {
                this.refreshHistory = [];
                this.setInfo('历史记录已清空');
              })
          }
        }

        this.TrendChartSection()

        GlassCard() {
          Column({ space: 12 }) {
            this.SectionTitle(`最近 ${this.refreshHistoryPreviewCount} 条记录`)
            
            if (this.refreshHistory.length === 0) {
              Text('暂无历史记录，刷新后将自动记录。')
                .fontSize(14)
                .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
                .margin({ top: 12, bottom: 12 })
                .alignSelf(ItemAlign.Center)
            } else {
              List({ space: 12 }) {
                ForEach(
                  this.refreshHistory.slice(0, this.refreshHistoryPreviewCount),
                  (item: RefreshHistoryItem, index: number) => {
                    ListItem() {
                      Column({ space: 6 }) {
                        Row() {
                          Text(item.at.split(' ')[1]) // 只显示时间
                            .fontSize(12)
                            .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
                          Blank()
                          Text(item.device_status)
                            .fontSize(12)
                            .fontWeight(FontWeight.Bold)
                            .fontColor(item.device_status === 'ONLINE' ? UiConstants.COLOR_PRIMARY : UiConstants.COLOR_TEXT_SECONDARY)
                        }
                        .width('100%')
                        
                        Text(`T=${item.temperature.toFixed(1)}°C  pH=${item.ph.toFixed(1)}  TDS=${item.tds.toFixed(0)}  Turb=${item.turbidity.toFixed(0)}  Lvl=${item.water_level.toFixed(0)}%`)
                          .fontSize(13)
                          .fontColor(UiConstants.COLOR_TEXT_PRIMARY)
                          .maxLines(1)
                          .textOverflow({ overflow: TextOverflow.Ellipsis })
                      }
                      .padding(12)
                      .backgroundColor('#F5F7FA')
                      .borderRadius(8)
                    }
                  },
                  (item: RefreshHistoryItem, index: number) => `${item.at}_${index}`
                )
              }
              .width('100%')
            }
          }
        }
      }
      .padding({ top: 12, bottom: 80, left: 12, right: 12 })
    }
    .scrollBar(BarState.Off)
    .edgeEffect(EdgeEffect.Spring)
  }

  build() {
    Stack() {
      // 1. 背景层
      WarmBackground()

      // 2. 内容层
      Column({ space: 12 }) {
        // 顶部导航栏
        Column() {
          GlassCard() {
            Row({ space: 12 }) {
              Text('Aquarium Assistant')
                .fontSize(20)
                .fontWeight(FontWeight.Bold)
                .fontColor(UiConstants.COLOR_PRIMARY)
                .layoutWeight(1)
              
              AnimButton({ text: '设置', primary: false, isEnabled: true })
                .width(72)
                .onClick(() => this.openSettings())
              
              AnimButton({ text: this.isLoading ? '...' : '刷新', primary: true, isEnabled: !this.isLoading })
                .width(72)
                .onClick(() => {
                  this.refreshShadowAndStatus({ showNotice: true, resetResponses: true })
                    .catch((e: Error) => this.setError(e?.message ?? String(e)));
                })
            }
            .width('100%')
            .alignItems(VerticalAlign.Center)
          }
        }
        .margin({ top: 12, left: 12, right: 12 })

        // 通知栏
        if (this.noticeText) {
          Column() {
            GlassCard() {
              Column({ space: 4 }) {
                Row({ space: 8 }) {
                  // 简单的状态指示圆点
                  Circle({ width: 8, height: 8 })
                    .fill(this.noticeIsError ? UiConstants.COLOR_DANGER : UiConstants.COLOR_PRIMARY)
                  
                  Text(this.noticeText)
                    .fontSize(14)
                    .fontWeight(FontWeight.Medium)
                    .fontColor(this.noticeIsError ? UiConstants.COLOR_DANGER : UiConstants.COLOR_PRIMARY)
                    .layoutWeight(1)
                }
                .width('100%')

                if (this.noticeHint) {
                  Text(this.noticeHint)
                    .fontSize(12)
                    .fontColor(UiConstants.COLOR_TEXT_SECONDARY)
                    .width('100%')
                    .margin({ left: 16 })
                }
              }
            }
          }
          .margin({ left: 12, right: 12 })
        }

        // 主 Tab 内容
        Tabs({ barPosition: this.isWideLayout ? BarPosition.Start : BarPosition.End, controller: this.tabsController }) {
          TabContent() {
            this.DashboardTab()
          }
          .tabBar(this.TabBarItem(0, '仪表盘'))

          TabContent() {
            this.ControlTab()
          }
          .tabBar(this.TabBarItem(1, '控制'))

          TabContent() {
            this.ThresholdConfigTab()
          }
          .tabBar(this.TabBarItem(2, '配置'))

          TabContent() {
            this.HistoryTab()
          }
          .tabBar(this.TabBarItem(3, '历史'))
        }
        .vertical(this.isWideLayout)
        .barMode(BarMode.Fixed)
        .barWidth(this.isWideLayout ? 100 : '100%')
        .barHeight(this.isWideLayout ? '100%' : 60)
        .animationDuration(300)
        .layoutWeight(1)
        .backgroundColor(Color.Transparent)
        .onChange((index: number) => {
          this.currentIndex = index;
        })
      }
    }
    .width('100%')
    .height('100%')
  }
}


