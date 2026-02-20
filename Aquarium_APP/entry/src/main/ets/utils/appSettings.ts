import preferences from '@ohos.data.preferences';
import { GlobalContext } from './globalContext';

const PREF_NAME = 'aquarium_app_prefs';

const KEY_BASE_URL = 'baseUrl';
const KEY_PROJECT_ID = 'projectId';
const KEY_DEVICE_ID = 'deviceId';
const KEY_SAVE_CREDENTIALS = 'saveCredentials';
const KEY_SAVE_DEVICE_SECRET = 'saveDeviceSecret';
const KEY_AUTO_REFRESH_ENABLED = 'autoRefreshEnabled';
const KEY_ALARM_NOTIFY_ENABLED = 'alarmNotifyEnabled';
const KEY_DISTRIBUTED_SYNC_ENABLED = 'distributedSyncEnabled';
const KEY_AK = 'ak';
const KEY_SK = 'sk';
const KEY_DEVICE_SECRET = 'deviceSecret';

export interface AppSettings {
  baseUrl: string;
  projectId: string;
  deviceId: string;
  saveCredentials: boolean;
  saveDeviceSecret: boolean;
  autoRefreshEnabled: boolean;
  alarmNotifyEnabled: boolean;
  distributedSyncEnabled: boolean;
  ak: string;
  sk: string;
  deviceSecret: string;
}

async function getPrefs() {
  const ctx = GlobalContext.getAppContext() as any;
  return await preferences.getPreferences(ctx, PREF_NAME);
}

export async function loadAppSettings(): Promise<Partial<AppSettings>> {
  const prefs = await getPrefs();

  const baseUrl = (await prefs.get(KEY_BASE_URL, '')) as string;
  const projectId = (await prefs.get(KEY_PROJECT_ID, '')) as string;
  const deviceId = (await prefs.get(KEY_DEVICE_ID, '')) as string;
  const saveCredentials = (await prefs.get(KEY_SAVE_CREDENTIALS, false)) as boolean;
  const saveDeviceSecret = (await prefs.get(KEY_SAVE_DEVICE_SECRET, false)) as boolean;
  const autoRefreshEnabled = (await prefs.get(KEY_AUTO_REFRESH_ENABLED, true)) as boolean;
  const alarmNotifyEnabled = (await prefs.get(KEY_ALARM_NOTIFY_ENABLED, true)) as boolean;
  const distributedSyncEnabled = (await prefs.get(KEY_DISTRIBUTED_SYNC_ENABLED, false)) as boolean;

  let ak = '';
  let sk = '';
  if (saveCredentials) {
    ak = (await prefs.get(KEY_AK, '')) as string;
    sk = (await prefs.get(KEY_SK, '')) as string;
  }

  let deviceSecret = '';
  if (saveDeviceSecret) {
    deviceSecret = (await prefs.get(KEY_DEVICE_SECRET, '')) as string;
  }

  return {
    baseUrl,
    projectId,
    deviceId,
    saveCredentials,
    saveDeviceSecret,
    autoRefreshEnabled,
    alarmNotifyEnabled,
    distributedSyncEnabled,
    ak,
    sk,
    deviceSecret,
  };
}

export async function saveAppSettings(settings: AppSettings): Promise<void> {
  const prefs = await getPrefs();

  await prefs.put(KEY_BASE_URL, settings.baseUrl);
  await prefs.put(KEY_PROJECT_ID, settings.projectId);
  await prefs.put(KEY_DEVICE_ID, settings.deviceId);
  await prefs.put(KEY_SAVE_CREDENTIALS, settings.saveCredentials);
  await prefs.put(KEY_SAVE_DEVICE_SECRET, settings.saveDeviceSecret);
  await prefs.put(KEY_AUTO_REFRESH_ENABLED, settings.autoRefreshEnabled);
  await prefs.put(KEY_ALARM_NOTIFY_ENABLED, settings.alarmNotifyEnabled);
  await prefs.put(KEY_DISTRIBUTED_SYNC_ENABLED, settings.distributedSyncEnabled);

  if (settings.saveCredentials) {
    await prefs.put(KEY_AK, settings.ak);
    await prefs.put(KEY_SK, settings.sk);
  } else {
    await prefs.put(KEY_AK, '');
    await prefs.put(KEY_SK, '');
  }

  if (settings.saveDeviceSecret) {
    await prefs.put(KEY_DEVICE_SECRET, settings.deviceSecret);
  } else {
    await prefs.put(KEY_DEVICE_SECRET, '');
  }

  await prefs.flush();
}

export async function saveAutoRefreshEnabled(enabled: boolean): Promise<void> {
  const prefs = await getPrefs();
  await prefs.put(KEY_AUTO_REFRESH_ENABLED, enabled);
  await prefs.flush();
}

export async function saveAlarmNotifyEnabled(enabled: boolean): Promise<void> {
  const prefs = await getPrefs();
  await prefs.put(KEY_ALARM_NOTIFY_ENABLED, enabled);
  await prefs.flush();
}

export async function saveDistributedSyncEnabled(enabled: boolean): Promise<void> {
  const prefs = await getPrefs();
  await prefs.put(KEY_DISTRIBUTED_SYNC_ENABLED, enabled);
  await prefs.flush();
}
