import socket from '@ohos.net.socket';
import { buffer } from '@kit.ArkTS';

export type RealtimeMqttState = 'DISCONNECTED' | 'CONNECTING' | 'CONNECTED' | 'ERROR';

export interface RealtimeMqttClientOptions {
  host: string;
  port: number;
  clientId: string;
  username: string;
  password: string;
  topic: string;
  keepAliveSec?: number;
  connectTimeoutMs?: number;
  onStateChange?: (state: RealtimeMqttState, message?: string) => void;
  onPublish?: (topic: string, payloadText: string) => void;
}

type Phase = 'IDLE' | 'WAIT_CONNACK' | 'WAIT_SUBACK' | 'CONNECTED';

function utf8Bytes(input: string): Uint8Array {
  const buf = buffer.from(input, 'utf-8');
  return new Uint8Array(buf.buffer, buf.byteOffset, buf.length);
}

function utf8String(bytes: Uint8Array): string {
  const ab = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
  return buffer.from(ab).toString('utf-8');
}

function concatBytes(parts: Uint8Array[]): Uint8Array {
  const total = parts.reduce((acc, p) => acc + p.length, 0);
  const out = new Uint8Array(total);
  let offset = 0;
  for (const p of parts) {
    out.set(p, offset);
    offset += p.length;
  }
  return out;
}

function encodeUint16(n: number): Uint8Array {
  const out = new Uint8Array(2);
  out[0] = (n >> 8) & 0xff;
  out[1] = n & 0xff;
  return out;
}

function encodeMqttString(text: string): Uint8Array {
  const bytes = utf8Bytes(text);
  return concatBytes([encodeUint16(bytes.length), bytes]);
}

function encodeRemainingLength(len: number): Uint8Array {
  const out: number[] = [];
  let value = len;
  do {
    let digit = value % 128;
    value = Math.floor(value / 128);
    if (value > 0) {
      digit = digit | 0x80;
    }
    out.push(digit);
  } while (value > 0);
  return new Uint8Array(out);
}

function decodeRemainingLength(buf: Uint8Array, offset: number):
  | { value: number; bytes: number }
  | null {
  let multiplier = 1;
  let value = 0;
  let bytes = 0;

  while (true) {
    if (offset + bytes >= buf.length) {
      return null;
    }
    const digit = buf[offset + bytes];
    bytes += 1;
    value += (digit & 0x7f) * multiplier;
    multiplier *= 128;
    if (multiplier > 128 * 128 * 128 * 128) {
      throw new Error('MQTT remaining length too large');
    }
    if ((digit & 0x80) === 0) {
      return { value, bytes };
    }
  }
}

function normalizeSocketData(data: any): Uint8Array | null {
  if (!data) {
    return null;
  }
  if (data instanceof ArrayBuffer) {
    return new Uint8Array(data);
  }
  const candidate = data?.message ?? data?.data ?? data?.payload;
  if (candidate instanceof ArrayBuffer) {
    return new Uint8Array(candidate);
  }
  if (candidate && candidate.buffer instanceof ArrayBuffer) {
    return new Uint8Array(candidate.buffer);
  }
  return null;
}

export class RealtimeMqttClient {
  private tcp: any | null = null;
  private recvBuf: Uint8Array = new Uint8Array(0);
  private packetId: number = 1;
  private phase: Phase = 'IDLE';
  private state: RealtimeMqttState = 'DISCONNECTED';
  private shouldRun: boolean = false;
  private reconnectAttempt: number = 0;

  private handshakeTimerId: number | undefined = undefined;
  private pingTimerId: number | undefined = undefined;
  private reconnectTimerId: number | undefined = undefined;
  private lastRxAt: number = 0;

  constructor(private readonly opts: RealtimeMqttClientOptions) {
  }

  getState(): RealtimeMqttState {
    return this.state;
  }

  start(): void {
    if (this.shouldRun) {
      return;
    }
    this.shouldRun = true;
    this.reconnectAttempt = 0;
    this.connectNow().catch((e) => {
      this.scheduleReconnect(e?.message ?? String(e));
    });
  }

