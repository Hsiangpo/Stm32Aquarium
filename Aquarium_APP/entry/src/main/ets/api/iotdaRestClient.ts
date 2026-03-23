import http from '@ohos.net.http';
import hilog from '@ohos.hilog';
import { buildAuthorization, buildXSdkDate } from './iotdaSigner';
import {
  IOTDA_COMMAND_NAME_CONTROL,
  IOTDA_COMMAND_NAME_SET_CONFIG,
  IOTDA_COMMAND_NAME_SET_THRESHOLDS,
  IOTDA_DEFAULTS,
  IOTDA_SERVICE_ID_CONFIG,
  IOTDA_SERVICE_ID_CONTROL,
  IOTDA_SERVICE_ID_THRESHOLD,
} from '../config/iotdaDefaults';
import {
  ConfigCommandParas,
  ControlCommandParas,
  IotdaDeviceMessageResponse,
  IotdaDeviceInfo,
  IotdaErrorBody,
  IotdaAsyncCommandResponse,
  IotdaSyncCommandResponse,
  ThresholdCommandParas,
  parseAquariumShadow,
  parseDeviceInfo,
} from '../model/aquariumModels';

export interface IotdaCredentials {
  ak: string;
  sk: string;
}

export interface IotdaClientConfig {
  baseUrl: string;
  region: string;
  instanceId: string;
  projectId: string;
  deviceId: string;
}

interface PathWithQuery {
  canonicalUri: string;
  canonicalQueryString: string;
}

const LOG_DOMAIN = 0x4f5444; // "IOTD"
const LOG_TAG = 'AquariumApp';

function normalizeBaseUrl(baseUrl: string): string {
  const trimmed = baseUrl.trim();
  if (trimmed.endsWith('/')) {
    return trimmed.slice(0, -1);
  }
  return trimmed;
}

