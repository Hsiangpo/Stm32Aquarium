import { IOTDA_SERVICE_ID_AQUARIUM } from '../config/iotdaDefaults';

export type DeviceStatus = 'ONLINE' | 'OFFLINE' | string;

export interface AquariumProperties {
  temperature: number;
  ph: number;
  tds: number;
  turbidity: number;
  water_level: number;
  heater: boolean;
  pump_in: boolean;
  pump_out: boolean;
  auto_mode: boolean;
  feed_countdown: number;
  feeding_in_progress: boolean;
  alarm_level: number;
  alarm_muted: boolean;
}

export interface AquariumShadowSnapshot {
  device_id: string;
  event_time?: string;
  properties: AquariumProperties;
}

export interface IotdaDeviceInfo {
  device_id: string;
  status?: DeviceStatus;
  device_name?: string;
  node_id?: string;
}

export interface IotdaErrorBody {
  error_code?: string;
  error_msg?: string;
}

export interface IotdaSyncCommandResponse {
  command_id?: string;
  response?: {
    result_code?: number;
    response_name?: string;
    paras?: Record<string, unknown>;
  };
  error_code?: string;
  error_msg?: string;
}

export interface IotdaAsyncCommandResponse {
  command_id?: string;
  error_code?: string;
  error_msg?: string;
}

export interface ControlCommandParas {
  heater?: boolean;
  pump_in?: boolean;
  pump_out?: boolean;
  mute?: boolean;
  auto_mode?: boolean;
  feed?: boolean;
  feed_once_delay?: number;
  target_temp?: number;
}

export interface ThresholdCommandParas {
  temp_min?: number;
  temp_max?: number;
  ph_min?: number;
  ph_max?: number;
  tds_warn?: number;
  tds_critical?: number;
  turbidity_warn?: number;
  turbidity_critical?: number;
  level_min?: number;
  level_max?: number;
  feed_interval?: number;
  feed_amount?: number;
}

export interface ConfigCommandParas {
  wifi_ssid?: string;
  wifi_password?: string;
  ph_offset?: number;
  tds_factor?: number;
}

function asObject(value: unknown): Record<string, unknown> | null {
  if (!value || typeof value !== 'object') {
    return null;
  }
  return value as Record<string, unknown>;
}

function toStringSafe(value: unknown): string | undefined {
  if (typeof value === 'string') {
    return value;
  }
  return undefined;
}

function toNumberSafe(value: unknown): number | undefined {
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

function toBoolSafe(value: unknown): boolean | undefined {
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

export function parseAquariumShadow(raw: unknown): AquariumShadowSnapshot {
  const root = asObject(raw);
  if (!root) {
    throw new Error('影子响应不是对象');
  }

  const deviceId = toStringSafe(root.device_id) ?? '';
  const shadow = root.shadow;
  if (!Array.isArray(shadow)) {
    throw new Error('影子响应缺少 shadow 数组');
  }

  const aquariumShadow = shadow.find((s) => {
    const so = asObject(s);
    return so?.service_id === IOTDA_SERVICE_ID_AQUARIUM;
  });
  const serviceObj = asObject(aquariumShadow);
  if (!serviceObj) {
    throw new Error(`shadow 中找不到 service_id=${IOTDA_SERVICE_ID_AQUARIUM}`);
  }

  const reported = asObject(serviceObj.reported);
  const propsObj = asObject(reported?.properties);
  if (!propsObj) {
    throw new Error('reported.properties 缺失');
  }

  const properties: AquariumProperties = {
    temperature: toNumberSafe(propsObj.temperature) ?? 0,
    ph: toNumberSafe(propsObj.ph) ?? 0,
    tds: toNumberSafe(propsObj.tds) ?? 0,
    turbidity: toNumberSafe(propsObj.turbidity) ?? 0,
    water_level: toNumberSafe(propsObj.water_level) ?? 0,
    heater: toBoolSafe(propsObj.heater) ?? false,
    pump_in: toBoolSafe(propsObj.pump_in) ?? false,
    pump_out: toBoolSafe(propsObj.pump_out) ?? false,
    auto_mode: toBoolSafe(propsObj.auto_mode) ?? false,
    feed_countdown: toNumberSafe(propsObj.feed_countdown) ?? 0,
    feeding_in_progress: toBoolSafe(propsObj.feeding_in_progress) ?? false,
    alarm_level: toNumberSafe(propsObj.alarm_level) ?? 0,
    alarm_muted: toBoolSafe(propsObj.alarm_muted) ?? false,
  };

  return {
    device_id: deviceId,
    event_time: toStringSafe(reported?.event_time),
    properties,
  };
}

export function parseDeviceInfo(raw: unknown): IotdaDeviceInfo {
  const root = asObject(raw);
  if (!root) {
    throw new Error('设备状态响应不是对象');
  }
  return {
    device_id: toStringSafe(root.device_id) ?? '',
    status: (toStringSafe(root.status) ?? 'UNKNOWN') as DeviceStatus,
    device_name: toStringSafe(root.device_name),
    node_id: toStringSafe(root.node_id),
  };
}
