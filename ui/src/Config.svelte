<script>
  import { onMount } from 'svelte';
  import { showNotice } from './dialogStore.js';
  import FieldLabel from './FieldLabel.svelte';
  import PasswordInput from './PasswordInput.svelte';

  export let data;
  export let loadConfig;
  export let module = 'sensor';

  let wifiConfig = {
    ssid: '',
    password: '',
    staticIp: { ip: '', gateway: '', subnet: '', dns: '' }
  };
  let wifiHasPassword = false;
  let wifiMode = 'dhcp';
  let wifiNetworks = [];
  let wifiScanLoading = false;
  let wifiScanError = '';
  let wifiValidation = { attempted: false, touched: {} };
  let sensorIntervalValue = '5';
  let sensorIntervalUnit = 'seconds';
  let sensorIntervalValidation = { attempted: false, touched: false };
  let mqttConfig = { server: '', port: 1883, user: '', password: '', discovery: true };
  let mqttHasPassword = false;
  let mqttDeviceId = '';

  const PASSWORD_MASK = '*****';
  const SAMPLE_UNITS = [
    { value: 'seconds', label: 'Sekunden', seconds: 1 },
    { value: 'minutes', label: 'Minuten', seconds: 60 },
    { value: 'hours', label: 'Stunden', seconds: 3600 },
    { value: 'days', label: 'Tage', seconds: 86400 }
  ];
  const MIN_SAMPLE_INTERVAL_SECONDS = 5;

  function isMaskedPassword(value) {
    return value === '***' || value === PASSWORD_MASK;
  }

  function getUnitSeconds(unit) {
    return SAMPLE_UNITS.find((entry) => entry.value === unit)?.seconds || 1;
  }

  function syncSensorIntervalFromSeconds(totalSeconds) {
    const normalizedSeconds = Math.max(MIN_SAMPLE_INTERVAL_SECONDS, Number(totalSeconds) || MIN_SAMPLE_INTERVAL_SECONDS);

    if (normalizedSeconds % 86400 === 0) {
      sensorIntervalUnit = 'days';
      sensorIntervalValue = String(normalizedSeconds / 86400);
      return;
    }
    if (normalizedSeconds % 3600 === 0) {
      sensorIntervalUnit = 'hours';
      sensorIntervalValue = String(normalizedSeconds / 3600);
      return;
    }
    if (normalizedSeconds % 60 === 0) {
      sensorIntervalUnit = 'minutes';
      sensorIntervalValue = String(normalizedSeconds / 60);
      return;
    }

    sensorIntervalUnit = 'seconds';
    sensorIntervalValue = String(normalizedSeconds);
  }

  function getSampleIntervalSeconds() {
    const numericValue = Number(sensorIntervalValue);
    if (!Number.isFinite(numericValue) || numericValue <= 0) {
      return 0;
    }
    return Math.round(numericValue * getUnitSeconds(sensorIntervalUnit));
  }

  function hasSensorIntervalError() {
    const shouldValidate = sensorIntervalValidation.attempted || sensorIntervalValidation.touched;
    return shouldValidate && getSampleIntervalSeconds() < MIN_SAMPLE_INTERVAL_SECONDS;
  }

  function getSensorIntervalError() {
    if (!hasSensorIntervalError()) {
      return '';
    }
    return 'Mindestens 5 Sekunden erforderlich.';
  }

  function touchSensorInterval() {
    sensorIntervalValidation = { ...sensorIntervalValidation, touched: true };
  }

  function enableStaticIp() {
    wifiMode = 'static';
    if (!wifiConfig.staticIp.subnet) {
      wifiConfig.staticIp.subnet = '255.255.255.0';
    }
    wifiConfig = { ...wifiConfig, staticIp: { ...wifiConfig.staticIp } };
  }

  function enableDhcp() {
    wifiMode = 'dhcp';
    wifiValidation = { attempted: false, touched: {} };
  }

  function isWifiFieldRequired(field) {
    return wifiMode === 'static' && ['ip', 'subnet', 'dns'].includes(field);
  }

  function isWifiFieldEmpty(field) {
    return !String(wifiConfig.staticIp[field] || '').trim();
  }

  function hasWifiFieldError(field) {
    if (!isWifiFieldRequired(field)) {
      return false;
    }
    return (wifiValidation.attempted || wifiValidation.touched[field]) && isWifiFieldEmpty(field);
  }

  function getWifiFieldError(field) {
    if (!hasWifiFieldError(field)) {
      return '';
    }

    if (field === 'ip') {
      return 'Bitte statische IP eingeben.';
    }
    if (field === 'subnet') {
      return 'Bitte Subnetz eingeben.';
    }
    if (field === 'dns') {
      return 'Bitte DNS eingeben.';
    }
    return 'Pflichtfeld fehlt.';
  }

  function touchWifiField(field) {
    wifiValidation = {
      ...wifiValidation,
      touched: {
        ...wifiValidation.touched,
        [field]: true
      }
    };
  }

  async function scanWifiNetworks() {
    wifiScanLoading = true;
    wifiScanError = '';
    try {
      const response = await fetch('/api/wifi/scan', { method: 'POST', cache: 'no-store' });
      if (!response.ok) {
        wifiScanError = 'WiFi-Scan fehlgeschlagen.';
        return;
      }

      const payload = await response.json();
      wifiNetworks = payload.networks || [];
      if (wifiNetworks.length === 0) {
        wifiScanError = 'Keine WiFi-Netze gefunden.';
      }
    } catch (_) {
      wifiScanError = 'WiFi-Scan fehlgeschlagen.';
    } finally {
      wifiScanLoading = false;
    }
  }

  function selectScannedWifi(event) {
    const nextSsid = event.currentTarget.value;
    if (!nextSsid) {
      return;
    }
    wifiConfig = { ...wifiConfig, ssid: nextSsid };
  }

  function loadAllConfig() {
    loadConfig();

    fetch('/api/wifi', { cache: 'no-store' })
      .then(r => r.json())
      .then(c => {
        wifiConfig = {
          ssid: c.ssid || '',
          password: isMaskedPassword(c.password || '') ? '' : (c.password || ''),
          staticIp: {
            ip: c.staticIp?.ip || '',
            gateway: c.staticIp?.gateway || '',
            subnet: c.staticIp?.subnet || '',
            dns: c.staticIp?.dns || ''
          }
        };
        wifiHasPassword = (c.hasPassword ?? false) || isMaskedPassword(c.password || '');
        wifiMode = c.useStaticIp ? 'static' : 'dhcp';
        wifiValidation = { attempted: false, touched: {} };
      });

    fetch('/api/mqtt', { cache: 'no-store' })
      .then(r => r.json())
      .then(c => {
        mqttConfig = {
          server: c.server || '',
          port: c.port || 1883,
          user: c.user || '',
          password: isMaskedPassword(c.password || '') ? '' : (c.password || ''),
          discovery: c.discovery ?? true
        };
        mqttHasPassword = (c.hasPassword ?? false) || isMaskedPassword(c.password || '');
        mqttDeviceId = c.device_id || '';
      });
  }

  $: if (module === 'sensor' && data.sampleIntervalSeconds) {
    syncSensorIntervalFromSeconds(data.sampleIntervalSeconds);
  }

  async function saveSensorConfig() {
    sensorIntervalValidation = { ...sensorIntervalValidation, attempted: true };
    const sampleIntervalSeconds = getSampleIntervalSeconds();
    if (sampleIntervalSeconds < MIN_SAMPLE_INTERVAL_SECONDS) {
      showNotice('error', 'Die Ultraschall-Abtastrate muss mindestens 5 Sekunden betragen.');
      return;
    }

    try {
      const response = await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          behaelterhoehe: data.behaelterhoehe,
          offset: data.offset,
          sampleIntervalSeconds
        })
      });

      if (!response.ok) {
        showNotice('error', 'Behälter-Konfiguration konnte nicht gespeichert werden.');
        return;
      }

      showNotice('success', 'Behälter-Konfiguration gespeichert.');
      data.sampleIntervalSeconds = sampleIntervalSeconds;
      sensorIntervalValidation = { attempted: false, touched: false };
    } catch (_) {
      showNotice('error', 'Behälter-Konfiguration konnte nicht gespeichert werden.');
    }
  }

  async function saveWifiConfig() {
    if (wifiMode === 'static') {
      wifiValidation = { ...wifiValidation, attempted: true };
      if (!wifiConfig.staticIp.ip || !wifiConfig.staticIp.subnet || !wifiConfig.staticIp.dns) {
        showNotice('error', 'Für statische IP sind Statische IP, Subnetz und DNS erforderlich.');
        return;
      }
    }

    try {
      const response = await fetch('/api/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          ssid: wifiConfig.ssid,
          password: wifiConfig.password,
          useStaticIp: wifiMode === 'static',
          staticIp: {
            ip: wifiMode === 'static' ? wifiConfig.staticIp.ip : '',
            gateway: wifiMode === 'static' ? wifiConfig.staticIp.gateway : '',
            subnet: wifiMode === 'static' ? wifiConfig.staticIp.subnet : '',
            dns: wifiMode === 'static' ? wifiConfig.staticIp.dns : ''
          }
        })
      });

      if (!response.ok) {
        showNotice('error', 'WiFi-Konfiguration konnte nicht gespeichert werden.');
        return;
      }

      showNotice('success', 'WiFi-Konfiguration gespeichert. Neustart erforderlich.');
      wifiValidation = { attempted: false, touched: {} };
    } catch (_) {
      showNotice('error', 'WiFi-Konfiguration konnte nicht gespeichert werden.');
    }
  }

  async function saveMqttConfig() {
    try {
      const response = await fetch('/api/mqtt', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(mqttConfig)
      });

      if (!response.ok) {
        showNotice('error', 'MQTT-Konfiguration konnte nicht gespeichert werden.');
        return;
      }

      showNotice('success', 'MQTT-Konfiguration gespeichert. Neustart erforderlich.');
    } catch (_) {
      showNotice('error', 'MQTT-Konfiguration konnte nicht gespeichert werden.');
    }
  }

  function getMqttStateLabel(state) {
    switch (state) {
      case 'connected':     return 'Verbunden';
      case 'connecting':    return 'Verbindet…';
      case 'backoff':       return 'Wartet auf Neuversuch…';
      case 'disconnected':  return 'Getrennt';
      case 'uninitialized': return 'Nicht konfiguriert';
      default:              return 'Unbekannt';
    }
  }

  async function reconnectMqtt() {
    try {
      await fetch('/api/mqtt/reconnect', { method: 'POST' });
    } catch (_) { }
  }

  onMount(() => {
    loadAllConfig();
  });
