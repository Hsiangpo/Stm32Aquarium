import { IOTDA_DEFAULTS } from '../config/iotdaDefaults';

export interface SessionSecrets {
  ak: string;
  sk: string;
  deviceSecret: string;
}

const secrets: SessionSecrets = {
  ak: IOTDA_DEFAULTS.ak,
  sk: IOTDA_DEFAULTS.sk,
  deviceSecret: IOTDA_DEFAULTS.deviceSecret,
};

export function setSessionSecrets(next: Partial<SessionSecrets>): void {
  if (typeof next.ak === 'string') {
    secrets.ak = next.ak;
  }
  if (typeof next.sk === 'string') {
    secrets.sk = next.sk;
  }
  if (typeof next.deviceSecret === 'string') {
    secrets.deviceSecret = next.deviceSecret;
  }
}

export function getSessionSecrets(): SessionSecrets {
  return {
    ak: secrets.ak,
    sk: secrets.sk,
    deviceSecret: secrets.deviceSecret,
  };
}

export function clearSessionSecrets(): void {
  secrets.ak = IOTDA_DEFAULTS.ak;
  secrets.sk = IOTDA_DEFAULTS.sk;
  secrets.deviceSecret = IOTDA_DEFAULTS.deviceSecret;
}
