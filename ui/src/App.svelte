<script>
  import { onMount } from 'svelte';
  import Dashboard from './Dashboard.svelte';
  import Config from './Config.svelte';
  import DebugOverlay from './DebugOverlay.svelte';

  let activeTab = 'dashboard';
  let theme = 'night';
  const DATA_CACHE_KEY = 'salzstand-last-data';
  let data = {
    rohdistanz: 0,
    salzstandCm: 0,
    salzstandPercent: 0,
    wifiSignal: 0,
    uptime: 0,
    ip: '',
    ssid: '',
    bssid: '',
    behaelterhoehe: 95,
    offset: 0
  };

  let ws;

  function restoreCachedData() {
    try {
      const raw = localStorage.getItem(DATA_CACHE_KEY);
      if (!raw) return;
      const cached = JSON.parse(raw);
      data = { ...data, ...cached };
    } catch (_) {
      // Ignore invalid cache and continue with live updates.
    }
  }

  function persistData() {
    try {
      localStorage.setItem(DATA_CACHE_KEY, JSON.stringify(data));
    } catch (_) {
      // Ignore storage write errors (e.g. private mode quota issues).
    }
  }

  function applyTheme(nextTheme) {
    theme = nextTheme;
    document.documentElement.setAttribute('data-theme', theme);
    localStorage.setItem('salzstand-theme', theme);
  }

  function setTheme(nextTheme) {
    if (nextTheme === theme) return;
    applyTheme(nextTheme);
  }

  onMount(() => {
    const storedTheme = localStorage.getItem('salzstand-theme');
    applyTheme(storedTheme === 'day' ? 'day' : 'night');
    restoreCachedData();

    // Connect to WebSocket
    ws = new WebSocket('ws://' + window.location.host + '/ws');

    ws.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      if (msg.type === 'sensor') {
        data.rohdistanz = msg.rohdistanz;
        data.salzstandCm = msg.salzstandCm;
        data.salzstandPercent = msg.salzstandPercent;
      } else if (msg.type === 'wifi') {
        data.wifiSignal = msg.signal;
        data.ip = msg.ip;
        data.ssid = msg.ssid;
        data.bssid = msg.bssid;
      } else if (msg.type === 'uptime') {
        data.uptime = msg.uptime;
      }
      persistData();
    };

    // Load initial config
    loadConfig();

    return () => {
      if (ws) ws.close();
    };
  });

  function loadConfig() {
    fetch('/api/config').then(r => r.json()).then(config => {
      data.behaelterhoehe = config.behaelterhoehe;
      data.offset = config.offset;
      persistData();
    });
  }

  function restartEsp() {
    fetch('/api/restart', { method: 'POST' });
  }

  const tabs = [
    { id: 'dashboard', label: 'Dashboard', subtitle: 'Live-Messwerte und Netzwerkstatus in einer kompakten Uebersicht.' },
    { id: 'sensor', label: 'Behälter', subtitle: 'Behälterparameter für Messgeometrie und Offset konfigurieren.' },
    { id: 'wifi', label: 'WiFi', subtitle: 'WLAN-Zugang und optionale statische Netzwerkdaten verwalten.' },
    { id: 'mqtt_ha', label: 'MQTT & HA', subtitle: 'Broker, Discovery und Home-Assistant-Anbindung zentral steuern.' },
    { id: 'debug', label: 'Debug', subtitle: 'Live-Logs sowie NVS-Snapshot fuer Diagnose und Fehleranalyse.' }
  ];

  $: activeTabMeta = tabs.find(tab => tab.id === activeTab) || tabs[0];
</script>

