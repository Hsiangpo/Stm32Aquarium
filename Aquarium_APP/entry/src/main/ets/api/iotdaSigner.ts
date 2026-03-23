import cryptoFramework from '@ohos.security.cryptoFramework';
import util from '@ohos.util';

const ALGORITHM = 'V11-HMAC-SHA256';
const DEFAULT_SERVICE = 'iotda';
const EMPTY_SHA256 =
  'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855';

interface CanonicalHeader {
  key: string;
  value: string;
}

function utf8Bytes(input: string): Uint8Array {
  try {
    const encoder = new util.TextEncoder();
    return encoder.encode(input);
  } catch {
    const bytes: number[] = [];
    for (let i = 0; i < input.length; i += 1) {
      const code = input.charCodeAt(i);
      if (code < 0x80) {
        bytes.push(code);
      } else if (code < 0x800) {
        bytes.push(0xc0 | (code >> 6));
        bytes.push(0x80 | (code & 0x3f));
      } else {
        bytes.push(0xe0 | (code >> 12));
        bytes.push(0x80 | ((code >> 6) & 0x3f));
        bytes.push(0x80 | (code & 0x3f));
      }
    }
    return new Uint8Array(bytes);
  }
}

function bytesToHex(bytes: Uint8Array): string {
  let output = '';
  for (let i = 0; i < bytes.length; i += 1) {
    output += bytes[i].toString(16).padStart(2, '0');
  }
  return output;
}

async function sha256Hex(payload: string): Promise<string> {
  if (payload.length === 0) {
    return EMPTY_SHA256;
  }

  const md = cryptoFramework.createMd('SHA256');
  await md.update({ data: utf8Bytes(payload) });
  const digest = await md.digest();
  return bytesToHex(digest.data);
}

async function hmacSha256Bytes(
  keyBytes: Uint8Array,
  data: string
): Promise<Uint8Array> {
  const keyGenerator = cryptoFramework.createSymKeyGenerator('HMAC');
  const symKey = await keyGenerator.convertKey({ data: keyBytes });

  const mac = cryptoFramework.createMac('SHA256');
  await mac.init(symKey);

  const inputData = data.length === 0 ? new Uint8Array([0]) : utf8Bytes(data);
  await mac.update({ data: inputData });
  const result = await mac.doFinal();
  return result.data;
}

async function hmacSha256Hex(keyBytes: Uint8Array, data: string): Promise<string> {
  return bytesToHex(await hmacSha256Bytes(keyBytes, data));
}

function rfc3986Encode(input: string): string {
  return encodeURIComponent(input).replace(/[!'()*]/g, (ch: string) => {
    return `%${ch.charCodeAt(0).toString(16).toUpperCase()}`;
  });
}

function canonicalizeUri(path: string): string {
  if (!path) {
    return '/';
  }

  const segments = path.split('/').map((segment) => encodeURIComponent(segment));
  let normalized = segments.join('/');
  if (!normalized.endsWith('/')) {
    normalized += '/';
  }
  return normalized;
}

function canonicalizeQueryString(query: string): string {
  if (!query) {
    return '';
  }

  const pairs = query
    .split('&')
    .filter((item) => item.length > 0)
    .map((item) => {
      const idx = item.indexOf('=');
      if (idx < 0) {
        return { key: item, value: '' };
      }
      return {
        key: item.slice(0, idx),
        value: item.slice(idx + 1),
      };
    });

  pairs.sort((left, right) => {
    if (left.key === right.key) {
      if (left.value === right.value) {
        return 0;
      }
      return left.value < right.value ? -1 : 1;
    }
    return left.key < right.key ? -1 : 1;
  });

  return pairs
    .map((pair) => `${rfc3986Encode(pair.key)}=${rfc3986Encode(pair.value)}`)
    .join('&');
}

function canonicalizeHeaders(headers: CanonicalHeader[]): {
  canonical: string;
  signedHeaders: string;
} {
  const normalized = headers
    .map((header) => ({
      key: header.key.toLowerCase().trim(),
      value: header.value.trim(),
    }))
    .sort((left, right) => (left.key < right.key ? -1 : left.key > right.key ? 1 : 0));

  const signedHeaders = normalized.map((header) => header.key).join(';');
  const canonical = normalized
    .map((header) => `${header.key}:${header.value}\n`)
    .join('');

  return { canonical, signedHeaders };
}

async function hkdfDeriveHex(ak: string, sk: string, info: string): Promise<string> {
  const prk = await hmacSha256Bytes(utf8Bytes(ak), sk);
  const okm = await hmacSha256Bytes(prk, `${info}\x01`);
  return bytesToHex(okm);
}

export function buildXSdkDate(date: Date = new Date()): string {
  const yyyy = date.getUTCFullYear();
  const mm = String(date.getUTCMonth() + 1).padStart(2, '0');
  const dd = String(date.getUTCDate()).padStart(2, '0');
  const hh = String(date.getUTCHours()).padStart(2, '0');
  const mi = String(date.getUTCMinutes()).padStart(2, '0');
  const ss = String(date.getUTCSeconds()).padStart(2, '0');
  return `${yyyy}${mm}${dd}T${hh}${mi}${ss}Z`;
}

export async function buildAuthorization(params: {
  method: string;
  canonicalUri: string;
  canonicalQueryString?: string;
  region: string;
  projectId?: string;
  instanceId?: string;
  serviceName?: string;
  host: string;
  xSdkDate: string;
  contentType?: string;
  body?: string;
  ak: string;
  sk: string;
}): Promise<string> {
  const method = params.method.toUpperCase();
  const body = params.body ?? '';
  const contentType = params.contentType ?? 'application/json';
  const serviceName = params.serviceName ?? DEFAULT_SERVICE;
  const dateText = params.xSdkDate.slice(0, 8);
  const info = `${dateText}/${params.region}/${serviceName}`;

  const headersToSign: CanonicalHeader[] = [
    { key: 'content-type', value: contentType },
    { key: 'host', value: params.host },
    { key: 'x-sdk-date', value: params.xSdkDate },
  ];

  if (params.projectId && params.projectId.length > 0) {
    headersToSign.push({ key: 'x-project-id', value: params.projectId });
  }
  if (params.instanceId && params.instanceId.length > 0) {
    headersToSign.push({ key: 'instance-id', value: params.instanceId });
  }

  const headerResult = canonicalizeHeaders(headersToSign);
  const payloadHash = await sha256Hex(body);
  const canonicalRequest = [
    method,
    canonicalizeUri(params.canonicalUri),
    canonicalizeQueryString(params.canonicalQueryString ?? ''),
    headerResult.canonical,
    headerResult.signedHeaders,
    payloadHash,
  ].join('\n');

  const canonicalRequestHash = await sha256Hex(canonicalRequest);
  const stringToSign = [
    ALGORITHM,
    params.xSdkDate,
    info,
    canonicalRequestHash,
  ].join('\n');

  const derivedKeyHex = await hkdfDeriveHex(params.ak, params.sk, info);
  const signature = await hmacSha256Hex(utf8Bytes(derivedKeyHex), stringToSign);

  return `${ALGORITHM} Credential=${params.ak}/${info}, SignedHeaders=${headerResult.signedHeaders}, Signature=${signature}`;
}
