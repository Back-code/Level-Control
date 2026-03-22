// SMTP-Provider-Profile für die Push-Konfiguration.
// Pflege zentral hier: neue Provider einfach an die Liste anhängen.

export const SMTP_PROVIDERS = [
  {
    id: 'custom',
    label: 'Benutzerdefiniert',
    host: '',
    port: 587,
    security: 'none'
  },
  {
    id: 'hetzner',
    label: 'Hetzner',
    host: 'mail.your-server.de',
    port: 465,
    security: 'ssl',
    certOk: true
  },
  {
    id: 'ionos',
    label: 'IONOS',
    host: 'smtp.ionos.de',
    port: 465,
    security: 'ssl',
    certOk: true
  },
  {
    id: 'strato',
    label: 'Strato',
    host: 'smtp.strato.de',
    port: 465,
    security: 'ssl',
    certOk: true
  },
  {
    id: 'gmail',
    label: 'Gmail',
    host: 'smtp.gmail.com',
    port: 465,
    security: 'ssl',
    certNote: 'Gmail verwendet Google-eigene Root-CAs (GTS Root R1), die nicht im Gerät hinterlegt sind. "Zertifikat überspringen" aktivieren.'
  },
  {
    id: 'outlook',
    label: 'Outlook / Microsoft 365',
    host: 'smtp.office365.com',
    port: 587,
    security: 'starttls',
    certNote: 'Microsoft erfordert STARTTLS (Port 587). STARTTLS wird vom Gerät aktuell nicht unterstützt – Outlook ist derzeit nicht kompatibel.'
  },
  {
    id: 'gmx',
    label: 'GMX',
    host: 'mail.gmx.net',
    port: 465,
    security: 'ssl',
    certNote: 'GMX verwendet T-TeleSec-Zertifikate, die nicht im Gerät hinterlegt sind. "Zertifikat überspringen" aktivieren.'
  },
  {
    id: 'webde',
    label: 'Web.de',
    host: 'smtp.web.de',
    port: 465,
    security: 'ssl',
    certNote: 'Web.de verwendet T-TeleSec-Zertifikate, die nicht im Gerät hinterlegt sind. "Zertifikat überspringen" aktivieren.'
  }
];

/**
 * Erkennt automatisch den Provider anhand von Host und Port.
 * Gibt 'custom' zurück, wenn kein bekannter Provider passt.
 */
export function detectProvider(host, port) {
  if (!host) return 'custom';
  const lc = String(host).toLowerCase().trim();
  const found = SMTP_PROVIDERS.find(
    (p) => p.host && p.host.toLowerCase() === lc && p.port === Number(port)
  );
  return found ? found.id : 'custom';
}