function extractHost(baseUrl: string): string {
  const trimmed = normalizeBaseUrl(baseUrl);
  const noProto = trimmed.replace(/^https?:\/\//, '');
  const slashIdx = noProto.indexOf('/');
  return slashIdx >= 0 ? noProto.slice(0, slashIdx) : noProto;
}

function splitPathAndQuery(path: string): PathWithQuery {
  const idx = path.indexOf('?');
  if (idx < 0) {
    return { canonicalUri: path, canonicalQueryString: '' };
  }
  return {
    canonicalUri: path.slice(0, idx),
    canonicalQueryString: path.slice(idx + 1),
  };
}

function jsonStringifySafe(data: unknown): string {
  return data ? JSON.stringify(data) : '';
}

function formatHttpError(statusCode: number, bodyText: string): string {
  if (!bodyText) {
    return `HTTP ${statusCode}`;
  }
  try {
    const parsed = JSON.parse(bodyText) as IotdaErrorBody;
    if (parsed?.error_code || parsed?.error_msg) {
      return `HTTP ${statusCode} ${parsed.error_code ?? ''} ${parsed.error_msg ?? ''}`.trim();
    }
  } catch (e) {
    // ignore
  }
  return `HTTP ${statusCode} ${bodyText}`.trim();
}

function errorDetail(err: unknown): string {
  if (!err) {
    return '未知错误';
  }
  if (typeof err === 'string') {
    return err;
  }
  if (typeof err === 'object') {
    const obj = err as Record<string, unknown>;
    const parts: string[] = [];
    if (typeof obj.code === 'number' || typeof obj.code === 'string') {
      parts.push(`code=${obj.code}`);
    }
    if (typeof obj.errorCode === 'number' || typeof obj.errorCode === 'string') {
      parts.push(`errorCode=${obj.errorCode}`);
    }
    if (typeof obj.message === 'string' && obj.message.length > 0) {
      parts.push(`message=${obj.message}`);
    }
    if (typeof obj.name === 'string' && obj.name.length > 0) {
      parts.push(`name=${obj.name}`);
    }
    if (typeof obj.statusCode === 'number' || typeof obj.statusCode === 'string') {
      parts.push(`statusCode=${obj.statusCode}`);
    }
    if (typeof obj.result === 'string' && obj.result.length > 0) {
      parts.push(`result=${obj.result}`);
    }
    const keys = Object.keys(obj);
    if (keys.length > 0) {
      parts.push(`keys=${keys.join(',')}`);
    }
    if (parts.length > 0) {
      return parts.join(' ');
    }
  }
  return String(err);
}

function logInfo(message: string): void {
  console.info(message);
  hilog.info(LOG_DOMAIN, LOG_TAG, '%{public}s', message);
}

function logError(message: string): void {
  console.error(message);
  hilog.error(LOG_DOMAIN, LOG_TAG, '%{public}s', message);
}

export class IotdaRestClient {
  private readonly cfg: IotdaClientConfig;
  private readonly cred: IotdaCredentials;
  private readonly host: string;

  constructor(cfg: IotdaClientConfig, cred: IotdaCredentials) {
    this.cfg = {
      ...cfg,
      baseUrl: normalizeBaseUrl(cfg.baseUrl),
      region: cfg.region || IOTDA_DEFAULTS.region,
      instanceId: cfg.instanceId || IOTDA_DEFAULTS.instanceId,
    };
    this.cred = cred;
    this.host = extractHost(this.cfg.baseUrl);
  }

  async getDeviceShadow() {
    const path = `/v5/iot/${this.cfg.projectId}/devices/${this.cfg.deviceId}/shadow`;
    const json = await this.requestJson('GET', path, undefined, 15000);
    try {
      return parseAquariumShadow(json);
    } catch (e) {
      logError(`parse shadow failed body=${JSON.stringify(json)} err=${errorDetail(e)}`);
      throw e;
    }
  }

  async getDeviceInfo(): Promise<IotdaDeviceInfo> {
    const path = `/v5/iot/${this.cfg.projectId}/devices/${this.cfg.deviceId}`;
    const json = await this.requestJson('GET', path, undefined, 15000);
    try {
      return parseDeviceInfo(json);
    } catch (e) {
      logError(`parse device failed body=${JSON.stringify(json)} err=${errorDetail(e)}`);
      throw e;
    }
  }

  async sendControl(paras: ControlCommandParas): Promise<IotdaSyncCommandResponse> {
    const path = `/v5/iot/${this.cfg.projectId}/devices/${this.cfg.deviceId}/commands`;
    const body = {
      service_id: IOTDA_SERVICE_ID_CONTROL,
      command_name: IOTDA_COMMAND_NAME_CONTROL,
      paras,
    };
    return await this.requestJson('POST', path, body, 20000);
  }

  async sendControlHeater(on: boolean): Promise<IotdaSyncCommandResponse> {
    return await this.sendControl({ heater: on });
  }

  async sendControlMessage(paras: ControlCommandParas): Promise<IotdaDeviceMessageResponse> {
    const path = `/v5/iot/${this.cfg.projectId}/devices/${this.cfg.deviceId}/messages`;
    const body = {
      name: IOTDA_COMMAND_NAME_CONTROL,
      message: {
        service_id: IOTDA_SERVICE_ID_CONTROL,
        command_name: IOTDA_COMMAND_NAME_CONTROL,
        paras,
      },
      payload_format: 'raw',
    };
    return await this.requestJson('POST', path, body, 15000);
  }

  async sendSetThresholdsAsync(
    paras: ThresholdCommandParas,
    options?: {
      expireTimeSeconds?: number;
      sendStrategy?: string;
    }
  ): Promise<IotdaAsyncCommandResponse> {
    const path = `/v5/iot/${this.cfg.projectId}/devices/${this.cfg.deviceId}/async-commands`;
    const body = {
      service_id: IOTDA_SERVICE_ID_THRESHOLD,
      command_name: IOTDA_COMMAND_NAME_SET_THRESHOLDS,
      paras,
      expire_time: options?.expireTimeSeconds ?? 86400,
      send_strategy: options?.sendStrategy ?? 'immediately',
    };
    return await this.requestJson('POST', path, body, 15000);
  }

  async sendSetConfig(paras: ConfigCommandParas): Promise<IotdaSyncCommandResponse> {
    const path = `/v5/iot/${this.cfg.projectId}/devices/${this.cfg.deviceId}/commands`;
    const body = {
      service_id: IOTDA_SERVICE_ID_CONFIG,
      command_name: IOTDA_COMMAND_NAME_SET_CONFIG,
      paras,
    };
    return await this.requestJson('POST', path, body, 20000);
  }

  private async requestJson(
    method: 'GET' | 'POST',
    path: string,
    body: Record<string, unknown> | undefined,
    timeoutMs: number
  ): Promise<any> {
    const url = `${this.cfg.baseUrl}${path}`;
    const canonical = splitPathAndQuery(path);
    const xSdkDate = buildXSdkDate(new Date());
    const contentType = 'application/json';
    const bodyText = body ? jsonStringifySafe(body) : '';

    const authorization = await buildAuthorization({
      method,
      canonicalUri: canonical.canonicalUri,
      canonicalQueryString: canonical.canonicalQueryString,
      region: this.cfg.region,
      projectId: this.cfg.projectId,
      instanceId: this.cfg.instanceId,
      host: this.host,
      xSdkDate,
      contentType,
      body: bodyText,
      ak: this.cred.ak,
      sk: this.cred.sk,
    });

    const headers: Record<string, string> = {
      'Content-Type': contentType,
      'X-Sdk-Date': xSdkDate,
      Host: this.host,
      'X-Project-Id': this.cfg.projectId,
      Authorization: authorization,
    };
    if (this.cfg.instanceId && this.cfg.instanceId.length > 0) {
      headers['Instance-Id'] = this.cfg.instanceId;
    }

    const httpRequest = http.createHttp();
    try {
      logInfo(`HTTP ${method} ${path} request start`);
      const resp = await httpRequest.request(url, {
        method: method === 'GET' ? http.RequestMethod.GET : http.RequestMethod.POST,
        header: headers,
        connectTimeout: timeoutMs,
        readTimeout: timeoutMs,
        expectDataType: http.HttpDataType.STRING,
        extraData: method === 'GET' ? undefined : bodyText,
      });

      const statusCode = resp.responseCode ?? 0;
      const bodyStr = typeof resp.result === 'string' ? resp.result : String(resp.result ?? '');
      logInfo(`HTTP ${method} ${path} response status=${statusCode}`);
      logInfo(`HTTP ${method} ${path} response body=${bodyStr}`);

      if (statusCode < 200 || statusCode >= 300) {
        throw new Error(formatHttpError(statusCode, bodyStr));
      }

      if (!bodyStr) {
        return {};
      }

      try {
        return JSON.parse(bodyStr);
      } catch (e) {
        throw new Error(`响应不是合法 JSON：${bodyStr}`);
      }
    } catch (e) {
      const detail = errorDetail(e);
      logError(`HTTP ${method} ${path} failed ${detail}`);
      throw new Error(detail);
    } finally {
      httpRequest.destroy();
    }
  }
}
