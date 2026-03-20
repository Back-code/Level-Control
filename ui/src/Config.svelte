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
    }).then(() => alert('Sensor-Konfiguration gespeichert'));
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

  function restart() {
    fetch('/api/restart', { method: 'POST' });
  }

  onMount(() => {
    loadAllConfig();
  });
</script>

{#if module === 'sensor'}
  <div class="config-section">
    <h2>Sensor</h2>
    <p>Messgeometrie und Korrekturwerte für die Füllstandsberechnung.</p>
    <label>Behälterhöhe (cm): <input type="number" bind:value={data.behaelterhoehe} /></label>
    <label>Offset (cm): <input type="number" bind:value={data.offset} /></label>
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

<button class="ghost" on:click={restart}>ESP32 Neustart</button>

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
  input {
    margin-left: 10px;
    width: min(360px, 90%);
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
  .ghost {
    margin-top: 16px;
    padding: 10px 14px;
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    background: var(--button-bg);
    color: var(--button-text);
    cursor: pointer;
  }
</style>