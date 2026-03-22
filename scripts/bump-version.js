#!/usr/bin/env node
// Bump-Version-Skript: wird vom pre-commit-Hook aufgerufen
// Liest version.json, erhöht den commit-Zähler und schreibt zurück.

import { readFileSync, writeFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { join, dirname } from 'path';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const versionPath = join(root, 'version.json');

const v = JSON.parse(readFileSync(versionPath, 'utf8'));

v.major = Number(v.major) || 0;
v.minor = Number(v.minor) || 0;
v.commit = Number(v.commit) || 0;

v.commit += 1;
if (v.commit > 99) {
  v.commit = 0;
  v.minor += 1;
}
if (v.minor > 9) {
  v.minor = 0;
  v.major += 1;
}
if (v.major > 99) {
  v.major = 0;
}

writeFileSync(versionPath, JSON.stringify(v, null, 2) + '\n');

const str = `${v.major}.${v.minor}.${v.commit}`;
process.stdout.write(`Version: ${str}\n`);
