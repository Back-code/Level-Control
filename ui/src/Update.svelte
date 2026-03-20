<script>
  import { onMount } from 'svelte';

  export let currentVersion = '0.00.000';

  let manifest = null;
  let manifestError = '';
  let status = {
    inProgress: false,
    success: false,
    rebootPending: false,
    source: '',
    target: '',
    phase: 'idle',
    message: '',
    availableVersion: '',
    received: 0,
    total: 0,
    firmwareMaxSize: 0,
    webuiMaxSize: 0
  };

  let firmwareFile = null;
  let webUiFile = null;

  function compareVersions(left, right) {
    const leftParts = left.split('.').map(part => Number(part) || 0);
    const rightParts = right.split('.').map(part => Number(part) || 0);
    for (let index = 0; index < 3; index += 1) {
      const delta = (leftParts[index] || 0) - (rightParts[index] || 0);
      if (delta !== 0) return delta;
    }
    return 0;
  }

  function humanSize(bytes) {
    if (!bytes) return 'unbekannt';
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
  }

  async function loadManifest() {
    try {
      const response = await fetch('/api/update/manifest');
      const payload = await response.json();
      if (!response.ok) {
        manifest = null;
        manifestError = payload.error || 'Manifest konnte nicht geladen werden';
        return;
      }
      manifest = payload;
      manifestError = '';
    } catch (_) {
      manifest = null;
      manifestError = 'Manifest konnte nicht geladen werden';
    }
  }

  async function loadStatus() {
    try {
      const response = await fetch('/api/update/status');
      if (response.ok) {
        status = await response.json();
      }
    } catch (_) {
      // Status-Polling ist optional.
    }
  }

  async function startRepoUpdate(target) {
    const label = target === 'full' ? 'Firmware und Web-UI' : 'nur die Firmware';
    if (!confirm(`Soll ${label} aus dem neuesten Release geladen werden?`)) {
      return;
    }

    const response = await fetch('/api/update/repo', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ target })
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      alert(payload.error || 'Repo-Update konnte nicht gestartet werden');
      return;
    }

    await loadStatus();
  }

  function validateLocalFile(target, file) {
    if (!file) {
      return 'Bitte zuerst eine .bin-Datei auswählen.';
    }

    const lowerName = file.name.toLowerCase();
    if (!lowerName.endsWith('.bin')) {
      return 'Es werden nur .bin-Dateien akzeptiert.';
    }

    if (lowerName.includes('bootloader') || lowerName.includes('partition')) {
      return 'Bootloader- und Partitionsdateien dürfen hier nicht hochgeladen werden.';
    }

    if (target === 'firmware') {
      if (lowerName.includes('littlefs') || lowerName.includes('web-ui')) {
        return 'Diese Datei sieht nach einer Web-UI-Datei aus. Bitte die Firmware-BIN wählen.';
      }
      if (status.firmwareMaxSize && file.size > status.firmwareMaxSize) {
        return 'Die Datei ist größer als die verfügbare Firmware-Partition.';
      }
      if (file.size < 65536) {
        return 'Die Datei ist für eine Firmware unplausibel klein.';
      }
    } else {
      if (lowerName.includes('firmware') || lowerName.includes('app')) {
        return 'Diese Datei sieht nach einer Firmware aus. Bitte die Web-UI-BIN wählen.';
      }
      if (status.webuiMaxSize && file.size > status.webuiMaxSize) {
        return 'Die Datei ist größer als die LittleFS-Partition.';
      }
      if (file.size < 4096) {
        return 'Die Datei ist für eine Web-UI unplausibel klein.';
      }
    }

    return '';
  }

  async function uploadLocal(target) {
    const file = target === 'firmware' ? firmwareFile : webUiFile;
    const error = validateLocalFile(target, file);
    if (error) {
      alert(error);
      return;
    }

    const label = target === 'firmware' ? 'Firmware' : 'Web-UI';
    if (!confirm(`Soll die lokale ${label}-Datei jetzt installiert werden?`)) {
      return;
    }

    const formData = new FormData();
    formData.append('file', file);

    const response = await fetch(`/api/update/upload/${target}`, {
      method: 'POST',
      body: formData
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      alert(payload.error || `${label}-Upload fehlgeschlagen`);
      return;
    }

    await loadStatus();
  }

  $: progressPercent = status.total > 0 ? Math.min(100, Math.round((status.received / status.total) * 100)) : 0;
  $: repoUpdateAvailable = manifest && compareVersions(manifest.version, currentVersion) > 0;

  onMount(() => {
    loadManifest();
    loadStatus();
    const interval = setInterval(loadStatus, 1000);
    return () => clearInterval(interval);
  });
</script>

<section class="update-grid">
  <article class="status-card hero">
    <div>
      <span class="eyebrow">Aktueller Stand</span>
      <h2>Update-Zentrale</h2>
      <p>
        Aktuelle Version: <strong>v{currentVersion}</strong>
        {#if manifest}
          <span class="latest-copy">Neueste Release: <strong>v{manifest.version}</strong></span>
        {/if}
      </p>
    </div>

    {#if manifest}
      <div class="manifest-box">
        <div>
          <span>Firmware</span>
          <strong>{manifest.assets?.firmware?.name || 'nicht vorhanden'}</strong>
          <small>{humanSize(manifest.assets?.firmware?.size)}</small>
        </div>
        <div>
          <span>Web-UI</span>
          <strong>{manifest.assets?.webui?.name || 'nicht vorhanden'}</strong>
          <small>{humanSize(manifest.assets?.webui?.size)}</small>
        </div>
      </div>
    {:else}
      <p class="warning">{manifestError || 'Noch kein Release-Manifest gefunden.'}</p>
    {/if}

    <div class="status-box" class:busy={status.inProgress}>
      <div class="status-head">
        <span>Status</span>
        <strong>{status.phase || 'idle'}</strong>
      </div>
      <p>{status.message || 'Kein Update aktiv.'}</p>
      <div class="progress-track">
        <div class="progress-fill" style={`width:${progressPercent}%`}></div>
      </div>
      <small>{status.received} / {status.total || 0} Bytes</small>
    </div>
  </article>

  <article class="action-card">
    <span class="eyebrow">1. Repo</span>
    <h3>OTA nur Firmware</h3>
    <p>Lädt die App-Binärdatei aus dem neuesten GitHub-Release und schreibt nur die Firmware-Partition.</p>
    <button on:click={() => startRepoUpdate('firmware')} disabled={!repoUpdateAvailable || status.inProgress}>
      Firmware aus Repo installieren
    </button>
    {#if manifest && !repoUpdateAvailable}
      <small>Keine neuere Release als v{currentVersion} gefunden.</small>
    {/if}
  </article>

  <article class="action-card">
    <span class="eyebrow">2. Repo</span>
    <h3>Komplettes Update</h3>
    <p>Installiert nacheinander Web-UI und Firmware aus dem Manifest des neuesten GitHub-Releases.</p>
    <button on:click={() => startRepoUpdate('full')} disabled={!repoUpdateAvailable || status.inProgress}>
      Firmware und Web-UI installieren
    </button>
    {#if manifest?.releaseUrl}
      <a class="release-link" href={manifest.releaseUrl} target="_blank" rel="noopener noreferrer">Release im Browser öffnen</a>
    {/if}
  </article>

  <article class="action-card upload-card">
    <span class="eyebrow">3. Lokal</span>
    <h3>Firmware per BIN</h3>
    <p>Nur gültige ESP32-Firmware-Dateien werden akzeptiert. Bootloader- und Partitionsdateien sind gesperrt.</p>
    <input type="file" accept=".bin" on:change={(event) => firmwareFile = event.currentTarget.files?.[0] || null} />
    <button on:click={() => uploadLocal('firmware')} disabled={status.inProgress}>Firmware hochladen</button>
    <small>Maximale Größe: {humanSize(status.firmwareMaxSize)}</small>
  </article>

  <article class="action-card upload-card">
    <span class="eyebrow">4. Lokal</span>
    <h3>Web-UI per BIN</h3>
    <p>Die LittleFS-Datei wird separat geprüft und darf keine Firmware-Signatur enthalten.</p>
    <input type="file" accept=".bin" on:change={(event) => webUiFile = event.currentTarget.files?.[0] || null} />
    <button on:click={() => uploadLocal('webui')} disabled={status.inProgress}>Web-UI hochladen</button>
    <small>Maximale Größe: {humanSize(status.webuiMaxSize)}</small>
  </article>
</section>

<style>
  .update-grid {
    display: grid;
    gap: 16px;
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .status-card,
  .action-card {
    border: 1px solid var(--surface-border);
    border-radius: 18px;
    background: var(--card-grad);
    padding: 18px;
    box-shadow: var(--shadow);
  }

  .hero {
    grid-column: 1 / -1;
    display: grid;
    gap: 16px;
  }

  .eyebrow {
    display: inline-block;
    margin-bottom: 6px;
    color: var(--text-muted);
    font-size: 0.74rem;
    letter-spacing: 0.18em;
    text-transform: uppercase;
  }

  h2,
  h3 {
    margin: 0 0 8px;
  }

  p {
    margin: 0;
    color: var(--text-muted);
    line-height: 1.5;
  }

  strong {
    color: var(--text-main);
  }

  .latest-copy {
    display: inline-block;
    margin-left: 12px;
  }

  .manifest-box {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 12px;
  }

  .manifest-box div,
  .status-box {
    border: 1px solid var(--surface-border);
    border-radius: 14px;
    background: var(--surface-2);
    padding: 14px;
  }

  .manifest-box span,
  .status-head span {
    display: block;
    color: var(--text-muted);
    font-size: 0.8rem;
    margin-bottom: 4px;
  }

  .manifest-box small,
  small {
    color: var(--text-muted);
  }

  .status-box.busy {
    outline: 1px solid rgba(98, 184, 221, 0.35);
  }

  .status-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
    margin-bottom: 10px;
  }

  .progress-track {
    height: 10px;
    margin: 10px 0 6px;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.08);
    overflow: hidden;
  }

  .progress-fill {
    height: 100%;
    border-radius: 999px;
    background: linear-gradient(90deg, #57b67b 0%, #62b8dd 100%);
    transition: width 0.2s ease;
  }

  .action-card {
    display: grid;
    gap: 12px;
    align-content: start;
  }

  button,
  .release-link {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-height: 42px;
    border-radius: 12px;
    border: 1px solid var(--surface-border);
    background: var(--button-bg);
    color: var(--button-text);
    font-weight: 700;
    text-decoration: none;
    cursor: pointer;
    transition: transform 0.18s ease, background 0.18s ease;
  }

  button:hover:not(:disabled),
  .release-link:hover {
    transform: translateY(-1px);
    background: var(--button-active-bg);
    color: var(--button-active-text);
  }

  button:disabled {
    opacity: 0.55;
    cursor: not-allowed;
  }

  input[type='file'] {
    width: 100%;
    color: var(--text-main);
  }

  .warning {
    color: #ffb58f;
  }

  @media (max-width: 840px) {
    .update-grid,
    .manifest-box {
      grid-template-columns: 1fr;
    }
  }
</style>