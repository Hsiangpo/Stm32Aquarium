import hilog from '@ohos.hilog';
import fileIo from '@ohos.file.fs';
import { GlobalContext } from './globalContext';

const LOG_DOMAIN = 0x4f5444; // "IOTD"
const LOG_TAG = 'AquariumApp';
const CA_RAWFILE_PATH = 'iotda_ca_bundle.pem';
const CA_SANDBOX_NAME = 'iotda_ca_bundle.pem';
const EMULATOR_CA_PATH = '/data/local/tmp/iotda_root.pem';

interface ResourceManagerLike {
  getRawFileContent(path: string): Promise<Uint8Array>;
}

interface AppContextLike {
  resourceManager: ResourceManagerLike;
  filesDir: string;
}

let caPathPromise: Promise<string> | null = null;

function getAppContext(): AppContextLike {
  return GlobalContext.getAppContext() as AppContextLike;
}

function toArrayBuffer(data: Uint8Array): ArrayBuffer {
  if (data.byteOffset === 0 && data.byteLength === data.buffer.byteLength) {
    return data.buffer;
  }
  return data.slice().buffer;
}

async function writeCaBundle(targetPath: string, content: Uint8Array): Promise<void> {
  const stream = await fileIo.createStream(targetPath, 'w');
  try {
    await stream.write(toArrayBuffer(content));
    await stream.flush();
  } finally {
    await stream.close();
  }
}

async function prepareCaBundle(): Promise<string> {
  const ctx = getAppContext();
  const targetPath = `${ctx.filesDir}/${CA_SANDBOX_NAME}`;
  const content = await ctx.resourceManager.getRawFileContent(CA_RAWFILE_PATH);
  await writeCaBundle(targetPath, content);
  if (fileIo.accessSync(EMULATOR_CA_PATH)) {
    hilog.info(LOG_DOMAIN, LOG_TAG, '%{public}s', `使用模拟器外部CA ${EMULATOR_CA_PATH}`);
    return EMULATOR_CA_PATH;
  }
  hilog.info(LOG_DOMAIN, LOG_TAG, '%{public}s', `自定义CA已写入 ${targetPath}`);
  return targetPath;
}

export async function ensureIotdaCaPath(): Promise<string> {
  if (!caPathPromise) {
    caPathPromise = prepareCaBundle().catch((err: Error) => {
      caPathPromise = null;
      throw err;
    });
  }
  return await caPathPromise;
}
