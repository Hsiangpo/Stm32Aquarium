import deviceManager from '@ohos.distributedDeviceManager';
import distributedKVStore from '@ohos.data.distributedKVStore';
import { GlobalContext } from './globalContext';

export interface DistributedDeviceInfo {
  deviceId: string;
  deviceName: string;
}

export interface SharedSettings {
  updatedAt: string;
  baseUrl: string;
  projectId: string;
  deviceId: string;
  autoRefreshEnabled: boolean;
  alarmNotifyEnabled: boolean;
}

const STORE_ID = 'aquarium_shared_settings';
const KEY_SETTINGS = 'settings_json';

let deviceManagerPromise: Promise<any> | null = null;
let kvStorePromise: Promise<any> | null = null;

function getBundleName(): string {
  const ctx = GlobalContext.getAppContext() as any;
  return String(ctx?.abilityInfo?.bundleName ?? 'com.hsiangpo.AquariumAPP');
}

async function getDeviceManager(): Promise<any> {
  if (deviceManagerPromise) {
    return deviceManagerPromise;
  }
  deviceManagerPromise = new Promise((resolve, reject) => {
    const dmAny: any = deviceManager as any;
    const bundleName = getBundleName();
    try {
      const ret = dmAny?.createDeviceManager?.(bundleName, (err: any, mgr: any) => {
        if (err) {
          reject(err);
        } else {
          resolve(mgr);
        }
      });
      if (ret && typeof ret.then === 'function') {
        ret.then(resolve).catch(reject);
      }
    } catch (e) {
      reject(e);
    }
  });
  return deviceManagerPromise;
}

export async function listTrustedDevices(): Promise<DistributedDeviceInfo[]> {
  const mgr = await getDeviceManager();
  const dm: any = mgr as any;

  let devices: any[] = [];
  try {
    if (typeof dm.getTrustedDeviceListSync === 'function') {
      devices = dm.getTrustedDeviceListSync();
    } else if (typeof dm.getTrustedDeviceList === 'function') {
      devices = await new Promise<any[]>((resolve, reject) => {
        dm.getTrustedDeviceList((err: any, list: any[]) => {
          if (err) reject(err);
          else resolve(list ?? []);
        });
      });
    } else if (typeof dm.getAvailableDeviceListSync === 'function') {
      devices = dm.getAvailableDeviceListSync();
    }
  } catch {
    devices = [];
  }

  return (devices ?? []).map((d: any) => ({
    deviceId: String(d?.deviceId ?? ''),
    deviceName: String(d?.deviceName ?? d?.name ?? d?.deviceId ?? ''),
  })).filter((d) => !!d.deviceId);
}

async function getKvStore(): Promise<any> {
  if (kvStorePromise) {
    return kvStorePromise;
  }

  kvStorePromise = (async () => {
    const ctx = GlobalContext.getAppContext() as any;
    const bundleName = getBundleName();
    const dkv: any = distributedKVStore as any;

    const manager = await new Promise<any>((resolve, reject) => {
      const cfg = { bundleName, userInfo: { userId: '0' } };
      try {
        let ret: any;
        try {
          ret = dkv?.createKVManager?.(ctx, cfg, (err: any, mgr: any) => {
            if (err) reject(err);
            else resolve(mgr);
          });
        } catch {
          ret = dkv?.createKVManager?.(cfg, (err: any, mgr: any) => {
            if (err) reject(err);
            else resolve(mgr);
          });
        }
        if (ret && typeof ret.then === 'function') {
          ret.then(resolve).catch(reject);
        }
      } catch (e) {
        reject(e);
      }
    });

    const options = {
      createIfMissing: true,
      encrypt: false,
      backup: false,
      autoSync: true,
      kvStoreType: dkv?.KVStoreType?.SINGLE_VERSION ?? 0,
      securityLevel: dkv?.SecurityLevel?.S1 ?? 1,
    };

    const store = await new Promise<any>((resolve, reject) => {
      try {
        const ret = manager?.getKVStore?.(STORE_ID, options, (err: any, kv: any) => {
          if (err) reject(err);
          else resolve(kv);
        });
        if (ret && typeof ret.then === 'function') {
          ret.then(resolve).catch(reject);
        }
      } catch (e) {
        reject(e);
      }
    });

    return store;
  })();

  return kvStorePromise;
}

