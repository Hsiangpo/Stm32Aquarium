const crypto = require('crypto');
const mqtt = require('mqtt');

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i += 1) {
    const item = argv[i];
    if (!item.startsWith('--')) {
      continue;
    }
    const eq = item.indexOf('=');
    if (eq >= 0) {
      const key = item.slice(2, eq);
      const val = item.slice(eq + 1);
      args[key] = val;
    } else {
      const key = item.slice(2);
      const next = argv[i + 1];
      if (next && !next.startsWith('--')) {
        args[key] = next;
        i += 1;
      } else {
        args[key] = 'true';
      }
    }
  }
  return args;
}

function usage() {
  return [
    'Usage:',
    '  node simulate.js --device-id <id> --device-secret <secret> [options]',
    '',
    'Options:',
    '  --host <host>           MQTT host (default: 8cee850016.st1.iotda-device.cn-north-4.myhuaweicloud.com)',
    '  --port <port>           MQTT port (default: 1883)',
    '  --interval <seconds>    Publish interval (default: 15)',
    '  --once                  Publish once then exit',
    '',
    'Env vars:',
    '  IOTDA_DEVICE_ID, IOTDA_DEVICE_SECRET, IOTDA_DEVICE_HOST, IOTDA_DEVICE_PORT, IOTDA_INTERVAL',
  ].join('\n');
}

function envOrArg(args, name, envName, fallback) {
  if (args[name] !== undefined) return args[name];
  if (process.env[envName]) return process.env[envName];
  return fallback;
}

function utcHourStamp(date = new Date()) {
  const iso = date.toISOString();
  return iso.slice(0, 13).replace(/[-T:]/g, '');
}

function hmacSha256Hex(key, message) {
  return crypto.createHmac('sha256', key).update(message).digest('hex');
}

function buildAuth(deviceId, deviceSecret) {
  const timestamp = utcHourStamp();
  const clientId = `${deviceId}_0_1_${timestamp}`;
  const username = deviceId;
  // IoTDA uses HMACSHA256(secret, timestamp) -> HMAC(key=timestamp, msg=secret)
  const password = hmacSha256Hex(timestamp, deviceSecret);
  return { clientId, username, password, timestamp };
}

function rand(min, max, decimals = 0) {
  const value = min + Math.random() * (max - min);
  return Number(value.toFixed(decimals));
}

function randBool(prob = 0.1) {
  return Math.random() < prob;
}

function buildPayload() {
  const feeding = randBool(0.05);
  const alarmLevel = randBool(0.05) ? 2 : randBool(0.1) ? 1 : 0;
  const props = {
    temperature: rand(24.0, 28.0, 2),
    ph: rand(6.6, 7.4, 2),
    tds: rand(200, 450, 0),
    turbidity: rand(5, 20, 0),
    water_level: rand(60, 95, 0),
    heater: randBool(0.3),
    pump_in: randBool(0.2),
    pump_out: randBool(0.2),
    auto_mode: true,
    feed_countdown: feeding ? 0 : rand(0, 7200, 0),
    feeding_in_progress: feeding,
    alarm_level: alarmLevel,
    alarm_muted: false,
  };

  return JSON.stringify({
    services: [
      {
        service_id: 'Aquarium',
        properties: props,
      },
    ],
  });
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const deviceId = envOrArg(args, 'device-id', 'IOTDA_DEVICE_ID', '');
  const deviceSecret = envOrArg(args, 'device-secret', 'IOTDA_DEVICE_SECRET', '');
  const host = envOrArg(args, 'host', 'IOTDA_DEVICE_HOST', '8cee850016.st1.iotda-device.cn-north-4.myhuaweicloud.com');
  const portRaw = envOrArg(args, 'port', 'IOTDA_DEVICE_PORT', '1883');
  const intervalRaw = envOrArg(args, 'interval', 'IOTDA_INTERVAL', '15');
  const once = args.once === 'true' || args.once === '';

  const port = Number(portRaw);
  const intervalSec = Number(intervalRaw);

  if (!deviceId || !deviceSecret) {
    console.error('Missing device-id or device-secret.');
    console.error(usage());
    process.exit(1);
  }
  if (!Number.isFinite(port) || port <= 0) {
    console.error('Invalid port.');
    process.exit(1);
  }
  if (!Number.isFinite(intervalSec) || intervalSec <= 0) {
    console.error('Invalid interval.');
    process.exit(1);
  }

  const auth = buildAuth(deviceId, deviceSecret);
  const topic = `$oc/devices/${deviceId}/sys/properties/report`;

  console.log(`Connecting to mqtt://${host}:${port}`);
  console.log(`clientId=${auth.clientId} timestamp=${auth.timestamp}`);

  const client = mqtt.connect({
    host,
    port,
    protocol: 'mqtt',
    clientId: auth.clientId,
    username: auth.username,
    password: auth.password,
    keepalive: 120,
    reconnectPeriod: 0,
  });

  let timer = null;

  function publishOnce() {
    const payload = buildPayload();
    client.publish(topic, payload, { qos: 0 }, (err) => {
      if (err) {
        console.error('Publish error:', err.message || err);
      } else {
        console.log('Published:', payload);
      }
      if (once) {
        client.end(true, () => process.exit(0));
      }
    });
  }

  client.on('connect', () => {
    console.log('MQTT connected.');
    publishOnce();
    if (!once) {
      timer = setInterval(publishOnce, intervalSec * 1000);
    }
  });

  client.on('error', (err) => {
    console.error('MQTT error:', err?.message || err);
    process.exit(1);
  });

  client.on('close', () => {
    if (timer) {
      clearInterval(timer);
      timer = null;
    }
  });

  process.on('SIGINT', () => {
    if (timer) clearInterval(timer);
    client.end(true, () => process.exit(0));
  });
}

main().catch((err) => {
  console.error(err?.message || err);
  process.exit(1);
});
