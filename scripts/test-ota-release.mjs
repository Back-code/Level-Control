#!/usr/bin/env node

import assert from 'node:assert/strict';
import { createVerify } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const version = JSON.parse(readFileSync(join(root, 'version.json'), 'utf8'));
const versionStr = `${Number(version.major) || 0}.${Number(version.minor) || 0}.${Number(version.commit) || 0}`;
const releaseDir = join(root, 'release', `v${versionStr}`);
const buildDir = join(root, '.pio', 'build', 'esp32-c3-devkitm-1');
const trailerSize = 81;
const trailerMagic = Buffer.from('LCSIGV1!', 'ascii');
const publicKeyHeader = readFileSync(join(root, 'include', 'ReleaseSigningPublicKey.h'), 'utf8');
const publicKeyPem = publicKeyHeader
  .match(/-----BEGIN PUBLIC KEY-----[\s\S]+?-----END PUBLIC KEY-----/)?.[0]
  ?.replaceAll('\\n', '\n');

assert.ok(publicKeyPem, 'Public-Key konnte nicht aus ReleaseSigningPublicKey.h gelesen werden');

const assets = [
  {
    kind: 'app',
    rawName: 'firmware.bin',
    releaseName: `level-control-v${versionStr}-app.bin`,
    minimumSize: 65536,
    firstByte: 0xe9
  },
  {
    kind: 'webui',
    rawName: 'littlefs.bin',
    releaseName: `level-control-v${versionStr}-web-ui.bin`,
    minimumSize: 4096,
    firstByte: null
  }
];

function simulateCurrentOtaReceiver(buffer, expectedPayloadSize) {
  let tail = Buffer.alloc(0);
  let streamReceived = 0;
  let payloadReceived = 0;
  let offset = 0;
  const chunkPattern = [137, 4096, 8191, 512, 16384];
  let patternIndex = 0;

  while (offset < buffer.length) {
    const chunkSize = Math.min(chunkPattern[patternIndex % chunkPattern.length], buffer.length - offset);
    const chunk = buffer.subarray(offset, offset + chunkSize);
    offset += chunk.length;
    patternIndex += 1;
    streamReceived += chunk.length;
    tail = Buffer.concat([tail, chunk]);

    while (tail.length > trailerSize) {
      const processLength = tail.length - trailerSize;
      payloadReceived += processLength;
      tail = tail.subarray(processLength);
    }
  }

  assert.equal(streamReceived, buffer.length, 'HTTP-Stream wurde nicht vollständig konsumiert');
  assert.equal(tail.length, trailerSize, 'Signatur-Trailer ist nicht vollständig erhalten');
  assert.equal(payloadReceived, expectedPayloadSize, 'OTA-Nutzlast und erwartete Größe weichen ab');
  return tail;
}

for (const asset of assets) {
  const rawPath = join(buildDir, asset.rawName);
  const releasePath = join(releaseDir, asset.releaseName);
  assert.ok(existsSync(rawPath), `Build-Artefakt fehlt: ${asset.rawName}`);
  assert.ok(existsSync(releasePath), `Release-Artefakt fehlt: ${asset.releaseName}`);

  const raw = readFileSync(rawPath);
  const release = readFileSync(releasePath);
  assert.ok(raw.length >= asset.minimumSize, `${asset.kind}-Nutzlast ist unplausibel klein`);
  assert.equal(release.length, raw.length + trailerSize, `${asset.kind}-Datei hat keine erwartete Trailergröße`);
  assert.deepEqual(
    release.subarray(0, raw.length),
    raw,
    `${asset.kind}-Release-Payload entspricht nicht dem aktuellen Build-Artefakt`
  );
  if (asset.firstByte !== null) {
    assert.equal(release[0], asset.firstByte, `${asset.kind}-Datei ist kein ESP32-App-Image`);
  } else {
    assert.notEqual(release[0], 0xe9, `${asset.kind}-Datei sieht wie ein App-Image aus`);
  }

  const trailer = simulateCurrentOtaReceiver(release, raw.length);
  assert.deepEqual(trailer.subarray(0, trailerMagic.length), trailerMagic, `${asset.kind}-Trailer-Magic stimmt nicht`);
  const signatureLength = trailer[trailerMagic.length];
  assert.ok(signatureLength > 0 && signatureLength <= 72, `${asset.kind}-Signaturlänge ist ungültig`);

  const verifier = createVerify('SHA256');
  verifier.update(raw);
  verifier.end();
  assert.equal(
    verifier.verify(publicKeyPem, trailer.subarray(trailerMagic.length + 1, trailerMagic.length + 1 + signatureLength)),
    true,
    `${asset.kind}-Signatur ist ungültig`
  );

  console.log(`OTA regression OK: ${asset.releaseName} (${raw.length} byte payload)`);
}

console.log(`OTA regression erfolgreich für v${versionStr}`);
