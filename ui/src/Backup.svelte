<script>
  import { confirmAction, showNotice } from './dialogStore.js';
  import { adminFetch } from './adminAuth.js';
  import { createTranslator } from './i18n.js';

  export let loadConfig;
  export let lang = 'de';

  let importFile = null;
  let importLoading = false;
  let importResult = null;
  let factoryResetLoading = false;
  let includeBackupSecrets = false;
  $: t = createTranslator(lang);

  async function doExport() {
    try {
      if (includeBackupSecrets) {
        const confirmed = await confirmAction({
          title: 'Geheimnisse exportieren?',
          message: 'Das Backup enthält WLAN-, MQTT- und SMTP-Zugangsdaten im Klartext.',
          confirmLabel: 'Trotzdem exportieren',
          cancelLabel: 'Abbrechen',
          tone: 'danger'
        });
        if (!confirmed) return;
      }

      const query = includeBackupSecrets ? '?includeSecrets=1' : '';
      const response = await adminFetch(`/api/export${query}`, { cache: 'no-store' });
      if (!response.ok) throw new Error('Backup konnte nicht erstellt werden');

      const blob = await response.blob();
      const url = URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = url;
      link.download = 'level-control-backup.json';
      link.click();
      URL.revokeObjectURL(url);
    } catch (error) {
      showNotice('error', error?.message || 'Backup konnte nicht erstellt werden');
    }
  }

  async function doImport() {
    if (!importFile) return;
    importLoading = true;
    importResult = null;
    try {
      const body = await importFile.arrayBuffer();
      const response = await adminFetch('/api/import', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok || payload.status !== 'ok') {
        throw new Error(payload.error || 'Import fehlgeschlagen');
      }

      const parts = [];
      if (payload.configRestored) parts.push('Konfiguration');
      if (payload.historyRestored) parts.push('Messverlauf');
      importResult = { ok: true, msg: `${parts.length ? parts.join(' und ') : 'Nichts'} wiederhergestellt.` };
      if (payload.configRestored) await loadConfig();
    } catch (error) {
      importResult = { ok: false, msg: error?.message || 'Import fehlgeschlagen' };
    } finally {
      importLoading = false;
    }
  }

  async function doFactoryReset() {
    if (factoryResetLoading) return;
    const confirmed = await confirmAction({
      title: 'Werksreset bestätigen',
      message: 'Alle Einstellungen und Messdaten werden gelöscht. Das Gerät startet danach automatisch neu. Fortfahren?',
      confirmLabel: 'Ja, alles löschen',
      cancelLabel: 'Abbrechen',
      tone: 'danger'
    });
    if (!confirmed) return;

    factoryResetLoading = true;
    try {
      const response = await adminFetch('/api/factory-reset', { method: 'POST' });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok || payload.status !== 'ok') {
        throw new Error(payload.error || 'Werksreset fehlgeschlagen');
      }
      showNotice('success', 'Werksreset gestartet. Das Gerät startet neu.');
    } catch (error) {
      showNotice('error', error?.message || 'Werksreset fehlgeschlagen');
    } finally {
      factoryResetLoading = false;
    }
  }
</script>

<div class="backup-module">
  <h2>Datensicherung</h2>
  <h3>Exportieren</h3>
  <p>Lädt Einstellungen und Messverlauf herunter. Zugangsdaten werden standardmäßig maskiert.</p>
  <label class="checkbox-row checkbox-row--warning"><input type="checkbox" bind:checked={includeBackupSecrets} /> Zugangsdaten bewusst einschließen</label>
  <button class="primary" on:click={doExport}>Backup herunterladen</button>

  <h3>Importieren</h3>
  <p>Stellt Einstellungen und Messverlauf aus einer exportierten Backup-Datei wieder her.</p>
  <input type="file" accept=".json" on:change={(event) => { importFile = event.currentTarget.files?.[0] || null; importResult = null; }} />
  <button class="primary" disabled={!importFile || importLoading} on:click={doImport}>
    {importLoading ? 'Wird importiert …' : 'Importieren'}
  </button>
  {#if importResult}<p class:error={!importResult.ok} class:success={importResult.ok}>{importResult.msg}</p>{/if}

  <h3>Werkszustand</h3>
  <p>Löscht alle Einstellungen und den kompletten Messverlauf. Anschließend startet das Gerät automatisch neu.</p>
  <button class="danger" disabled={factoryResetLoading} on:click={doFactoryReset}>
    {factoryResetLoading ? 'Werksreset läuft …' : 'Gerät auf Werkszustand zurücksetzen'}
  </button>
</div>

<style>
  .backup-module { display: grid; gap: 12px; padding: 20px; border: 1px solid var(--surface-border); border-radius: 12px; background: var(--card-grad); color: var(--text-main); }
  h2, h3, p { margin: 0; }
  h3 { margin-top: 12px; color: var(--text-main); }
  p { color: var(--text-muted); line-height: 1.5; }
  input[type='file'] { color: var(--text-muted); }
  .checkbox-row { display: flex; gap: 8px; align-items: center; color: var(--text-muted); font-weight: 700; }
  button { min-height: 40px; width: fit-content; padding: 0 14px; border: 1px solid var(--surface-border); border-radius: 8px; background: var(--button-active-bg); color: var(--button-active-text); font-weight: 800; cursor: pointer; }
  button:disabled { cursor: default; opacity: 0.55; }
  button.danger { background: rgba(180, 45, 45, 0.85); }
  .success { color: #86efac; }
  .error { color: #fca5a5; }
</style>
