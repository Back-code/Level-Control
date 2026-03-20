<script>
  import { onMount } from 'svelte';

  export let embedded = false;
  let logs = [];
  let nvsData = null;
  let ws;

  onMount(() => {
    ws = new WebSocket('ws://' + window.location.host + '/ws');

    ws.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      if (msg.type === 'log') {
        logs = [msg, ...logs].slice(0, 50);
      }
    };

    // Fetch NVS content
    fetch('/api/nvs')
      .then(r => r.json())
      .then(nvs => {
        nvsData = nvs;
      });

    return () => {
      if (ws) ws.close();
    };
  });
</script>

<div class="debug-overlay" class:embedded>
  <h2>Debug</h2>
  <div class="logs">
    {#each logs as log}
      <div class="log {log.level}">{log.timestamp} [{log.level}] {log.message}</div>
    {/each}
  </div>
  <div class="nvs">
    <h3>NVS Snapshot</h3>
    {#if nvsData}
      <pre>{JSON.stringify(nvsData, null, 2)}</pre>
    {:else}
      <p>Lade NVS-Daten...</p>
    {/if}
  </div>
</div>

<style>
  .debug-overlay {
    background: var(--surface-2);
    color: var(--text-main);
    padding: 16px;
    border-radius: 14px;
    border: 1px solid var(--surface-border);
    min-height: 380px;
    max-height: 70vh;
    overflow-y: auto;
  }
  .debug-overlay:not(.embedded) {
    position: fixed;
    top: 0;
    right: 0;
    width: 400px;
    height: 100%;
    max-height: none;
    border-radius: 0;
    border: 0;
  }
  h2 {
    margin-top: 0;
  }
  h3 {
    margin-bottom: 8px;
    color: var(--accent);
  }
  .logs {
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    padding: 10px;
    background: rgba(255, 255, 255, 0.06);
    margin-bottom: 14px;
    max-height: 280px;
    overflow-y: auto;
  }
  .log {
    margin: 5px 0;
    font-family: Consolas, monospace;
    font-size: 0.85rem;
  }
  .nvs {
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    padding: 10px;
    background: rgba(255, 255, 255, 0.05);
  }
  pre {
    margin: 0;
    white-space: pre-wrap;
    word-break: break-word;
    font-size: 0.82rem;
    color: var(--text-main);
  }
  .ERROR { color: #f87171; }
  .WARN { color: #fbbf24; }
  .INFO { color: var(--text-main); }
  .DEBUG { color: #94a3b8; }
</style>