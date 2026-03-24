<script>
  import { onMount } from 'svelte';
  import { confirmAction, showNotice } from './dialogStore.js';

  export let currentVersion = '0.0.0';

  // Tatsächlich laufende Version – kommt vom Gerät via /api/update/status.
  // Fällt auf den compile-time Prop zurück bis der erste Fetch abgeschlossen ist.
  let liveInstalledVersion = '';

  let statusCard = null;
  let appFileInput = null;
  let webUiFileInput = null;

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
    appMaxSize: 0,
    firmwareMaxSize: 0,
    webuiMaxSize: 0
  };

  let appFile = null;
  let webUiFile = null;
  let localUpload = {
    active: false,
    target: '',
    received: 0,
    total: 0
  };
  let otaReloadScheduled = false;

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

  function progressValue(received, total) {
    if (!total || total <= 0) return 0;
    return Math.min(100, Math.round((received / total) * 100));
  }

  function isRepoProgress(target) {
    return status.inProgress && status.source === 'repo' && status.target === target;
  }

  function isUploadProgress(target) {
    return localUpload.active && localUpload.target === target;
  }

  function formatProgressText(received, total) {
    if (!total) {
      return `${humanSize(received)} übertragen`;
    }
    return `${humanSize(received)} / ${humanSize(total)}`;
  }

  function isBusy() {
    return status.inProgress || localUpload.active;
  }

  function uploadFileWithProgress(target, formData) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', `/api/update/upload/${target}`);
      xhr.responseType = 'text';

      xhr.upload.onprogress = (event) => {
        if (!event.lengthComputable) {
          return;
        }

        localUpload = {
          active: true,
          target,
          received: event.loaded,
          total: event.total
        };
      };

      xhr.onerror = () => {
        localUpload = { active: false, target: '', received: 0, total: 0 };
        reject(new Error('Netzwerkfehler beim Upload'));
      };

      xhr.onload = () => {
        const payload = (() => {
          try {
            return JSON.parse(xhr.responseText || '{}');
          } catch (_) {
            return {};
          }
        })();

        localUpload = {
          active: false,
          target: '',
          received: payload.status === 'ok' ? localUpload.total : 0,
          total: payload.status === 'ok' ? localUpload.total : 0
        };

        if (xhr.status >= 200 && xhr.status < 300) {
          resolve(payload);
          return;
        }

        reject(new Error(payload.error || 'Upload fehlgeschlagen'));
      };

      xhr.send(formData);
    });
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
        if (status.installedVersion) {
          liveInstalledVersion = status.installedVersion;
        }

        // After a successful OTA the device reboots. Wait until device is back up, then reload.
        if (!otaReloadScheduled && status.success && status.rebootPending && !status.inProgress) {
          otaReloadScheduled = true;
          // Give the ESP32 time to start rebooting, then poll until it responds.
          setTimeout(async () => {
            let attempts = 0;
            while (attempts < 24) {
              try {
                const r = await fetch('/api/update/status', { cache: 'no-store' });
                if (r.ok) {
                  window.location.reload();
                  return;
                }
              } catch (_) { /* Gerät startet noch neu */ }
              await new Promise(res => setTimeout(res, 500));
              attempts++;
            }
            window.location.reload(); // Fallback nach ~12 s
          }, 3000);
        }
      }
    } catch (_) {
      // Status-Polling ist optional.
    }
  }

  async function startRepoUpdate() {
    if (isBusy() || !manifest) {
      return;
    }

    // Automatisch erkennen, welche Updates verfügbar sind
    const hasAppUpdate = manifest.assets?.app || manifest.assets?.firmware;
    const hasWebUiUpdate = manifest.assets?.webui;

    let target = 'app'; // default
    let updateLabel = 'der App';

    if (hasAppUpdate && hasWebUiUpdate) {
      target = 'full';
      updateLabel = 'App und Web-UI';
    } else if (hasWebUiUpdate && !hasAppUpdate) {
      target = 'webui';
      updateLabel = 'der Web-UI';
    }

    const confirmed = await confirmAction({
      title: 'Repo-Update starten?',
      message: `Soll die neueste Version von ${updateLabel} aus dem GitHub-Release geladen und installiert werden?`,
      confirmLabel: 'Update starten',
      cancelLabel: 'Abbrechen'
    });

    if (!confirmed) {
      return;
    }

    // Zur Status-Card scrollen
    if (statusCard) {
      statusCard.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }

    const response = await fetch('/api/update/repo', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ target })
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      showNotice('error', payload.error || 'Repo-Update konnte nicht gestartet werden');
      return;
    }

    showNotice('success', payload.message || 'Repo-Update wurde gestartet.');
    await loadStatus();
  }

  function getRepoUpdateStatus(manifestData, installedVersion) {
    if (!manifestData) {
      return { hasUpdate: false, label: 'Kein Release-Manifest' };
    }

    const hasAppUpdate = manifestData.assets?.app || manifestData.assets?.firmware;
    const hasWebUiUpdate = manifestData.assets?.webui;
    const appNewer = compareVersions(manifestData.version, installedVersion) > 0;

    if (!appNewer) {
      return { hasUpdate: false, label: 'Bereits auf neuester Version' };
    }

    const updates = [];
    if (hasAppUpdate) updates.push('App');
    if (hasWebUiUpdate) updates.push('Web-UI');

    return {
      hasUpdate: updates.length > 0,
      label: `Update verfügbar: ${updates.join(' + ')}`
    };
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

    const maxAppSize = status.appMaxSize || status.firmwareMaxSize;

    if (target === 'app') {
      if (lowerName.includes('littlefs') || lowerName.includes('web-ui')) {
        return 'Diese Datei sieht nach einer Web-UI-Datei aus. Bitte die App-BIN wählen.';
      }
      if (maxAppSize && file.size > maxAppSize) {
        return 'Die Datei ist größer als die verfügbare App-Partition.';
      }
      if (file.size < 65536) {
        return 'Die Datei ist für eine App unplausibel klein.';
      }
    } else {
      if (lowerName.includes('firmware') || lowerName.includes('app')) {
        return 'Diese Datei sieht nach einer App aus. Bitte die Web-UI-BIN wählen.';
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

  async function uploadLocal(target, fileToUpload = null) {
    if (isBusy()) {
      return;
    }

    // Wenn fileToUpload nicht übergeben wird, aus appFile/webUiFile lesen
    if (!fileToUpload) {
      fileToUpload = target === 'app' ? appFile : webUiFile;
    }

    const error = validateLocalFile(target, fileToUpload);
    if (error) {
      showNotice('error', error);
      return;
    }

    const label = target === 'app' ? 'App' : 'Web-UI';
    const confirmed = await confirmAction({
      title: 'Lokales Update installieren?',
      message: `Soll die lokale ${label}-Datei jetzt installiert werden?`,
      confirmLabel: `${label} installieren`,
      cancelLabel: 'Abbrechen'
    });

    if (!confirmed) {
      return;
    }

    const formData = new FormData();
    formData.append('file', fileToUpload);

    localUpload = { active: true, target, received: 0, total: fileToUpload.size };

    // Zur Status-Card scrollen
    if (statusCard) {
      statusCard.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }

    let payload;
    try {
      payload = await uploadFileWithProgress(target, formData);
    } catch (errorReason) {
      showNotice('error', errorReason.message || `${label}-Upload fehlgeschlagen`);
      return;
    }

    showNotice('success', payload.message || `${label} wurde erfolgreich hochgeladen. Gerät startet neu.`);
    await loadStatus();
  }

  function triggerFileSelect(target) {
    if (target === 'app') {
      appFileInput?.click();
    } else {
      webUiFileInput?.click();
    }
  }

  function handleFileSelected(target) {
    return (event) => {
      const file = event.currentTarget.files?.[0] || null;
      if (!file) {
        return;
      }

      // Speichern für spätere Verwendung
      if (target === 'app') {
        appFile = file;
      } else {
        webUiFile = file;
      }

      // Zur Status-Card scrollen
      if (statusCard) {
        statusCard.scrollIntoView({ behavior: 'smooth', block: 'start' });
      }

      // Direkt mit der ausgewählten Datei uploadLocal aufrufen
      uploadLocal(target, file);
    };
  }

  $: progressPercent = progressValue(status.received, status.total);
  $: repoProgress = status.inProgress && status.source === 'repo' && (status.target === 'app' || status.target === 'full' || status.target === 'webui')
    ? progressValue(status.received, status.total)
    : 0;
  $: uploadAppProgress = localUpload.active && localUpload.target === 'app' ? progressValue(localUpload.received, localUpload.total) : 0;
  $: uploadWebUiProgress = localUpload.active && localUpload.target === 'webui' ? progressValue(localUpload.received, localUpload.total) : 0;
  $: displayedVersion = liveInstalledVersion || currentVersion;
  $: repoStatus = getRepoUpdateStatus(manifest, displayedVersion);
  $: currentReleaseUrl = `https://github.com/Back-code/Salzstand/releases/tag/v${displayedVersion}`;
  $: anyBusy = status.inProgress || localUpload.active;

  onMount(() => {
    loadManifest();
    loadStatus();
    const interval = setInterval(loadStatus, 1000);
    return () => clearInterval(interval);
  });
</script>

<section class="update-grid">
  <article class="status-card hero" bind:this={statusCard}>
    {#if manifestError}
      <div class="manifest-badge warning">{manifestError}</div>
    {:else if manifest}
      <div class="manifest-badge success">Manifest geladen</div>
    {/if}

    <div>
      <span class="eyebrow">Stand</span>
      <h2>Update-Zentrale</h2>
      <div class="version-row">
        <div class="version-item installed">
          <span class="version-label">Installierte Version</span>
          <a href={currentReleaseUrl} target="_blank" rel="noopener noreferrer" class="version-link">v{displayedVersion}</a>
        </div>
        {#if manifest}
          <span class="version-separator" aria-hidden="true"></span>
          <div class="version-item available">
            <span class="version-label">Verfügbare Version</span>
            <a href={manifest.releaseUrl} target="_blank" rel="noopener noreferrer" class="version-link">v{manifest.version}</a>
          </div>
        {/if}
      </div>
    </div>

    {#if manifest}
      <div class="manifest-box">
        <div>
          <span>App</span>
          <strong>{manifest.assets?.app?.name || manifest.assets?.firmware?.name || 'nicht vorhanden'}</strong>
          <small>{humanSize(manifest.assets?.app?.size || manifest.assets?.firmware?.size)}</small>
        </div>
        <div>
          <span>Web-UI</span>
          <strong>{manifest.assets?.webui?.name || 'nicht vorhanden'}</strong>
          <small>{humanSize(manifest.assets?.webui?.size)}</small>
        </div>
      </div>
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

  <article class="action-card repo-card">
    <span class="eyebrow">1. OTA</span>
    {#if repoStatus.hasUpdate}
      <div class="status-badge success">✓ {repoStatus.label}</div>
    {:else}
      <div class="status-badge info">⊘ {repoStatus.label}</div>
    {/if}
    <h3>Repo-Update (automatisch erkannt)</h3>
    <p>Erkennt automatisch, welche Updates verfügbar sind und installiert diese aus dem neuesten GitHub-Release.</p>
    <button on:click={() => startRepoUpdate()} disabled={!repoStatus.hasUpdate || anyBusy}>
      Repo-Update starten
    </button>
    {#if isRepoProgress('app') || isRepoProgress('full') || isRepoProgress('webui')}
      <div class="inline-progress">
        <div class="inline-progress-head">
          <span>OTA läuft</span>
          <strong>{repoProgress}%</strong>
        </div>
        <div class="mini-track"><div class="mini-fill" style={`width:${repoProgress}%`}></div></div>
        <small>{status.phase}: {formatProgressText(status.received, status.total)}</small>
      </div>
    {/if}
    {#if manifest?.releaseUrl}
      <a class="release-link" href={manifest.releaseUrl} target="_blank" rel="noopener noreferrer">Release im Browser öffnen</a>
    {/if}
  </article>

  <article class="action-card upload-card">
    <span class="eyebrow">2. Lokal</span>
    <h3>Updates hochladen</h3>
    <p>Wählen Sie eine gültige Binärdatei (.bin).<br />Bootloader- und Partitionsdateien sind nicht erlaubt.</p>
    
    <!-- Hidden file inputs -->
    <input 
      type="file" 
      accept=".bin" 
      bind:this={appFileInput}
      style="display:none"
      on:change={handleFileSelected('app')}
    />
    <input 
      type="file" 
      accept=".bin" 
      bind:this={webUiFileInput}
      style="display:none"
      on:change={handleFileSelected('webui')}
    />
    
    <!-- Action buttons -->
    <div class="button-group">
      <button on:click={() => triggerFileSelect('app')} disabled={anyBusy}>
        App.bin wählen
      </button>
      <button on:click={() => triggerFileSelect('webui')} disabled={anyBusy}>
        Web-UI.bin wählen
      </button>
    </div>
    
    <!-- Upload progress indicators -->
    {#if isUploadProgress('app')}
      <div class="inline-progress">
        <div class="inline-progress-head">
          <span>App-Upload läuft</span>
          <strong>{uploadAppProgress}%</strong>
        </div>
        <div class="mini-track"><div class="mini-fill" style={`width:${uploadAppProgress}%`}></div></div>
        <small>{formatProgressText(localUpload.received, localUpload.total)}</small>
      </div>
    {/if}
    
    {#if isUploadProgress('webui')}
      <div class="inline-progress">
        <div class="inline-progress-head">
          <span>Web-UI-Upload läuft</span>
          <strong>{uploadWebUiProgress}%</strong>
        </div>
        <div class="mini-track"><div class="mini-fill" style={`width:${uploadWebUiProgress}%`}></div></div>
        <small>{formatProgressText(localUpload.received, localUpload.total)}</small>
      </div>
    {/if}
    
    <div class="sizes-info">
      <span>Max App: {humanSize(status.appMaxSize || status.firmwareMaxSize)}</span>
      <span>Max Web-UI: {humanSize(status.webuiMaxSize)}</span>
    </div>
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
    position: relative;
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

  .manifest-badge {
    position: absolute;
    top: 18px;
    right: 18px;
    max-width: min(44ch, calc(100% - 36px));
    padding: 8px 12px;
    border-radius: 999px;
    font-size: 0.78rem;
    line-height: 1.25;
    backdrop-filter: blur(10px);
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.08);
  }

  .manifest-badge.success {
    color: #9ad9a8;
  }

  .manifest-badge.warning {
    color: #ffb58f;
    border-color: rgba(255, 181, 143, 0.25);
    background: rgba(99, 49, 26, 0.28);
  }

  .version-row {
    display: flex;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
    margin-top: 8px;
  }

  .version-item {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .version-label {
    font-size: 0.75rem;
    color: var(--text-muted);
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }

  .version-link {
    font-size: 0.95rem;
    font-weight: 600;
    color: #62b8dd;
    text-decoration: none;
    cursor: pointer;
    transition: color 0.2s ease;
  }

  .version-link:hover {
    color: #7ec9eb;
    text-decoration: underline;
  }

  .version-separator {
    width: 32px;
    height: 1px;
    border-radius: 999px;
    background: linear-gradient(90deg, transparent 0%, rgba(98, 184, 221, 0.6) 50%, transparent 100%);
  }

  .status-badge {
    position: absolute;
    top: 18px;
    right: 18px;
    padding: 6px 12px;
    border-radius: 6px;
    font-size: 0.8rem;
    font-weight: 600;
    background: rgba(255, 255, 255, 0.08);
  }

  .status-badge.success {
    color: #57b67b;
    border: 1px solid rgba(87, 182, 123, 0.3);
  }

  .status-badge.info {
    color: var(--text-muted);
    border: 1px solid var(--surface-border);
  }

  .repo-card {
    position: relative;
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

  .inline-progress {
    display: grid;
    gap: 6px;
    padding: 10px 12px;
    border-radius: 12px;
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.05);
  }

  .inline-progress-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
  }

  .inline-progress-head span {
    color: var(--text-muted);
    font-size: 0.82rem;
  }

  .mini-track {
    height: 8px;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.08);
    overflow: hidden;
  }

  .mini-fill {
    height: 100%;
    border-radius: 999px;
    background: linear-gradient(90deg, #57b67b 0%, #62b8dd 100%);
    transition: width 0.18s ease;
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

  .button-group {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 12px;
  }

  .button-group button {
    width: 100%;
  }

  .sizes-info {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 12px;
    padding: 10px 12px;
    border-radius: 10px;
    background: rgba(255, 255, 255, 0.04);
    font-size: 0.8rem;
  }

  .sizes-info span {
    color: var(--text-muted);
  }

  input[type='file'] {
    width: 100%;
    color: var(--text-main);
  }

  @media (max-width: 840px) {
    .update-grid,
    .manifest-box {
      grid-template-columns: 1fr;
    }

    .hero {
      padding-top: 64px;
    }

    .manifest-badge {
      left: 18px;
      right: 18px;
      max-width: none;
    }

    .button-group {
      grid-template-columns: 1fr;
    }

    .sizes-info {
      grid-template-columns: 1fr;
    }
  }
</style>