<script>
  import { onMount } from 'svelte';

  export let data;
  export let loadConfig;
  export let module = 'sensor';

  let wifiConfig = {
    ssid: '',
    password: '',
    staticIp: { ip: '', gateway: '', subnet: '', dns: '' }
  };
  let mqttConfig = { server: '', port: 1883, user: '', password: '', discovery: true };

  function loadAllConfig() {
    loadConfig();

    fetch('/api/wifi')
      .then(r => r.json())
      .then(c => {
        wifiConfig = {
          ssid: c.ssid || '',
          password: c.password || '',
          staticIp: {
            ip: c.staticIp?.ip || '',
            gateway: c.staticIp?.gateway || '',
            subnet: c.staticIp?.subnet || '',
            dns: c.staticIp?.dns || ''
          }
        };
      });

    fetch('/api/mqtt')
      .then(r => r.json())
      .then(c => {
        mqttConfig = {
          server: c.server || '',
          port: c.port || 1883,
          user: c.user || '',
          password: c.password || '',
          discovery: c.discovery ?? true
        };
      });
  }

  function saveSensorConfig() {
    fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        behaelterhoehe: data.behaelterhoehe,
        offset: data.offset
      })
    }).then(() => alert('Behaelter-Konfiguration gespeichert'));
  }

  function saveWifiConfig() {
    fetch('/api/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ssid: wifiConfig.ssid,
        password: wifiConfig.password,
        staticIp: {
          ip: wifiConfig.staticIp.ip,
          gateway: wifiConfig.staticIp.gateway,
          subnet: wifiConfig.staticIp.subnet,
          dns: wifiConfig.staticIp.dns
        }
      })
    }).then(() => alert('WiFi-Konfiguration gespeichert. Neustart erforderlich.'));
  }

  function saveMqttConfig() {
    fetch('/api/mqtt', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(mqttConfig)
    }).then(() => alert('MQTT-Konfiguration gespeichert. Neustart erforderlich.'));
  }

  onMount(() => {
    loadAllConfig();
  });
</script>

{#if module === 'sensor'}
  <div class="config-section">
    <h2>Behälter</h2>
    <p>Messgeometrie und Korrekturwerte für die Füllstandsberechnung.</p>

    <label class="field-row">
      <span>Offset (cm):</span>
      <input type="number" bind:value={data.offset} />
    </label>

    <label class="field-row">
      <span>Behälterhöhe (cm):</span>
      <input type="number" bind:value={data.behaelterhoehe} />
    </label>

    <div class="sensor-sketch" aria-label="Erklärung Behälterhöhe und Offset">
      <svg viewBox="0 0 400 220" role="img">
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
    <label>SSID: <input bind:value={wifiConfig.ssid} /></label>
    <label>Passwort: <input type="password" bind:value={wifiConfig.password} /></label>
    <label>Statische IP: <input bind:value={wifiConfig.staticIp.ip} /></label>
    <label>Gateway: <input bind:value={wifiConfig.staticIp.gateway} /></label>
    <label>Subnetz: <input bind:value={wifiConfig.staticIp.subnet} /></label>
    <label>DNS: <input bind:value={wifiConfig.staticIp.dns} /></label>
    <button class="primary" on:click={saveWifiConfig}>Speichern</button>
  </div>
{:else if module === 'mqtt_ha'}
  <div class="config-section">
    <h2>MQTT & HA</h2>
    <p>Broker-Zugangsdaten und Home Assistant Discovery.</p>
    <label>Server: <input bind:value={mqttConfig.server} /></label>
    <label>Port: <input type="number" bind:value={mqttConfig.port} /></label>
    <label>Benutzer: <input bind:value={mqttConfig.user} /></label>
    <label>Passwort: <input type="password" bind:value={mqttConfig.password} /></label>
    <label class="checkbox-row"><input type="checkbox" bind:checked={mqttConfig.discovery} /> Home Assistant Discovery aktivieren</label>
    <button class="primary" on:click={saveMqttConfig}>Speichern</button>

    <h3>Home Assistant</h3>
    <p>Konfiguriere MQTT-Discovery oben. Die Sensoren werden automatisch in HA erkannt.</p>
    <p>Discovery-Topics:</p>
    <ul>
      <li>homeassistant/sensor/{'{deviceId}'}/fill_level/config</li>
      <li>homeassistant/sensor/{'{deviceId}'}/distance_cm/config</li>
      <li>homeassistant/sensor/{'{deviceId}'}/raw_distance/config</li>
      <li>homeassistant/sensor/{'{deviceId}'}/ping_us/config</li>
      <li>homeassistant/number/{'{deviceId}'}/behaelterhoehe/config</li>
      <li>homeassistant/number/{'{deviceId}'}/offset/config</li>
    </ul>
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
  .field-row span {
    white-space: nowrap;
    text-align: left;
    padding-left: 0;
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
  }
</style>