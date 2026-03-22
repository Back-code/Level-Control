#!/usr/bin/env node

import { createPublicKey, generateKeyPairSync } from 'crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const signingDir = join(root, 'signing');
const privateKeyPath = join(signingDir, 'release_private.pem');
const publicKeyPath = join(signingDir, 'release_public.pem');
const publicHeaderPath = join(root, 'include', 'ReleaseSigningPublicKey.h');

function writePublicHeader(publicPem) {
  const escapedPem = publicPem
    .replace(/\\/g, '\\\\')
    .replace(/"/g, '\\"')
    .replace(/\r?\n/g, '\\n');
  const header = [
    '#ifndef RELEASE_SIGNING_PUBLIC_KEY_H',
    '#define RELEASE_SIGNING_PUBLIC_KEY_H',
    '',
    '// Public key for manifest signature verification (ECDSA P-256).',
    `static const char kReleaseSigningPublicKeyPem[] = "${escapedPem}";`,
    '',
    '#endif // RELEASE_SIGNING_PUBLIC_KEY_H',
    ''
  ].join('\n');

  writeFileSync(publicHeaderPath, header);
}

mkdirSync(signingDir, { recursive: true });

if (!existsSync(privateKeyPath) || !existsSync(publicKeyPath)) {
  const { privateKey, publicKey } = generateKeyPairSync('ec', {
    namedCurve: 'prime256v1',
    privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
    publicKeyEncoding: { type: 'spki', format: 'pem' }
  });

  writeFileSync(privateKeyPath, privateKey, { mode: 0o600 });
  writeFileSync(publicKeyPath, publicKey);
}

const publicPem = readFileSync(publicKeyPath, 'utf8');

// Normalize key format and keep header in sync.
const normalizedPublicPem = createPublicKey(publicPem).export({ type: 'spki', format: 'pem' });
writeFileSync(publicKeyPath, normalizedPublicPem);
writePublicHeader(normalizedPublicPem);

process.stdout.write(`Release-Signatur-Keys bereit:\n- ${privateKeyPath}\n- ${publicKeyPath}\n- ${publicHeaderPath}\n`);
