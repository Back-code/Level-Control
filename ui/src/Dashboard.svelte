<script>
  export let data;
</script>

<section class="dashboard-hero">
  <p class="eyebrow">Monitoring</p>
  <h2>Live Dashboard</h2>
  <p class="hero-sub">Echtzeitansicht fuer Fuellstand, Netzwerk und Laufzeit des Sensors.</p>
</section>

<div class="grid">
  <div class="card">
    <h2><span class="mini-icon"><svg viewBox="0 0 24 24"><path d="M3 17h18v2H3v-2zm2-8h14v2H5V9zm3-6h8v2H8V3z"/></svg></span>Aktuelle Distanz</h2>
    <p class="value">{data.rohdistanz.toFixed(2)} m</p>
  </div>
  <div class="card">
    <h2><span class="mini-icon"><svg viewBox="0 0 24 24"><path d="M5 20h14V4H5v16zm2-2v-4h10v4H7zm0-6V6h10v6H7z"/></svg></span>Salzstand</h2>
    <p class="value">{data.salzstandCm.toFixed(1)} cm</p>
    <p class="value">{data.salzstandPercent.toFixed(1)} %</p>
  </div>
  <div class="card">
    <h2><span class="mini-icon"><svg viewBox="0 0 24 24"><path d="M12 18a2 2 0 1 0 0 4 2 2 0 0 0 0-4zm0-4c2.56 0 4.92 1.04 6.62 2.73l1.42-1.41A11.96 11.96 0 0 0 12 12c-3.12 0-5.96 1.19-8.04 3.14l1.42 1.41A9.33 9.33 0 0 1 12 14z"/></svg></span>WiFi Signal</h2>
    <p class="value">{data.wifiSignal} dBm</p>
  </div>
  <div class="card">
    <h2><span class="mini-icon"><svg viewBox="0 0 24 24"><path d="M11 7h2v6l4 2-1 1.73L11 14V7zm1-5a10 10 0 1 0 10 10A10 10 0 0 0 12 2z"/></svg></span>Uptime</h2>
    <p class="value">{Math.floor(data.uptime / 3600)}h {Math.floor((data.uptime % 3600) / 60)}m</p>
  </div>
  <div class="card">
    <h2><span class="mini-icon"><svg viewBox="0 0 24 24"><path d="M12 2 2 7l10 5 8-4v6h2V7L12 2zm-8 9v6l8 4 8-4v-6l-8 4-8-4z"/></svg></span>Netzwerk</h2>
    <p>IP: {data.ip}</p>
    <p>SSID: {data.ssid}</p>
    <p>BSSID: {data.bssid}</p>
  </div>
</div>

<style>
  .dashboard-hero {
    margin-bottom: 14px;
    padding: 12px 14px;
    border-radius: 12px;
    border: 1px solid var(--surface-border);
    background: linear-gradient(135deg, rgba(255, 255, 255, 0.08) 0%, rgba(255, 255, 255, 0.02) 100%);
  }

  .eyebrow {
    margin: 0;
    color: var(--accent);
    text-transform: uppercase;
    letter-spacing: 0.12em;
    font-size: 0.74rem;
    font-weight: 700;
  }

  h2 {
    margin: 4px 0;
    font-size: clamp(1.2rem, 2.6vw, 1.8rem);
    color: transparent;
    background: linear-gradient(100deg, var(--text-main) 0%, var(--accent) 100%);
    -webkit-background-clip: text;
    background-clip: text;
  }

  .hero-sub {
    margin: 0;
    color: var(--text-muted);
    font-size: 0.9rem;
  }

  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: 14px;
  }

  .card {
    border: 1px solid var(--surface-border);
    padding: 16px;
    border-radius: 14px;
    background: var(--card-grad);
    box-shadow: var(--shadow);
  }

  .card h2 {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 1rem;
    margin: 0 0 8px 0;
    color: var(--text-muted);
  }

  .mini-icon {
    width: 24px;
    height: 24px;
    border-radius: 8px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.08);
    color: var(--accent);
  }

  .mini-icon svg {
    width: 14px;
    height: 14px;
    fill: currentColor;
  }

  .card p {
    margin: 4px 0;
    font-weight: 600;
    color: var(--text-main);
  }

  .card p.value {
    font-size: clamp(1.35rem, 3.2vw, 2.05rem);
    font-weight: 800;
    letter-spacing: 0.02em;
    line-height: 1.1;
  }
</style>
