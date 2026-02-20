import http from '@ohos.net.http';
import { buildAuthorization, buildXSdkDate } from './iotdaSigner';
import {
  IOTDA_COMMAND_NAME_CONTROL,
  IOTDA_COMMAND_NAME_SET_CONFIG,
  IOTDA_COMMAND_NAME_SET_THRESHOLDS,
  IOTDA_SERVICE_ID_CONFIG,
  IOTDA_SERVICE_ID_CONTROL,
  IOTDA_SERVICE_ID_THRESHOLD,
} from '../config/iotdaDefaults';
import {
  ConfigCommandParas,
  ControlCommandParas,
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
  projectId: string;
  deviceId: string;
}

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

export class IotdaRestClient {
  private readonly cfg: IotdaClientConfig;
  private readonly cred: IotdaCredentials;
  private readonly host: string;

  constructor(cfg: IotdaClientConfig, cred: IotdaCredentials) {
    this.cfg = { ...cfg, baseUrl: normalizeBaseUrl(cfg.baseUrl) };
    this.cred = cred;
    this.host = extractHost(this.cfg.baseUrl);
  }

  async getDeviceShadow() {
    const path = `/v5/iot/${this.cfg.projectId}/devices/${this.cfg.deviceId}/shadow`;
    const json = await this.requestJson('GET', path, undefined, 15000);
    return parseAquariumShadow(json);
  }

  async getDeviceInfo(): Promise<IotdaDeviceInfo> {
    const path = `/v5/iot/${this.cfg.projectId}/devices/${this.cfg.deviceId}`;
    const json = await this.requestJson('GET', path, undefined, 15000);
    return parseDeviceInfo(json);
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
    const xSdkDate = buildXSdkDate(new Date());
    const contentType = body ? 'application/json' : undefined;
    const bodyText = body ? jsonStringifySafe(body) : '';

    const authorization = await buildAuthorization({
      method,
      canonicalUri: path,
      canonicalQueryString: '',
      host: this.host,
      xSdkDate,
      contentType,
      body: bodyText,
      ak: this.cred.ak,
      sk: this.cred.sk,
    });

    const headers: Record<string, string> = {
      'X-Sdk-Date': xSdkDate,
      Authorization: authorization,
      Accept: 'application/json',
    };
    if (contentType) {
      headers['Content-Type'] = contentType;
    }

    const httpRequest = http.createHttp();
    try {
      const resp = await httpRequest.request(url, {
        method: method === 'GET' ? http.RequestMethod.GET : http.RequestMethod.POST,
        header: headers,
        connectTimeout: timeoutMs,
        readTimeout: timeoutMs,
        expectDataType: http.HttpDataType.STRING,
        extraData: bodyText,
      });

      const statusCode = resp.responseCode ?? 0;
      const bodyStr = typeof resp.result === 'string' ? resp.result : String(resp.result ?? '');

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
    } finally {
      httpRequest.destroy();
    }
  }
}
