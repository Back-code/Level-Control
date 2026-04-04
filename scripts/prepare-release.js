#!/usr/bin/env node

import { createHash, createSign } from 'crypto';
import { spawnSync } from 'child_process';
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const buildDir = join(root, '.pio', 'build', 'esp32-c3-devkitm-1');
const version = JSON.parse(readFileSync(join(root, 'version.json'), 'utf8'));
const versionStr = `${Number(version.major) || 0}.${Number(version.minor) || 0}.${Number(version.commit) || 0}`;
const releaseDir = join(root, 'release', `v${versionStr}`);
const privateKeyPath = process.env.RELEASE_SIGNING_PRIVATE_KEY || join(root, 'signing', 'release_private.pem');

function buildManifestSigningPayload(manifest) {
  const app = manifest.assets.app;
  const webui = manifest.assets.webui;
  return [
    `version=${manifest.version}`,
    `releaseUrl=${manifest.releaseUrl}`,
    `app.name=${app.name}`,
    `app.url=${app.url}`,
    `app.sha256=${app.sha256}`,
    `app.size=${app.size}`,
    `webui.name=${webui.name}`,
    `webui.url=${webui.url}`,
    `webui.sha256=${webui.sha256}`,
    `webui.size=${webui.size}`
  ].join('\n');
}

function signManifest(manifest) {
  if (!existsSync(privateKeyPath)) {
    throw new Error(
      `Signatur-Schlüssel fehlt: ${privateKeyPath}\n` +
      'Bitte zuerst ausführen: node scripts/generate-release-signing-keys.js'
    );
  }

  const privateKeyPem = readFileSync(privateKeyPath, 'utf8');
  const payload = buildManifestSigningPayload(manifest);
  const signer = createSign('SHA256');
  signer.update(payload);
  signer.end();

  return {
    algorithm: 'ECDSA_P256_SHA256',
    value: signer.sign(privateKeyPem, 'base64')
  };
}

function runGit(args) {
  const result = spawnSync('git', args, {
    cwd: root,
    encoding: 'utf8'
  });
  if (result.status !== 0) {
    return '';
  }
  return (result.stdout || '').trim();
}

function fetchTags() {
  spawnSync('git', ['fetch', '--tags'], { cwd: root, encoding: 'utf8' });
}

function getPreviousTag(currentTag) {
  const tagOutput = runGit(['tag', '--sort=v:refname']);
  if (!tagOutput) {
    return '';
  }
  const tags = tagOutput
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => /^v\d+\.\d+\.\d+$/.test(line));

  const previous = tags.filter((tag) => tag !== currentTag).pop();
  return previous || '';
}

function getChangelogLines(previousTag) {
  const range = previousTag ? `${previousTag}..HEAD` : 'HEAD';
  const logOutput = runGit(['log', '--pretty=format:%s', range]);
  if (!logOutput) {
    return [];
  }
  return logOutput
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => `- ${line}`);
}

fetchTags();

const assets = [
  { source: 'bootloader.bin', target: `level-control-v${versionStr}-bootloader.bin`, kind: 'bootloader' },
  { source: 'partitions.bin', target: `level-control-v${versionStr}-partitions.bin`, kind: 'partitions' },
  { source: 'firmware.bin', target: `level-control-v${versionStr}-app.bin`, kind: 'app' },
  { source: 'littlefs.bin', target: `level-control-v${versionStr}-web-ui.bin`, kind: 'webui' }
];

mkdirSync(releaseDir, { recursive: true });

const copiedAssets = assets.map((asset) => {
  const sourcePath = join(buildDir, asset.source);
  const targetPath = join(releaseDir, asset.target);
  if (!existsSync(sourcePath)) {
    throw new Error(`Fehlendes Build-Artefakt: ${asset.source}`);
  }
  copyFileSync(sourcePath, targetPath);
  const buffer = readFileSync(targetPath);
  const sha256 = createHash('sha256').update(buffer).digest('hex');
  return {
    ...asset,
    size: buffer.length,
    sha256,
    targetPath
  };
});

const sums = copiedAssets
  .map((asset) => `${asset.sha256}  ${asset.target}`)
  .join('\n') + '\n';
writeFileSync(join(releaseDir, 'SHA256SUMS.txt'), sums);

const app = copiedAssets.find((asset) => asset.kind === 'app');
const webui = copiedAssets.find((asset) => asset.kind === 'webui');
const currentTag = `v${versionStr}`;
const previousTag = getPreviousTag(currentTag);
const changelogLines = getChangelogLines(previousTag);

const manifest = {
  version: versionStr,
  releaseUrl: `https://github.com/Back-code/Level-Control/releases/tag/v${versionStr}`,
  assets: {
    app: {
      name: app.target,
      url: `https://github.com/Back-code/Level-Control/releases/download/v${versionStr}/${app.target}`,
      sha256: app.sha256,
      size: app.size
    },
    webui: {
      name: webui.target,
      url: `https://github.com/Back-code/Level-Control/releases/download/v${versionStr}/${webui.target}`,
      sha256: webui.sha256,
      size: webui.size
    }
  },
  signature: null
};

manifest.signature = signManifest(manifest);

writeFileSync(join(releaseDir, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');

const releaseNotes = [
  `## Level-Control v${versionStr}`,
  '',
  'Release-Assets für OTA und manuelles Flashen.',
  '',
  previousTag ? `Changelog seit ${previousTag}:` : 'Changelog:',
  ...(changelogLines.length ? changelogLines : ['- Keine Änderungen gefunden.']),
  '',
  'Enthalten:',
  `- ${app.target}`,
  `- ${webui.target}`,
  `- ${copiedAssets.find((asset) => asset.kind === 'bootloader').target}`,
  `- ${copiedAssets.find((asset) => asset.kind === 'partitions').target}`,
  '- manifest.json',
  '- SHA256SUMS.txt',
  '',
  'Hinweis:',
  'Für OTA werden app.bin, web-ui.bin und manifest.json verwendet.',
  'Für vollständiges Recovery per Kabel stehen zusätzlich bootloader.bin und partitions.bin bereit.',
  '',
  'Standard-Checks vor Veröffentlichung:',
  '- App und LittleFS erfolgreich gebaut',
  '- manifest.json verweist auf denselben Tag',
  '- SHA256SUMS.txt liegt bei',
  '- OTA-Test aus der Web-UI gegen das veröffentlichte Release durchgeführt'
].join('\n') + '\n';

writeFileSync(join(releaseDir, 'release-notes.txt'), releaseNotes);

process.stdout.write(`Release-Artefakte erstellt: release/v${versionStr}\n`);
