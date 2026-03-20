#!/usr/bin/env node
// Bump-Version-Skript: wird vom pre-commit-Hook aufgerufen
// Liest version.json, erhöht den commit-Zähler und schreibt zurück.

import { readFileSync, writeFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { join, dirname } from 'path';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const versionPath = join(root, 'version.json');

const v = JSON.parse(readFileSync(versionPath, 'utf8'));

v.commit++;
if (v.commit > 100) {
  v.commit = 0;
  v.minor++;
}
if (v.minor > 10) {
  v.minor = 0;
  // major bleibt – wird vom Nutzer manuell gesetzt
}

writeFileSync(versionPath, JSON.stringify(v, null, 2) + '\n');

const str = `${v.major}.${String(v.minor).padStart(2, '0')}.${String(v.commit).padStart(3, '0')}`;
process.stdout.write(`Version: ${str}\n`);