  stop(): void {
    this.shouldRun = false;
    this.clearReconnectTimer();
    this.clearHandshakeTimer();
    this.clearPingTimer();
    this.phase = 'IDLE';
    this.setState('DISCONNECTED');
    this.closeSocket();
  }

  private setState(state: RealtimeMqttState, message?: string): void {
    this.state = state;
    if (this.opts.onStateChange) {
      try {
        this.opts.onStateChange(state, message);
      } catch {
        // ignore
      }
    }
  }

  private clearHandshakeTimer(): void {
    if (this.handshakeTimerId === undefined) {
      return;
    }
    clearTimeout(this.handshakeTimerId);
    this.handshakeTimerId = undefined;
  }

  private clearPingTimer(): void {
    if (this.pingTimerId === undefined) {
      return;
    }
    clearInterval(this.pingTimerId);
    this.pingTimerId = undefined;
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimerId === undefined) {
      return;
    }
    clearTimeout(this.reconnectTimerId);
    this.reconnectTimerId = undefined;
  }

  private closeSocket(): void {
    const tcp = this.tcp;
    this.tcp = null;
    this.recvBuf = new Uint8Array(0);
    this.lastRxAt = 0;
    if (!tcp) {
      return;
    }
    try {
      tcp.close?.();
    } catch {
      // ignore
    }
  }

  private async connectNow(): Promise<void> {
    if (!this.shouldRun) {
      return;
    }
    this.clearReconnectTimer();
    this.clearHandshakeTimer();
    this.clearPingTimer();
    this.closeSocket();

    const host = this.opts.host.trim();
    const port = this.opts.port;
    if (!host) {
      throw new Error('MQTT host 不能为空');
    }
    if (!port || port <= 0) {
      throw new Error('MQTT port 不合法');
    }
    if (!this.opts.clientId.trim() || !this.opts.username.trim() || !this.opts.password.trim()) {
      throw new Error('MQTT 鉴权信息不完整');
    }
    if (!this.opts.topic.trim()) {
      throw new Error('MQTT topic 不能为空');
    }

    this.phase = 'WAIT_CONNACK';
    this.setState('CONNECTING');

    const tcp = socket.constructTCPSocketInstance();
    this.tcp = tcp;

    tcp.on?.('message', (data: any) => {
      const bytes = normalizeSocketData(data);
      if (!bytes) {
        return;
      }
      this.lastRxAt = Date.now();
      this.onBytes(bytes);
    });

    tcp.on?.('close', () => {
      if (!this.shouldRun) {
        return;
      }
      this.scheduleReconnect('TCP 连接已关闭');
    });

    tcp.on?.('error', (err: any) => {
      if (!this.shouldRun) {
        return;
      }
      const msg = err?.message ?? String(err ?? 'TCP error');
      this.scheduleReconnect(msg);
    });

    await new Promise<void>((resolve, reject) => {
      const timeout = this.opts.connectTimeoutMs ?? 8000;
      try {
        const ret = tcp.connect?.({ address: { address: host, port }, timeout }, (err: any) => {
          if (err) {
            reject(err);
          } else {
            resolve();
          }
        });
        if (ret && typeof ret.then === 'function') {
          ret.then(resolve).catch(reject);
        }
      } catch (e) {
        reject(e);
      }
    });

    await this.sendPacket(this.buildConnectPacket());

    this.handshakeTimerId = setTimeout(() => {
      if (!this.shouldRun) {
        return;
      }
      if (this.phase === 'WAIT_CONNACK') {
        this.scheduleReconnect('CONNACK 超时');
      }
    }, 10000) as unknown as number;
  }

  private scheduleReconnect(reason: string): void {
    if (!this.shouldRun) {
      return;
    }
    if (this.reconnectTimerId !== undefined) {
      return;
    }

    this.clearHandshakeTimer();
    this.clearPingTimer();
    this.closeSocket();

    const delayMs = Math.min(30000, 1000 * Math.pow(2, this.reconnectAttempt));
    this.reconnectAttempt = Math.min(this.reconnectAttempt + 1, 10);

    this.phase = 'IDLE';
    this.setState('ERROR', `${reason}（${delayMs}ms 后重试）`);

    this.reconnectTimerId = setTimeout(() => {
      this.reconnectTimerId = undefined;
      this.connectNow().catch((e) => {
        this.scheduleReconnect(e?.message ?? String(e));
      });
    }, delayMs) as unknown as number;
  }

  private onBytes(bytes: Uint8Array): void {
    this.recvBuf = concatBytes([this.recvBuf, bytes]);

    while (true) {
      if (this.recvBuf.length < 2) {
        return;
      }
      const headerByte1 = this.recvBuf[0];
      const packetType = headerByte1 >> 4;
      const remaining = decodeRemainingLength(this.recvBuf, 1);
      if (!remaining) {
        return;
      }

      const headerLen = 1 + remaining.bytes;
      const totalLen = headerLen + remaining.value;
      if (this.recvBuf.length < totalLen) {
        return;
      }

      const packet = this.recvBuf.slice(0, totalLen);
      this.recvBuf = this.recvBuf.slice(totalLen);

      try {
        this.handlePacket(packetType, headerByte1, packet, headerLen);
      } catch (e) {
        if (this.shouldRun) {
          this.scheduleReconnect(e?.message ?? String(e));
          return;
        }
      }
    }
  }

  private handlePacket(packetType: number, headerByte1: number, packet: Uint8Array, headerLen: number): void {
    switch (packetType) {
      case 2:
        this.handleConnack(packet, headerLen);
        return;
      case 9:
        this.handleSuback(packet, headerLen);
        return;
      case 13:
        // PINGRESP
        return;
      case 3:
        this.handlePublish(headerByte1, packet, headerLen);
        return;
      default:
        return;
    }
  }

  private handleConnack(packet: Uint8Array, headerLen: number): void {
    if (packet.length < headerLen + 2) {
      throw new Error('CONNACK 长度不足');
    }
    const rc = packet[headerLen + 1];
    if (rc !== 0) {
      throw new Error(`CONNACK 返回码=${rc}`);
    }

    this.clearHandshakeTimer();
    this.phase = 'WAIT_SUBACK';

    this.handshakeTimerId = setTimeout(() => {
      if (!this.shouldRun) {
        return;
      }
      if (this.phase === 'WAIT_SUBACK') {
        this.scheduleReconnect('SUBACK 超时');
      }
    }, 10000) as unknown as number;

    const pid = this.nextPacketId();
    this.pendingSubPacketId = pid;
    this.sendPacket(this.buildSubscribePacket(pid, this.opts.topic)).catch((e) => {
      if (this.shouldRun) {
        this.scheduleReconnect(e?.message ?? String(e));
      }
    });
  }

  private pendingSubPacketId: number | null = null;

  private handleSuback(packet: Uint8Array, headerLen: number): void {
    if (packet.length < headerLen + 3) {
      throw new Error('SUBACK 长度不足');
    }
    const pid = (packet[headerLen] << 8) | packet[headerLen + 1];
    if (this.pendingSubPacketId !== null && pid !== this.pendingSubPacketId) {
      return;
    }
    const granted = packet[headerLen + 2];
    if (granted === 0x80) {
      throw new Error('SUBACK 订阅失败');
    }

    this.clearHandshakeTimer();
    this.pendingSubPacketId = null;
    this.phase = 'CONNECTED';
    this.reconnectAttempt = 0;
    this.setState('CONNECTED');
    this.startPingLoop();
  }

  private handlePublish(headerByte1: number, packet: Uint8Array, headerLen: number): void {
    const qos = (headerByte1 & 0x06) >> 1;
    let offset = headerLen;
    if (packet.length < offset + 2) {
      return;
    }
    const topicLen = (packet[offset] << 8) | packet[offset + 1];
    offset += 2;
    if (packet.length < offset + topicLen) {
      return;
    }
    const topicBytes = packet.slice(offset, offset + topicLen);
    const topic = utf8String(topicBytes);
    offset += topicLen;

    let publishPacketId: number | null = null;
    if (qos > 0) {
      if (packet.length < offset + 2) {
        return;
      }
      publishPacketId = (packet[offset] << 8) | packet[offset + 1];
      offset += 2;
    }

    const payloadBytes = packet.slice(offset);
    const payloadText = utf8String(payloadBytes);

    if (this.opts.onPublish) {
      try {
        this.opts.onPublish(topic, payloadText);
      } catch {
        // ignore
      }
    }

    // QoS1: 需要回 PUBACK，否则服务端可能重发导致重复消息。
    if (qos === 1 && publishPacketId !== null) {
      this.sendPacket(this.buildPubAckPacket(publishPacketId)).catch(() => {
        // ignore; close/error 会触发重连
      });
    }
  }

  private startPingLoop(): void {
    this.clearPingTimer();
    const keepAliveSec = this.opts.keepAliveSec ?? 120;
    const intervalMs = Math.max(10000, Math.floor((keepAliveSec * 1000) / 2));
    this.lastRxAt = Date.now();

    this.pingTimerId = setInterval(() => {
      if (!this.shouldRun || this.phase !== 'CONNECTED') {
        return;
      }
      const now = Date.now();
      if (this.lastRxAt && now - this.lastRxAt > keepAliveSec * 1000 * 2) {
        this.scheduleReconnect('MQTT 心跳超时');
        return;
      }
      this.sendPacket(this.buildPingReqPacket()).catch(() => {
        // ignore; close/error 会触发重连
      });
    }, intervalMs) as unknown as number;
  }

  private nextPacketId(): number {
    const id = this.packetId;
    this.packetId = (this.packetId % 0xffff) + 1;
    return id;
  }

  private buildConnectPacket(): Uint8Array {
    const protocolName = encodeMqttString('MQTT');
    const protocolLevel = new Uint8Array([0x04]); // MQTT 3.1.1
    const connectFlags = new Uint8Array([0xc2]); // username + password + clean session
    const keepAliveSec = this.opts.keepAliveSec ?? 120;
    const keepAlive = encodeUint16(keepAliveSec);

    const variableHeader = concatBytes([protocolName, protocolLevel, connectFlags, keepAlive]);

    const payload = concatBytes([
      encodeMqttString(this.opts.clientId),
      encodeMqttString(this.opts.username),
      encodeMqttString(this.opts.password),
    ]);

    const remainingLength = variableHeader.length + payload.length;
    const fixedHeader = concatBytes([
      new Uint8Array([0x10]),
      encodeRemainingLength(remainingLength),
    ]);

    return concatBytes([fixedHeader, variableHeader, payload]);
  }

  private buildSubscribePacket(packetId: number, topic: string): Uint8Array {
    const topicFilter = encodeMqttString(topic);
    const qos0 = new Uint8Array([0x00]);
    const payload = concatBytes([topicFilter, qos0]);
    const variableHeader = encodeUint16(packetId);
    const remainingLength = variableHeader.length + payload.length;
    const fixedHeader = concatBytes([
      new Uint8Array([0x82]),
      encodeRemainingLength(remainingLength),
    ]);
    return concatBytes([fixedHeader, variableHeader, payload]);
  }

  private buildPingReqPacket(): Uint8Array {
    return new Uint8Array([0xc0, 0x00]);
  }

  private buildPubAckPacket(packetId: number): Uint8Array {
    return concatBytes([new Uint8Array([0x40, 0x02]), encodeUint16(packetId)]);
  }

  private async sendPacket(packet: Uint8Array): Promise<void> {
    const tcp = this.tcp;
    if (!tcp) {
      throw new Error('TCP 未连接');
    }
    const ab = packet.buffer.slice(packet.byteOffset, packet.byteOffset + packet.byteLength);

    await new Promise<void>((resolve, reject) => {
      try {
        const ret = tcp.send?.({ data: ab }, (err: any) => {
          if (err) {
            reject(err);
          } else {
            resolve();
          }
        });
        if (ret && typeof ret.then === 'function') {
          ret.then(resolve).catch(reject);
        }
      } catch (e) {
        reject(e);
      }
    });
  }
}
