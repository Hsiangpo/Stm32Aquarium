import { cryptoFramework } from '@kit.CryptoArchitectureKit';
import { buffer } from '@kit.ArkTS';

function utf8Bytes(input: string): Uint8Array {
  const buf = buffer.from(input, 'utf-8');
  return new Uint8Array(buf.buffer, buf.byteOffset, buf.length);
}

function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes)
    .map((b) => b.toString(16).padStart(2, '0'))
    .join('');
}

function buildUtcTimestamp(date: Date = new Date()): string {
  const iso = date.toISOString(); // e.g. 2025-12-14T21:12:34.567Z
  return iso.slice(0, 13).replace(/[-T:]/g, '');
}

async function hmacSha256Hex(key: string, data: string): Promise<string> {
  const keyGen = cryptoFramework.createSymKeyGenerator('HMAC');
  const symKey = await keyGen.convertKey({ data: utf8Bytes(key) });

  const mac = cryptoFramework.createMac('SHA256');
  await mac.init(symKey);
  await mac.update({ data: utf8Bytes(data) });
  const macResult = await mac.doFinal();
  return bytesToHex(macResult.data);
}

export interface IotdaMqttAuthResult {
  timestamp: string;
  clientId: string;
  username: string;
  password: string;
}

export async function buildIotdaMqttAuth(params: {
  deviceId: string;
  deviceSecret: string;
  fixedTimestamp?: string;
}): Promise<IotdaMqttAuthResult> {
  const deviceId = params.deviceId.trim();
  const deviceSecret = params.deviceSecret.trim();
  const fixedTimestamp = params.fixedTimestamp?.trim();
  const timestamp =
    fixedTimestamp && fixedTimestamp.length > 0
      ? fixedTimestamp
      : buildUtcTimestamp();

  if (!deviceId) {
    throw new Error('deviceId 不能为空');
  }
  if (!deviceSecret) {
    throw new Error('deviceSecret 不能为空');
  }
  if (!timestamp) {
    throw new Error('timestamp 不能为空');
  }

  // IoTDA 设备 MQTT 鉴权（见 docs/HuaweiCloud.MD）：
  // password = HMAC_SHA256(key=timestamp, msg=deviceSecret)
  if (!/^\d{10}$/.test(timestamp)) {
    throw new Error('timestamp 必须为 UTC YYYYMMDDHH');
  }
  const password = await hmacSha256Hex(timestamp, deviceSecret);

  // sign_type=0：不校验时间戳（仍会参与 password HMAC），用于避免与设备端 sign_type=1 的 clientId 冲突。
  const clientId = `${deviceId}_0_0_${timestamp}`;

  return {
    timestamp,
    clientId,
    username: deviceId,
    password,
  };
}
