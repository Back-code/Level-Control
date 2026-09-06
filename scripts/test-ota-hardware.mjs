#!/usr/bin/env node

import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const version = JSON.parse(readFileSync(join(root, 'version.json'), 'utf8'));
const versionStr = `${Number(version.major) || 0}.${Number(version.minor) || 0}.${Number(version.commit) || 0}`;
const pythonPath = join(root, '.venv', 'Scripts', 'python.exe');
const defaultHost = 'http://stand.local';
const host = (process.env.OTA_TEST_HOST || defaultHost).replace(/\/$/, '');

function detectSerialDevices() {
  if (!existsSync(pythonPath)) {
    throw new Error(`Python aus .venv nicht gefunden: ${pythonPath}`);
  }

  const result = spawnSync(pythonPath, ['-m', 'platformio', 'device', 'list', '--json-output'], {
    cwd: root,
    encoding: 'utf8'
  });
  if (result.status !== 0) {
    throw new Error(`PlatformIO konnte die USB-Geraete nicht auflisten:\n${result.stderr || result.stdout}`);
  }

  const parsed = JSON.parse(result.stdout || '[]');
  return Array.isArray(parsed) ? parsed : parsed.devices || [];
}

async function request(path, options = {}) {
  const response = await fetch(`${host}${path}`, {
    ...options,
    signal: AbortSignal.timeout(options.timeoutMs || 10000)
  });
  const text = await response.text();
  let payload = {};
  try {
    payload = JSON.parse(text || '{}');
  } catch (_) {
    payload = { raw: text };
  }
  return { response, payload };
}

async function startRepoUpdate() {
  const { response, payload } = await request('/api/update/repo', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ target: 'full' }),
    timeoutMs: 15000
  });
  assert.equal(response.status, 202, `Repo-OTA konnte nicht gestartet werden: ${payload.error || response.status}`);
  console.log('Hardware OTA gestartet: Repo-Update full');
}

async function waitForVersion() {
  for (let attempt = 0; attempt < 60; attempt += 1) {
    await new Promise(resolve => setTimeout(resolve, 1000));
    try {
      const { response, payload } = await request('/api/update/status');
      if (response.ok && payload.installedVersion === versionStr && !payload.inProgress) {
        return payload;
      }
    } catch (_) {
      // Device is expected to be unreachable during reboot.
    }
  }
  throw new Error(`Geraet ist nach OTA nicht mit Version ${versionStr} erreichbar`);
}

const devices = detectSerialDevices();
if (devices.length === 0) {
  console.log('Hardware OTA-Test uebersprungen: kein PlatformIO-USB-Geraet verbunden.');
  process.exit(0);
}

let initialStatus;
try {
  ({ payload: initialStatus } = await request('/api/update/status'));
} catch (error) {
  throw new Error(`USB-Geraet erkannt, aber OTA-Host ${host} ist nicht erreichbar: ${error.message}`);
}

assert.ok(initialStatus.installedVersion, 'OTA-Status enthaelt keine installierte Version');
assert.notEqual(initialStatus.installedVersion, versionStr, `Geraet laeuft bereits mit v${versionStr}; fuer einen HIL-Upgrade-Test muss die Zielversion neu sein`);

await startRepoUpdate();
const finalStatus = await waitForVersion();
console.log(`Hardware OTA Regression erfolgreich: ${initialStatus.installedVersion} -> ${finalStatus.installedVersion}`);