<main>
  <header class="topbar">
    <div class="topbar-row">
      <div class="brand">
        <span class="brand-icon" aria-hidden="true">
          <svg viewBox="0 0 24 24"><path d="M12 2 3 6v6c0 5.25 3.66 9.8 9 10 5.34-.2 9-4.75 9-10V6l-9-4zm0 3.18 6 2.67v4.15c0 3.76-2.47 7.24-6 7.98-3.53-.74-6-4.22-6-7.98V7.85l6-2.67zM8 12h8v2H8v-2z"/></svg>
        </span>
        <div class="brand-copy">
          <h1>Salzstand Control</h1>
          <p>Smart Reservoir Monitor</p>
        </div>
      </div>
      <div class="top-controls">
        <button class="restart-btn" on:click={restartEsp}>
          <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 6V3L8 7l4 4V8c2.76 0 5 2.24 5 5a5 5 0 0 1-5 5 5 5 0 0 1-4.9-4H5.08A7 7 0 0 0 12 20a7 7 0 0 0 0-14z"/></svg>
          Neustart
        </button>
        <div class="theme-switch" role="group" aria-label="Theme-Auswahl">
          <button class="theme-btn" class:active={theme === 'day'} on:click={() => setTheme('day')}>
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3a1 1 0 0 1 1 1v1a1 1 0 1 1-2 0V4a1 1 0 0 1 1-1zm0 15a4 4 0 1 0 0-8 4 4 0 0 0 0 8zm8-5a1 1 0 0 1-1 1h-1a1 1 0 1 1 0-2h1a1 1 0 0 1 1 1zM7 13a1 1 0 0 1-1 1H5a1 1 0 1 1 0-2h1a1 1 0 0 1 1 1zm9.66 5.66a1 1 0 0 1-1.41 0l-.7-.71a1 1 0 0 1 1.41-1.41l.7.7a1 1 0 0 1 0 1.42zM9.45 6.45a1 1 0 0 1-1.41 0l-.71-.7a1 1 0 1 1 1.41-1.42l.71.71a1 1 0 0 1 0 1.41zm7.2-1.66a1 1 0 0 1 0 1.41l-.7.71a1 1 0 0 1-1.42-1.41l.71-.71a1 1 0 0 1 1.41 0zM9.45 17.55a1 1 0 0 1 0 1.41l-.71.7a1 1 0 0 1-1.41-1.41l.7-.71a1 1 0 0 1 1.42 0z"/></svg>
            Tag
          </button>
          <button class="theme-btn" class:active={theme === 'night'} on:click={() => setTheme('night')}>
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 14.5A8.5 8.5 0 1 1 9.5 4a1 1 0 0 1 1.05 1.47A6.5 6.5 0 1 0 18.53 13.45 1 1 0 0 1 20 14.5z"/></svg>
            Nacht
          </button>
        </div>
      </div>
    </div>
    <nav>
      {#each tabs as tab}
        <button on:click={() => activeTab = tab.id} class:active={activeTab === tab.id}>
          <span class={"tab-icon " + tab.id} aria-hidden="true">
            {#if tab.id === 'dashboard'}
              <svg viewBox="0 0 24 24"><path d="M4 13h7V4H4v9zm9 7h7V4h-7v16zM4 20h7v-5H4v5z"/></svg>
            {:else if tab.id === 'sensor'}
              <svg viewBox="0 0 24 24"><path d="M7 4a2 2 0 0 0-2 2v12a5 5 0 1 0 10 0V6a2 2 0 0 0-2-2H7zm2 2h2v8.26a3 3 0 1 1-2 0V6z"/></svg>
            {:else if tab.id === 'wifi'}
              <svg viewBox="0 0 24 24"><path d="M12 18a2 2 0 1 0 0 4 2 2 0 0 0 0-4zm0-4c2.56 0 4.92 1.04 6.62 2.73l1.42-1.41A11.96 11.96 0 0 0 12 12c-3.12 0-5.96 1.19-8.04 3.14l1.42 1.41A9.33 9.33 0 0 1 12 14zm0-4c3.74 0 7.12 1.51 9.56 3.95l1.41-1.41A15.45 15.45 0 0 0 12 8c-4.3 0-8.19 1.75-10.97 4.57l1.41 1.41A13.44 13.44 0 0 1 12 10z"/></svg>
            {:else if tab.id === 'mqtt_ha'}
              <svg viewBox="0 0 24 24"><path d="M12 2l6 6h-4v5h-4V8H6l6-6zm-8 12h4v6h8v-6h4v8H4v-8z"/></svg>
            {:else}
              <svg viewBox="0 0 24 24"><path d="M19.14 12.94a7.43 7.43 0 0 0 .05-.94 7.43 7.43 0 0 0-.05-.94l2.03-1.58a.5.5 0 0 0 .12-.64l-1.92-3.32a.5.5 0 0 0-.6-.22l-2.39.96a7.16 7.16 0 0 0-1.63-.94l-.36-2.54A.5.5 0 0 0 13.9 2h-3.8a.5.5 0 0 0-.49.42l-.36 2.54a7.16 7.16 0 0 0-1.63.94l-2.39-.96a.5.5 0 0 0-.6.22L2.71 8.48a.5.5 0 0 0 .12.64l2.03 1.58a7.43 7.43 0 0 0-.05.94c0 .32.02.63.05.94l-2.03 1.58a.5.5 0 0 0-.12.64l1.92 3.32a.5.5 0 0 0 .6.22l2.39-.96c.5.39 1.05.72 1.63.94l.36 2.54a.5.5 0 0 0 .49.42h3.8a.5.5 0 0 0 .49-.42l.36-2.54c.58-.22 1.13-.55 1.63-.94l2.39.96a.5.5 0 0 0 .6-.22l1.92-3.32a.5.5 0 0 0-.12-.64l-2.03-1.58zM12 15.5A3.5 3.5 0 1 1 12 8a3.5 3.5 0 0 1 0 7.5z"/></svg>
            {/if}
          </span>
          <span>{tab.label}</span>
        </button>
      {/each}
    </nav>
    <p class="subtitle">{activeTabMeta.subtitle}</p>
  </header>

  <section class="module-shell">
    {#if activeTab === 'dashboard'}
      <Dashboard bind:data />
    {:else if activeTab === 'sensor'}
      <Config bind:data {loadConfig} module="sensor" />
    {:else if activeTab === 'wifi'}
      <Config bind:data {loadConfig} module="wifi" />
    {:else if activeTab === 'mqtt_ha'}
      <Config bind:data {loadConfig} module="mqtt_ha" />
    {:else if activeTab === 'debug'}
      <DebugOverlay embedded={true} />
    {/if}
  </section>
</main>

<style>
  :global(:root) {
    --bg-main: radial-gradient(circle at 20% 0%, #0f2140 0%, #0b1630 46%, #070d1f 100%);
    --text-main: #e8f0ff;
    --text-muted: #a8bddf;
    --surface: rgba(9, 20, 42, 0.72);
    --surface-2: rgba(8, 18, 36, 0.8);
    --surface-border: rgba(112, 146, 198, 0.25);
    --button-bg: #1c345d;
    --button-text: #dceafe;
    --button-active-bg: #2c74bb;
    --button-active-text: #ffffff;
    --accent: #62b8dd;
    --card-grad: linear-gradient(160deg, rgba(20, 38, 74, 0.9) 0%, rgba(12, 27, 56, 0.9) 100%);
    --shadow: 0 12px 30px rgba(0, 6, 20, 0.45);
  }

  :global(html[data-theme='day']) {
    --bg-main: radial-gradient(circle at 20% 0%, #e9f9ec 0%, #dff5e7 45%, #eafcf1 100%);
    --text-main: #183326;
    --text-muted: #3e6a55;
    --surface: rgba(255, 255, 255, 0.86);
    --surface-2: rgba(248, 255, 251, 0.9);
    --surface-border: rgba(80, 146, 110, 0.26);
    --button-bg: #d9efe0;
    --button-text: #1b3d2f;
    --button-active-bg: #3f9a66;
    --button-active-text: #ffffff;
    --accent: #57b67b;
    --card-grad: linear-gradient(160deg, #ffffff 0%, #f2fff7 100%);
    --shadow: 0 12px 26px rgba(53, 104, 73, 0.14);
  }

  :global(body) {
    margin: 0;
    background: var(--bg-main);
    color: var(--text-main);
    transition: background 0.25s ease, color 0.25s ease;
  }

  main {
    font-family: 'Trebuchet MS', 'Segoe UI', sans-serif;
    max-width: 1300px;
    margin: 0 auto;
    padding: 16px;
  }

  .topbar {
    position: sticky;
    top: 0;
    z-index: 20;
    backdrop-filter: blur(10px);
    background: var(--surface);
    border: 1px solid var(--surface-border);
    border-radius: 16px;
    padding: 14px;
    margin-bottom: 16px;
    box-shadow: var(--shadow);
  }

  .topbar-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 14px;
  }

  h1 {
    margin: 0;
    font-size: clamp(1.35rem, 3vw, 2rem);
    color: var(--text-main);
    letter-spacing: 0.6px;
    text-transform: uppercase;
    font-weight: 900;
    text-shadow: 0 1px 0 rgba(0, 0, 0, 0.12), 0 8px 20px rgba(0, 0, 0, 0.18);
  }

  .brand {
    display: flex;
    align-items: center;
    gap: 10px;
    min-width: 0;
  }

  .brand-icon {
    width: 40px;
    height: 40px;
    border-radius: 12px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: linear-gradient(150deg, rgba(98, 184, 221, 0.2) 0%, rgba(98, 184, 221, 0.05) 100%);
    border: 1px solid var(--surface-border);
    color: var(--accent);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.08);
  }

  .brand-icon svg {
    width: 20px;
    height: 20px;
    fill: currentColor;
  }

  .brand-copy {
    min-width: 0;
  }

  .brand-copy p {
    margin: 3px 0 0;
    color: var(--text-muted);
    font-size: 0.8rem;
    letter-spacing: 0.16em;
    text-transform: uppercase;
  }

  .theme-switch {
    display: inline-flex;
    gap: 6px;
    padding: 4px;
    border: 1px solid var(--surface-border);
    border-radius: 999px;
    background: var(--surface-2);
  }

  .top-controls {
    display: inline-flex;
    align-items: center;
    gap: 8px;
  }

  .restart-btn {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 6px 10px;
    border-radius: 999px;
    border: 1px solid var(--surface-border);
    background: var(--button-bg);
    color: var(--button-text);
    font-size: 0.86rem;
    font-weight: 700;
    cursor: pointer;
  }

  .restart-btn svg {
    width: 15px;
    height: 15px;
    fill: currentColor;
  }

  .restart-btn:hover {
    background: var(--button-active-bg);
    color: var(--button-active-text);
  }

  .theme-btn {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 6px 10px;
    border-radius: 999px;
    border: none;
    background: transparent;
    color: var(--text-muted);
    font-size: 0.86rem;
    font-weight: 700;
    cursor: pointer;
  }

  .theme-btn svg {
    width: 15px;
    height: 15px;
    fill: currentColor;
  }

  .theme-btn.active {
    background: var(--button-active-bg);
    color: var(--button-active-text);
    box-shadow: 0 6px 14px rgba(0, 0, 0, 0.18);
  }

  nav {
    position: relative;
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-top: 4px;
    padding-bottom: 8px;
  }

  nav button {
    position: relative;
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 10px 16px;
    border: none;
    border-radius: 999px;
    background: var(--button-bg);
    color: var(--button-text);
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s ease;
  }

  nav button.active {
    background: var(--button-active-bg);
    color: var(--button-active-text);
    box-shadow: 0 8px 18px rgba(8, 28, 58, 0.35);
  }

  nav button::after {
    content: '';
    position: absolute;
    left: 16px;
    right: 16px;
    bottom: -8px;
    height: 3px;
    border-radius: 999px;
    transform: scaleX(0);
    transform-origin: center;
    background: linear-gradient(90deg, var(--accent) 0%, #9fd5ff 100%);
    transition: transform 0.22s ease;
  }

  nav button.active::after {
    transform: scaleX(1);
  }

  nav button:hover {
    transform: translateY(-1px);
  }

  .tab-icon {
    width: 28px;
    height: 28px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.22);
    color: var(--text-main);
    border: 1px solid var(--surface-border);
  }

  .tab-icon svg {
    width: 16px;
    height: 16px;
    fill: currentColor;
  }

  .tab-icon.dashboard { color: #6fc0ff; }
  .tab-icon.sensor { color: #7fd996; }
  .tab-icon.wifi { color: #ff8a80; }
  .tab-icon.mqtt_ha { color: #c4a0ff; }
  .tab-icon.debug { color: #ffd37d; }

  :global(html[data-theme='day']) .tab-icon.dashboard { color: #2363b4; }
  :global(html[data-theme='day']) .tab-icon.sensor { color: #238043; }
  :global(html[data-theme='day']) .tab-icon.wifi { color: #c34b4b; }
  :global(html[data-theme='day']) .tab-icon.mqtt_ha { color: #7b48c9; }
  :global(html[data-theme='day']) .tab-icon.debug { color: #b07a13; }

  .subtitle {
    margin: 12px 2px 0;
    color: var(--text-muted);
    font-size: 0.92rem;
    line-height: 1.35;
  }

  .module-shell {
    background: var(--surface);
    border: 1px solid var(--surface-border);
    border-radius: 16px;
    padding: 18px;
    box-shadow: var(--shadow);
  }

  @media (max-width: 640px) {
    main {
      padding: 10px;
    }

    .module-shell {
      padding: 12px;
    }

    nav {
      gap: 6px;
      padding-bottom: 6px;
    }

    .topbar-row {
      align-items: flex-start;
      flex-direction: column;
      margin-bottom: 12px;
    }

    nav button {
      padding: 8px 12px;
      font-size: 0.9rem;
    }

    nav button::after {
      left: 12px;
      right: 12px;
      bottom: -6px;
    }
  }
</style>