</script>

{#if module === 'sensor'}
  <div class="config-section">
    <h2>Konfiguration</h2>
    <p>Messgeometrie, Korrekturwerte und Ultraschall-Abtastrate für die Füllstandsberechnung.</p>

    <label class="field-row">
      <span>Offset (cm):</span>
      <input type="number" bind:value={data.offset} />
    </label>

    <label class="field-row">
      <span>Behälterhöhe (cm):</span>
      <input type="number" bind:value={data.behaelterhoehe} />
    </label>

    <label class="field-row field-row--top-align">
      <span><FieldLabel text="Abtastrate Ultraschall:" required={true} /></span>
      <div class="field-control field-control--inline">
        <input
          type="number"
          min="1"
          step="1"
          bind:value={sensorIntervalValue}
          aria-required="true"
          aria-invalid={hasSensorIntervalError()}
          class:input-invalid={hasSensorIntervalError()}
          on:blur={touchSensorInterval}
        />
        <select bind:value={sensorIntervalUnit} on:blur={touchSensorInterval}>
          {#each SAMPLE_UNITS as unit}
            <option value={unit.value}>{unit.label}</option>
          {/each}
        </select>
        {#if hasSensorIntervalError()}
          <p class="field-error field-error--inline">{getSensorIntervalError()}</p>
        {:else}
          <p class="helper-text field-help">Kleinster zulässiger Wert: 5 Sekunden.</p>
        {/if}
      </div>
    </label>

    <div class="sensor-sketch" aria-label="Erklärung Behälterhöhe und Offset">
      <svg viewBox="0 0 400 148" role="img">
        <!-- Behälter Container mit Sensor-Module -->
        <rect x="120" y="30" width="160" height="110" rx="8" fill="rgba(255,255,255,0.08)" stroke="currentColor" stroke-width="2" />
        
        <!-- Kleine eckige Kiste mit 2 Sensoren oben im Behälter (zentriert) -->
        <rect x="165" y="35" width="70" height="18" rx="3" fill="rgba(100,150,255,0.2)" stroke="currentColor" stroke-width="1.5" />
        <circle cx="180" cy="44" r="3" fill="var(--accent)" />
        <circle cx="220" cy="44" r="3" fill="var(--accent)" />
        
        <!-- Ultraschallwellen vom linken und rechten Sensor -->
        <ellipse cx="180" cy="54" rx="8" ry="5" fill="none" stroke="rgba(100,150,255,0.4)" stroke-width="1" />
        <ellipse cx="180" cy="61" rx="13" ry="8" fill="none" stroke="rgba(100,150,255,0.3)" stroke-width="1" />
        <ellipse cx="180" cy="68" rx="18" ry="11" fill="none" stroke="rgba(100,150,255,0.2)" stroke-width="1" />
        
        <ellipse cx="220" cy="54" rx="8" ry="5" fill="none" stroke="rgba(100,150,255,0.4)" stroke-width="1" />
        <ellipse cx="220" cy="61" rx="13" ry="8" fill="none" stroke="rgba(100,150,255,0.3)" stroke-width="1" />
        <ellipse cx="220" cy="68" rx="18" ry="11" fill="none" stroke="rgba(100,150,255,0.2)" stroke-width="1" />
        
        <!-- Behälterboden Linie (zentriert) -->
        <line x1="130" y1="118" x2="270" y2="118" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3" />

        <!-- Offset Pfeil (Links, näher an Behälterhöhe) -->
        <line x1="112" y1="65" x2="112" y2="82" stroke="var(--accent)" stroke-width="2" />
        <polygon points="108,67 116,67 112,59" fill="var(--accent)" />
        <polygon points="108,80 116,80 112,88" fill="var(--accent)" />
        <text x="105" y="77" text-anchor="end" fill="var(--accent)" font-size="13" font-weight="600">Offset</text>

        <!-- Behälterhöhe Pfeil (Links unten) -->
        <line x1="112" y1="92" x2="112" y2="125" stroke="var(--accent)" stroke-width="2" />
        <polygon points="108,94 116,94 112,86" fill="var(--accent)" />
        <polygon points="108,123 116,123 112,131" fill="var(--accent)" />
        <text x="105" y="113" text-anchor="end" fill="var(--accent)" font-size="13" font-weight="600">Behälterhöhe</text>

        <text x="200" y="22" text-anchor="middle" fill="var(--text-muted)" font-size="11">Sensor</text>
        <text x="200" y="137" text-anchor="middle" fill="var(--text-muted)" font-size="11">Behälterboden</text>
      </svg>
      <p class="sketch-note">Offset verschiebt die Referenz der Sensorposition. Positive Werte vergrößern, negative Werte verkleinern den berechneten Füllstand.</p>
    </div>

    <button class="primary" on:click={saveSensorConfig}>Speichern</button>
  </div>
{:else if module === 'wifi'}
  <div class="config-section">
    <h2>WiFi</h2>
    <p>Netzwerkzugang und optionale statische IP-Konfiguration.</p>
    <label class="field-row">
      <span>SSID:</span>
      <div class="input-action-row">
        <input bind:value={wifiConfig.ssid} />
        <button class="icon-button" type="button" on:click={scanWifiNetworks} aria-label="WiFi-Netze suchen" title="WiFi-Netze suchen">
          <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M10 4a6 6 0 1 0 3.87 10.58l4.27 4.28 1.42-1.42-4.28-4.27A6 6 0 0 0 10 4zm0 2a4 4 0 1 1 0 8 4 4 0 0 1 0-8z"/></svg>
        </button>
      </div>
    </label>
    {#if wifiScanLoading}
      <p class="helper-text">WiFi-Scan läuft...</p>
    {:else if wifiScanError}
      <p class="helper-text error">{wifiScanError}</p>
    {/if}
    {#if wifiNetworks.length > 0}
      <label class="field-row compact-row">
        <span>Gefundene Netze:</span>
        <select on:change={selectScannedWifi}>
          <option value="">Bitte WiFi auswählen</option>
          {#each wifiNetworks as network}
            <option value={network.ssid}>{network.ssid} ({network.rssi} dBm)</option>
          {/each}
        </select>
      </label>
    {/if}

    <div class="field-row">
      <span>Passwort:</span>
      <PasswordInput bind:value={wifiConfig.password} hasStoredPassword={wifiHasPassword} mask={PASSWORD_MASK} />
    </div>

    <div class="choice-grid" role="radiogroup" aria-label="IP-Konfiguration">
      <label class="choice-card">
        <input type="radio" name="wifi-ip-mode" checked={wifiMode === 'dhcp'} on:change={enableDhcp} />
        <span>DHCP</span>
      </label>
      <label class="choice-card">
        <input type="radio" name="wifi-ip-mode" checked={wifiMode === 'static'} on:change={enableStaticIp} />
        <span>Statische IP einstellen</span>
      </label>
    </div>

    {#if wifiMode === 'static'}
      <label class="field-row field-row--top-align">
        <span><FieldLabel text="Statische IP:" required={true} /></span>
        <div class="field-control">
          <input
            bind:value={wifiConfig.staticIp.ip}
            aria-required="true"
            aria-invalid={hasWifiFieldError('ip')}
            class:input-invalid={hasWifiFieldError('ip')}
            on:blur={() => touchWifiField('ip')}
          />
          {#if hasWifiFieldError('ip')}
            <p class="field-error">{getWifiFieldError('ip')}</p>
          {/if}
        </div>
      </label>
      <label class="field-row">
        <span><FieldLabel text="Gateway:" /></span>
        <input bind:value={wifiConfig.staticIp.gateway} />
      </label>
      <label class="field-row field-row--top-align">
        <span><FieldLabel text="Subnetz:" required={true} /></span>
        <div class="field-control">
          <input
            bind:value={wifiConfig.staticIp.subnet}
            aria-required="true"
            aria-invalid={hasWifiFieldError('subnet')}
            class:input-invalid={hasWifiFieldError('subnet')}
            on:blur={() => touchWifiField('subnet')}
          />
          {#if hasWifiFieldError('subnet')}
            <p class="field-error">{getWifiFieldError('subnet')}</p>
          {/if}
        </div>
      </label>
      <label class="field-row field-row--top-align">
        <span><FieldLabel text="DNS:" required={true} /></span>
        <div class="field-control">
          <input
            bind:value={wifiConfig.staticIp.dns}
            aria-required="true"
            aria-invalid={hasWifiFieldError('dns')}
            class:input-invalid={hasWifiFieldError('dns')}
            on:blur={() => touchWifiField('dns')}
          />
          {#if hasWifiFieldError('dns')}
            <p class="field-error">{getWifiFieldError('dns')}</p>
          {/if}
        </div>
      </label>
    {/if}

    <button class="primary" on:click={saveWifiConfig}>Speichern</button>
  </div>
{:else if module === 'mqtt_ha'}
  <div class="config-section">
    <h2>MQTT & HA</h2>
    <p>Broker-Zugangsdaten und Home Assistant Discovery.</p>
    <div class="mqtt-status-row">
      <span class="mqtt-dot mqtt-dot--{data.mqttState || 'unknown'}" aria-hidden="true"></span>
      <span class="mqtt-state-text">{getMqttStateLabel(data.mqttState)}</span>
      {#if (data.mqttState || 'unknown') !== 'connected'}
        <button class="secondary-sm" type="button" on:click={reconnectMqtt}>Jetzt verbinden</button>
      {/if}
    </div>
    <label class="field-row"><span><FieldLabel text="Server:" /></span><input bind:value={mqttConfig.server} /></label>
    <label class="field-row"><span>Port:</span><input type="number" bind:value={mqttConfig.port} /></label>
    <label class="field-row"><span>Benutzer:</span><input bind:value={mqttConfig.user} /></label>
    <div class="field-row">
      <span>Passwort:</span>
      <PasswordInput bind:value={mqttConfig.password} hasStoredPassword={mqttHasPassword} mask={PASSWORD_MASK} />
    </div>
    <label class="checkbox-row"><input type="checkbox" bind:checked={mqttConfig.discovery} /> Home Assistant Discovery aktivieren</label>
    <button class="primary" on:click={saveMqttConfig}>Speichern</button>

    <h3>Home Assistant</h3>
    {#if mqttDeviceId}
      <p class="device-id-row">Geräte-ID: <code class="device-id-code">{mqttDeviceId}</code></p>
    {/if}
    <p>Konfiguriere MQTT-Discovery oben. Alle Entitäten werden automatisch in HA erkannt.</p>

    <details class="topic-card">
      <summary>
        <span class="topic-card-title">MQTT-Topics</span>
        <span class="topic-card-meta">Discovery, Status und OTA</span>
      </summary>

      <div class="topic-card-content">
        <p class="topic-heading">Discovery-Topics</p>
        <ul class="topic-list">
          <li class="topic-group">Messwerte</li>
          <li><code>homeassistant/sensor/{mqttDeviceId || '{deviceId}'}/fill_level/config</code></li>
          <li><code>homeassistant/sensor/{mqttDeviceId || '{deviceId}'}/distance_cm/config</code></li>
          <li><code>homeassistant/sensor/{mqttDeviceId || '{deviceId}'}/raw_distance/config</code></li>
          <li><code>homeassistant/sensor/{mqttDeviceId || '{deviceId}'}/ping_us/config</code></li>
          <li class="topic-group">Konfiguration</li>
          <li><code>homeassistant/number/{mqttDeviceId || '{deviceId}'}/behaelterhoehe/config</code></li>
          <li><code>homeassistant/number/{mqttDeviceId || '{deviceId}'}/offset/config</code></li>
          <li><code>homeassistant/number/{mqttDeviceId || '{deviceId}'}/sample_interval/config</code></li>
          <li class="topic-group">Systeminfo</li>
          <li><code>homeassistant/sensor/{mqttDeviceId || '{deviceId}'}/rssi/config</code></li>
          <li><code>homeassistant/sensor/{mqttDeviceId || '{deviceId}'}/ip_address/config</code></li>
          <li><code>homeassistant/sensor/{mqttDeviceId || '{deviceId}'}/ssid/config</code></li>
          <li><code>homeassistant/sensor/{mqttDeviceId || '{deviceId}'}/uptime/config</code></li>
          <li><code>homeassistant/sensor/{mqttDeviceId || '{deviceId}'}/free_heap/config</code></li>
          <li><code>homeassistant/sensor/{mqttDeviceId || '{deviceId}'}/cpu_freq/config</code></li>
          <li class="topic-group">OTA Update</li>
          <li><code>homeassistant/update/{mqttDeviceId || '{deviceId}'}/ota/config</code></li>
        </ul>

        <p class="topic-heading">Status-Topics</p>
        <ul class="topic-list">
          <li><code>salzstand/status</code> - online / offline (LWT)</li>
          <li><code>salzstand/sensor/state</code> - Messwerte (JSON)</li>
          <li><code>salzstand/config/behaelterhoehe/state</code> / <code>.../set</code></li>
          <li><code>salzstand/config/offset/state</code> / <code>.../set</code></li>
          <li><code>salzstand/config/sampleinterval/state</code> / <code>.../set</code></li>
          <li><code>salzstand/system/state</code> - CPU, RAM, WiFi (JSON)</li>
          <li><code>salzstand/update/state</code> - OTA-Status (JSON)</li>
          <li><code>salzstand/update/install</code> - OTA starten (Befehl)</li>
        </ul>
      </div>
    </details>
  </div>
{/if}

<style>
  .config-section {
    border: 1px solid var(--surface-border);
    padding: 16px;
    border-radius: 14px;
    background: var(--card-grad);
    box-shadow: var(--shadow);
  }
  h2 {
    margin: 0 0 8px 0;
    color: var(--text-main);
  }
  h3 {
    margin-top: 20px;
    margin-bottom: 8px;
    color: var(--accent);
  }
  p {
    color: var(--text-muted);
    margin-top: 0;
  }
  label {
    display: block;
    margin: 10px 0 6px;
    color: var(--text-main);
    font-weight: 600;
  }
  .field-row {
    display: grid;
    grid-template-columns: minmax(150px, 200px) 1fr;
    align-items: center;
    gap: 12px;
    margin: 10px 0;
  }
  .field-row--top-align {
    align-items: start;
  }
  .field-row span {
    white-space: nowrap;
    text-align: left;
    padding-left: 0;
  }
  .field-control {
    width: min(360px, 100%);
  }
  .field-control--inline {
    display: grid;
    grid-template-columns: 80px 1fr;
    gap: 8px;
    align-items: start;
  }
  input {
    margin-left: 0;
    width: min(360px, 100%);
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    padding: 8px 10px;
    background: rgba(255, 255, 255, 0.14);
    color: var(--text-main);
  }
  .field-control input {
    width: 100%;
  }
  .field-control--inline select {
    width: 100%;
  }
  .input-invalid {
    border-color: #ff6b6b;
    box-shadow: 0 0 0 1px rgba(255, 107, 107, 0.25);
    background: rgba(255, 107, 107, 0.08);
  }
  .field-error {
    margin: 6px 0 0;
    font-size: 0.82rem;
    color: #ff9a8f;
  }
  .field-error--inline,
  .field-help {
    grid-column: 1 / -1;
  }
  select {
    width: min(360px, 100%);
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    padding: 8px 10px;
    background: rgba(255, 255, 255, 0.14);
    color: var(--text-main);
  }
  select option {
    background-color: #0d1b33;
    color: #e8f0ff;
  }
  :global(html[data-theme='day']) select option {
    background-color: #f2fff7;
    color: #183326;
  }
  .input-action-row {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    gap: 8px;
    align-items: center;
  }
  .icon-button {
    width: 40px;
    height: 40px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    background: rgba(255, 255, 255, 0.08);
    color: var(--accent);
    cursor: pointer;
  }
  .icon-button svg {
    width: 18px;
    height: 18px;
    fill: currentColor;
  }
  .compact-row {
    margin-top: 4px;
  }
  .choice-grid {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 10px;
    margin: 12px 0 4px;
  }
  .choice-card {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 10px 12px;
    border: 1px solid var(--surface-border);
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.05);
    cursor: pointer;
  }
  .choice-card input {
    width: 16px;
    height: 16px;
    margin: 0;
  }
  .choice-card span {
    color: var(--text-main);
    font-weight: 600;
  }
  .helper-text {
    margin-top: 4px;
    margin-bottom: 6px;
    font-size: 0.84rem;
    color: var(--text-muted);
  }
  .helper-text.error {
    color: #ff9a8f;
  }
  .checkbox-row {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .checkbox-row input {
    margin: 0;
    width: 16px;
    height: 16px;
  }
  ul {
    margin-top: 8px;
    margin-bottom: 0;
    padding-left: 18px;
  }
  .device-id-row {
    font-size: 0.9rem;
    color: var(--text-muted);
    margin-bottom: 8px;
  }
  .device-id-code {
    font-family: monospace;
    background: rgba(98, 184, 221, 0.15);
    border-radius: 5px;
    padding: 2px 6px;
    color: var(--accent);
    font-size: 0.85rem;
    user-select: all;
    cursor: text;
  }
  .topic-heading {
    font-weight: 600;
    color: var(--text-main);
    margin-top: 14px;
    margin-bottom: 4px;
    font-size: 0.9rem;
  }
  .topic-card {
    margin-top: 14px;
    border: 1px solid var(--surface-border);
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.05);
    overflow: hidden;
  }
  .topic-card summary {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 12px 14px;
    cursor: pointer;
    list-style: none;
    color: var(--text-main);
    user-select: none;
  }
  .topic-card summary::-webkit-details-marker {
    display: none;
  }
  .topic-card summary::after {
    content: '▾';
    font-size: 0.9rem;
    color: var(--accent);
    transition: transform 0.2s ease;
  }
  .topic-card[open] summary::after {
    transform: rotate(180deg);
  }
  .topic-card-title {
    font-weight: 600;
    color: var(--text-main);
  }
  .topic-card-meta {
    margin-left: auto;
    font-size: 0.8rem;
    color: var(--text-muted);
  }
  .topic-card-content {
    padding: 0 14px 14px;
    border-top: 1px solid rgba(255, 255, 255, 0.06);
  }
  .topic-list {
    margin-top: 4px;
    margin-bottom: 8px;
    padding-left: 0;
    list-style: none;
  }
  .topic-list li {
    padding: 2px 0;
    font-size: 0.82rem;
    color: var(--text-muted);
    word-break: break-all;
  }
  .topic-list li:not(.topic-group) {
    padding-left: 12px;
  }
  .topic-group {
    color: var(--text-main);
    font-weight: 600;
    font-size: 0.78rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    margin-top: 10px;
    opacity: 0.65;
  }
  .topic-list code {
    font-family: monospace;
    font-size: 0.82rem;
    background: rgba(255, 255, 255, 0.07);
    border-radius: 4px;
    padding: 1px 5px;
    color: var(--text-main);
  }
  .primary {
    margin-top: 10px;
    padding: 10px 14px;
    border: none;
    border-radius: 10px;
    background: var(--button-active-bg);
    color: var(--button-active-text);
    font-weight: 600;
    cursor: pointer;
  }
  .sensor-sketch {
    margin-top: 14px;
    margin-bottom: 8px;
    padding: 10px;
    border: 1px solid var(--surface-border);
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.05);
    color: var(--text-main);
  }
  .sensor-sketch svg {
    width: 100%;
    max-width: 500px;
    height: auto;
    display: block;
    margin: 0 auto;
  }
  .sketch-note {
    margin: 2px 0 2px;
    font-size: 0.85rem;
    color: var(--text-muted);
    text-align: center;
  }

  @media (max-width: 640px) {
    .field-row {
      grid-template-columns: 1fr;
      gap: 6px;
    }
    .field-control--inline {
      grid-template-columns: 80px 1fr;
    }
    .choice-grid {
      grid-template-columns: 1fr;
    }
    .topic-card summary {
      align-items: flex-start;
      flex-direction: column;
    }
    .topic-card-meta {
      margin-left: 0;
    }
  }
  .mqtt-status-row {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 10px 14px;
    border-radius: 10px;
    background: rgba(255, 255, 255, 0.05);
    margin-bottom: 14px;
  }
  .mqtt-dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    flex-shrink: 0;
  }
  .mqtt-dot--connected { background: #4caf50; box-shadow: 0 0 6px rgba(76,175,80,0.5); }
  .mqtt-dot--connecting,
  .mqtt-dot--backoff { background: #ff9800; box-shadow: 0 0 6px rgba(255,152,0,0.5); }
  .mqtt-dot--disconnected { background: #f44336; }
  .mqtt-dot--uninitialized,
  .mqtt-dot--unknown { background: rgba(200,200,200,0.25); }
  .mqtt-state-text {
    flex: 1;
    font-size: 0.9rem;
    color: var(--text-muted);
  }
  .secondary-sm {
    padding: 5px 12px;
    font-size: 0.8rem;
    border: 1px solid var(--surface-border);
    border-radius: 8px;
    background: rgba(255,255,255,0.07);
    color: var(--text-main);
    cursor: pointer;
    white-space: nowrap;
  }
  .secondary-sm:hover {
    background: rgba(255,255,255,0.12);
  }
</style>