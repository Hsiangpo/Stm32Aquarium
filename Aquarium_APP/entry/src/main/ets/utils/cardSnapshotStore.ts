import preferences from '@ohos.data.preferences';
import { GlobalContext } from './globalContext';

const PREF_NAME = 'aquarium_app_prefs';
const KEY_CARD_SNAPSHOT = 'cardSnapshot';

export interface CardSnapshot {
  updatedAt: string;
  deviceId: string;
  deviceStatus: string;
  alarmLevel: number;
  temperature: number;
  ph: number;
  tds: number;
  turbidity: number;
  waterLevel: number;
}

async function getPrefs() {
  const ctx = GlobalContext.getAppContext() as any;
  return await preferences.getPreferences(ctx, PREF_NAME);
}

export async function saveCardSnapshot(snapshot: CardSnapshot): Promise<void> {
  const prefs = await getPrefs();
  await prefs.put(KEY_CARD_SNAPSHOT, JSON.stringify(snapshot));
  await prefs.flush();
}

export async function loadCardSnapshot(): Promise<CardSnapshot | null> {
  const prefs = await getPrefs();
  const raw = (await prefs.get(KEY_CARD_SNAPSHOT, '')) as string;
  if (!raw) {
    return null;
  }
  try {
    const obj = JSON.parse(raw) as Partial<CardSnapshot>;
    if (!obj || typeof obj !== 'object') {
      return null;
    }
    return {
      updatedAt: String(obj.updatedAt ?? ''),
      deviceId: String(obj.deviceId ?? ''),
      deviceStatus: String(obj.deviceStatus ?? ''),
      alarmLevel: Number(obj.alarmLevel ?? 0),
      temperature: Number(obj.temperature ?? 0),
      ph: Number(obj.ph ?? 0),
      tds: Number(obj.tds ?? 0),
      turbidity: Number(obj.turbidity ?? 0),
      waterLevel: Number(obj.waterLevel ?? 0),
    };
  } catch {
    return null;
  }
}