export async function publishSharedSettings(settings: SharedSettings): Promise<void> {
  const store: any = await getKvStore();
  const json = JSON.stringify(settings);

  const putRet = store?.put?.(KEY_SETTINGS, json);
  if (putRet && typeof putRet.then === 'function') {
    await putRet;
  } else {
    await new Promise<void>((resolve, reject) => {
      store?.put?.(KEY_SETTINGS, json, (err: any) => {
        if (err) reject(err);
        else resolve();
      });
    });
  }

  // 可选：触发同步（若 API 可用）
  try {
    const devices = await listTrustedDevices();
    if (typeof store?.sync === 'function' && devices.length > 0) {
      const ids = devices.map((d) => d.deviceId);
      const syncRet = store.sync(ids, 0);
      if (syncRet && typeof syncRet.then === 'function') {
        await syncRet;
      }
    }
  } catch {
    // ignore
  }
}

export async function loadSharedSettings(): Promise<SharedSettings | null> {
  const store: any = await getKvStore();

  let raw: any = null;
  try {
    const getRet = store?.get?.(KEY_SETTINGS);
    if (getRet && typeof getRet.then === 'function') {
      raw = await getRet;
    } else {
      raw = await new Promise<any>((resolve, reject) => {
        store?.get?.(KEY_SETTINGS, (err: any, v: any) => {
          if (err) reject(err);
          else resolve(v);
        });
      });
    }
  } catch {
    raw = null;
  }

  if (!raw) {
    return null;
  }
  const text = String(raw);
  if (!text) {
    return null;
  }
  try {
    const obj = JSON.parse(text) as Partial<SharedSettings>;
    return {
      updatedAt: String(obj.updatedAt ?? ''),
      baseUrl: String(obj.baseUrl ?? ''),
      projectId: String(obj.projectId ?? ''),
      deviceId: String(obj.deviceId ?? ''),
      autoRefreshEnabled: !!obj.autoRefreshEnabled,
      alarmNotifyEnabled: typeof obj.alarmNotifyEnabled === 'boolean' ? obj.alarmNotifyEnabled : true,
    };
  } catch {
    return null;
  }
}

export async function subscribeSharedSettingsChanged(
  handler: (settings: SharedSettings) => void
): Promise<() => void> {
  const store: any = await getKvStore();
  const dkv: any = distributedKVStore as any;
  const type = dkv?.SubscribeType?.SUBSCRIBE_TYPE_ALL ?? 0;

  const listener = (data: any) => {
    try {
      const insert: string[] = data?.insertEntries ?? data?.insert ?? [];
      const update: string[] = data?.updateEntries ?? data?.update ?? [];
      const keys = [...(insert ?? []), ...(update ?? [])];
      if (!keys.includes(KEY_SETTINGS)) {
        return;
      }
    } catch {
      // ignore
    }
    loadSharedSettings()
      .then((s) => {
        if (s) handler(s);
      })
      .catch(() => {
        // ignore
      });
  };

  try {
    store?.on?.('dataChange', type, listener);
  } catch {
    // ignore
  }

  return () => {
    try {
      store?.off?.('dataChange', listener);
    } catch {
      // ignore
    }
  };
}

export async function startRemoteFlow(targetDeviceId: string, params: {
  baseUrl: string;
  projectId: string;
  deviceId: string;
}): Promise<void> {
  const ctx = GlobalContext.getAppContext() as any;
  const bundleName = getBundleName();
  const want: any = {
    deviceId: targetDeviceId,
    bundleName,
    abilityName: 'EntryAbility',
    parameters: {
      baseUrl: params.baseUrl,
      projectId: params.projectId,
      deviceId: params.deviceId,
    },
  };

  const ret = ctx?.startAbility?.(want);
  if (ret && typeof ret.then === 'function') {
    await ret;
    return;
  }
  await new Promise<void>((resolve, reject) => {
    ctx?.startAbility?.(want, (err: any) => {
      if (err) reject(err);
      else resolve();
    });
  });
}

