#!/usr/bin/env node

import { createHash } from 'crypto';
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const buildDir = join(root, '.pio', 'build', 'esp32-c3-devkitm-1');
const version = JSON.parse(readFileSync(join(root, 'version.json'), 'utf8'));
const versionStr = `${version.major}.${String(version.minor).padStart(2, '0')}.${String(version.commit).padStart(3, '0')}`;
const releaseDir = join(root, 'release', `v${versionStr}`);

const assets = [
  { source: 'bootloader.bin', target: `salzstand-v${versionStr}-bootloader.bin`, kind: 'bootloader' },
  { source: 'partitions.bin', target: `salzstand-v${versionStr}-partitions.bin`, kind: 'partitions' },
  { source: 'firmware.bin', target: `salzstand-v${versionStr}-app.bin`, kind: 'firmware' },
  { source: 'littlefs.bin', target: `salzstand-v${versionStr}-web-ui.bin`, kind: 'webui' }
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

const firmware = copiedAssets.find((asset) => asset.kind === 'firmware');
const webui = copiedAssets.find((asset) => asset.kind === 'webui');

const manifest = {
  version: versionStr,
  releaseUrl: `https://github.com/Back-code/Salzstand/releases/tag/v${versionStr}`,
  assets: {
    firmware: {
      name: firmware.target,
      url: `https://github.com/Back-code/Salzstand/releases/download/v${versionStr}/${firmware.target}`,
      sha256: firmware.sha256,
      size: firmware.size
    },
    webui: {
      name: webui.target,
      url: `https://github.com/Back-code/Salzstand/releases/download/v${versionStr}/${webui.target}`,
      sha256: webui.sha256,
      size: webui.size
    }
  }
};

writeFileSync(join(releaseDir, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');

process.stdout.write(`Release-Artefakte erstellt: release/v${versionStr}\n`);