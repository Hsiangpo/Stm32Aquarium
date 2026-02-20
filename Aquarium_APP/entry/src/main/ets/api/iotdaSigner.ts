import { cryptoFramework } from '@kit.CryptoArchitectureKit';
import { buffer } from '@kit.ArkTS';

const ALGORITHM = 'SDK-HMAC-SHA256';

function utf8Bytes(input: string): Uint8Array {
  const buf = buffer.from(input, 'utf-8');
  return new Uint8Array(buf.buffer, buf.byteOffset, buf.length);
}

function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes)
    .map((b) => b.toString(16).padStart(2, '0'))
    .join('');
}

async function sha256Hex(payload: string): Promise<string> {
  const md = cryptoFramework.createMd('SHA256');
  await md.update({ data: utf8Bytes(payload) });
  const digest = await md.digest();
  return bytesToHex(digest.data);
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

function canonicalizeHeaders(headers: Record<string, string>) {
  const normalized = Object.entries(headers)
    .map(([k, v]) => [k.toLowerCase().trim(), String(v).trim()] as const)
    .sort(([a], [b]) => (a === b ? 0 : a < b ? -1 : 1));

  const signedHeaders = normalized.map(([k]) => k).join(';');
  const canonical = normalized.map(([k, v]) => `${k}:${v}\n`).join('');

  return { canonical, signedHeaders };
}

export function buildXSdkDate(date: Date = new Date()): string {
  const iso = date.toISOString(); // e.g. 2025-12-14T21:12:34.567Z
  return iso.replace(/[-:]/g, '').replace(/\.\d{3}Z$/, 'Z'); // 20251214T211234Z
}

export async function buildAuthorization(params: {
  method: string;
  canonicalUri: string; // e.g. /v5/iot/{project_id}/devices/{device_id}/shadow
  canonicalQueryString?: string; // empty when no query
  host: string;
  xSdkDate: string; // YYYYMMDDTHHMMSSZ
  contentType?: string; // application/json for POST
  body?: string; // GET can be empty string
  ak: string;
  sk: string;
}): Promise<string> {
  const method = params.method.toUpperCase();
  const canonicalQueryString = params.canonicalQueryString ?? '';
  const body = params.body ?? '';

  const headersToSign: Record<string, string> = {
    host: params.host,
    'x-sdk-date': params.xSdkDate,
  };
  if (params.contentType) {
    headersToSign['content-type'] = params.contentType;
  }

  const { canonical: canonicalHeadersText, signedHeaders } =
    canonicalizeHeaders(headersToSign);

  const payloadHash = await sha256Hex(body);
  const canonicalRequest = [
    method,
    params.canonicalUri,
    canonicalQueryString,
    canonicalHeadersText,
    signedHeaders,
    payloadHash,
  ].join('\n');

  const stringToSign = [
    ALGORITHM,
    params.xSdkDate,
    await sha256Hex(canonicalRequest),
  ].join('\n');

  const signature = await hmacSha256Hex(params.sk, stringToSign);

  return `${ALGORITHM} Access=${params.ak}, SignedHeaders=${signedHeaders}, Signature=${signature}`;
}
