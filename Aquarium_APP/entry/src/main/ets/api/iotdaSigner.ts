import { cryptoFramework } from '@kit.CryptoArchitectureKit';
import { buffer } from '@kit.ArkTS';

const ALGORITHM = 'V11-HMAC-SHA256';
const DEFAULT_SERVICE = 'iotda';

interface CanonicalHeader {
  key: string;
  value: string;
}

interface CanonicalHeaderResult {
  canonical: string;
  signedHeaders: string;
}

interface QueryPair {
  key: string;
  value: string;
}

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
  const digest = await hmacSha256Bytes(utf8Bytes(key), utf8Bytes(data));
  return bytesToHex(digest);
}

async function hmacSha256Bytes(
  key: Uint8Array,
  data: Uint8Array
): Promise<Uint8Array> {
  const keyGen = cryptoFramework.createSymKeyGenerator('HMAC');
  const symKey = await keyGen.convertKey({ data: key });

  const mac = cryptoFramework.createMac('SHA256');
  await mac.init(symKey);
  await mac.update({ data });
  const macResult = await mac.doFinal();
  return macResult.data;
}

function rfc3986Encode(input: string): string {
  return encodeURIComponent(input).replace(/[!'()*]/g, (ch: string) => {
    return `%${ch.charCodeAt(0).toString(16).toUpperCase()}`;
  });
}

function canonicalizeUri(path: string): string {
  let decoded = path;
  try {
    decoded = decodeURIComponent(path);
  } catch (_err) {
    decoded = path;
  }

  const segments = decoded.split('/');
  let encoded = '';
  for (let i = 0; i < segments.length; i++) {
    if (i > 0) {
      encoded += '/';
    }
    encoded += rfc3986Encode(segments[i]);
  }
  if (encoded.endsWith('/')) {
    return encoded;
  }
  return `${encoded}/`;
}

function canonicalizeQueryString(query: string): string {
  if (query.length === 0) {
    return '';
  }

  const pairs: QueryPair[] = [];
  const items = query.split('&');
  for (let i = 0; i < items.length; i++) {
    const item = items[i];
    if (item.length === 0) {
      continue;
    }
    const idx = item.indexOf('=');
    const pair: QueryPair = { key: '', value: '' };
    if (idx < 0) {
      pair.key = item;
      pair.value = '';
    } else {
      pair.key = item.slice(0, idx);
      pair.value = item.slice(idx + 1);
    }
    pairs.push(pair);
  }
  pairs.sort((a: QueryPair, b: QueryPair): number => {
    if (a.key === b.key) {
      if (a.value === b.value) {
        return 0;
      }
      return a.value < b.value ? -1 : 1;
    }
    return a.key < b.key ? -1 : 1;
  });

  let output = '';
  for (let i = 0; i < pairs.length; i++) {
    if (i > 0) {
      output += '&';
    }
    output += `${rfc3986Encode(pairs[i].key)}=${rfc3986Encode(pairs[i].value)}`;
  }
  return output;
}

function canonicalizeHeaders(headers: CanonicalHeader[]): CanonicalHeaderResult {
  const entries: CanonicalHeader[] = [];
  for (let i = 0; i < headers.length; i++) {
    entries.push({
      key: headers[i].key.toLowerCase().trim(),
      value: headers[i].value.trim(),
    });
  }
  entries.sort((a: CanonicalHeader, b: CanonicalHeader): number => {
    if (a.key === b.key) {
      return 0;
    }
    return a.key < b.key ? -1 : 1;
  });

  let signedHeaders = '';
  let canonical = '';
  for (let i = 0; i < entries.length; i++) {
    if (i > 0) {
      signedHeaders += ';';
    }
    signedHeaders += entries[i].key;
    canonical += `${entries[i].key}:${entries[i].value}\n`;
  }
  return { canonical, signedHeaders };
}

export function buildXSdkDate(date: Date = new Date()): string {
  const iso = date.toISOString(); // e.g. 2025-12-14T21:12:34.567Z
  return iso.replace(/[-:]/g, '').replace(/\.\d{3}Z$/, 'Z'); // 20251214T211234Z
}

export async function buildAuthorization(params: {
  method: string;
  canonicalUri: string;
  canonicalQueryString?: string;
  region: string;
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
  const queryText = params.canonicalQueryString ?? '';
  const canonicalQueryString = canonicalizeQueryString(queryText);
  const body = params.body ?? '';
  const serviceName = params.serviceName ?? DEFAULT_SERVICE;

  const headersToSign: CanonicalHeader[] = [];
  headersToSign.push({ key: 'host', value: params.host });
  headersToSign.push({ key: 'x-sdk-date', value: params.xSdkDate });
  if (params.instanceId && params.instanceId.length > 0) {
    headersToSign.push({ key: 'instance-id', value: params.instanceId });
  }
  if (params.contentType) {
    headersToSign.push({ key: 'content-type', value: params.contentType });
  }

  const headerResult = canonicalizeHeaders(headersToSign);
  const canonicalHeadersText = headerResult.canonical;
  const signedHeaders = headerResult.signedHeaders;

  const payloadHash = await sha256Hex(body);
  const canonicalRequest = [
    method,
    canonicalizeUri(params.canonicalUri),
    canonicalQueryString,
    canonicalHeadersText,
    signedHeaders,
    payloadHash,
  ].join('\n');

  const dateText = params.xSdkDate.slice(0, 8);
  const info = `${dateText}/${params.region}/${serviceName}`;
  const canonicalRequestHash = await sha256Hex(canonicalRequest);
  const stringToSign = [
    ALGORITHM,
    params.xSdkDate,
    info,
    canonicalRequestHash,
  ].join('\n');

  const prk = await hmacSha256Bytes(utf8Bytes(params.ak), utf8Bytes(params.sk));
  const der = await hmacSha256Bytes(prk, utf8Bytes(`${info}\x01`));
  const derivedHex = bytesToHex(der);
  const signature = await hmacSha256Hex(derivedHex, stringToSign);

  return `${ALGORITHM} Credential=${params.ak}/${info}, SignedHeaders=${signedHeaders}, Signature=${signature}`;
}
