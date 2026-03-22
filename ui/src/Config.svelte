<script>
  import { onMount } from 'svelte';
  import { showNotice } from './dialogStore.js';
  import { SMTP_PROVIDERS, detectProvider } from './smtpProviders.js';
  import FieldLabel from './FieldLabel.svelte';
  import PasswordInput from './PasswordInput.svelte';

  export let data;
  export let loadConfig;
  export let module = 'sensor';
  export let onDirtyStateChange = () => {};

  let wifiConfig = {
    ssid: '',
    password: '',
    deviceName: 'Salzstand',
    staticIp: { ip: '', gateway: '', subnet: '', dns: '' }
  };
  let wifiHasPassword = false;
  let wifiMode = 'dhcp';
  let wifiNetworks = [];
  let wifiScanLoading = false;
  let wifiScanError = '';
  let wifiValidation = { attempted: false, touched: {} };
  let wifiConfigDirty = false;
  let sensorOffsetValue = '0';
  let sensorHeightValue = '95';
  let sensorConfigDirty = false;
  let sensorIntervalValue = '5';
  let sensorIntervalUnit = 'seconds';
  let sensorIntervalValidation = { attempted: false, touched: false };
  let sensorIntervalDirty = false;
  let lastSyncedSampleIntervalSeconds = null;
  let lastSyncedSensorOffset = null;
  let lastSyncedSensorHeight = null;
  let mqttConfig = { server: '', port: 1883, user: '', password: '', discovery: true };
  let mqttConfigDirty = false;
  let mqttHasPassword = false;
  let mqttDeviceId = '';
  const DEFAULT_PUSH_SENDER_NAME = 'Salzstand Control';
  const DEFAULT_PUSH_SUBJECT = 'Salzstand Control Warnung: Stand hat {level_percent}% erreicht. Salz nachfüllen!';
  const DEFAULT_PUSH_BODY = 'Der Füllstand hat {level_percent}% ({level_cm} cm) erreicht.\nBitte Salz nachfüllen!\nDein Salzstand Control';
  let pushConfig = {
    enabled: false,
    smtpServer: '',
    smtpPort: 587,
    useSsl: false,
    startTls: false,
    smtpSkipCertVerify: false,
    authUser: '',
    authPassword: '',
    senderName: DEFAULT_PUSH_SENDER_NAME,
    senderEmail: '',
    recipientEmail: '',
    triggerPercent: 20,
    sendHour: 8,
    sendMinute: 0,
    reminderCycle: 'day',
    reminderWeekday: 1,
    subjectTemplate: DEFAULT_PUSH_SUBJECT,
    bodyTemplate: DEFAULT_PUSH_BODY
  };
  let pushConfigDirty = false;
  let pushHasAuthPassword = false;
  let selectedProvider = 'custom';
  let smtpDiagResult = null;
  let smtpDiagLoading = false;
  let lastReportedDirtyState = null;

  const PASSWORD_MASK = '*****';
  const SAMPLE_UNITS = [
    { value: 'seconds', label: 'Sekunden', seconds: 1 },
    { value: 'minutes', label: 'Minuten', seconds: 60 },
    { value: 'hours', label: 'Stunden', seconds: 3600 },
    { value: 'days', label: 'Tage', seconds: 86400 }
  ];
  const MIN_SAMPLE_INTERVAL_SECONDS = 5;
  const PUSH_CYCLE_OPTIONS = [
    { value: 'day', label: 'Tag' },
    { value: 'week', label: 'Woche' },
    { value: 'month', label: 'Monat' }
  ];
  const WEEKDAY_OPTIONS = [
    { value: 1, label: 'Montag' },
    { value: 2, label: 'Dienstag' },
    { value: 3, label: 'Mittwoch' },
    { value: 4, label: 'Donnerstag' },
    { value: 5, label: 'Freitag' },
    { value: 6, label: 'Samstag' },
    { value: 7, label: 'Sonntag' }
  ];
  const TIME_OPTIONS = Array.from({ length: 96 }, (_, index) => {
    const hour = Math.floor(index / 4);
    const minute = (index % 4) * 15;
    const value = toTimeString(hour, minute);
    return { value, label: value };
  });

  function toTimeString(hour, minute) {
    const h = Math.max(0, Math.min(23, Number(hour) || 0));
    const m = Math.max(0, Math.min(59, Number(minute) || 0));
    return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
  }

  function parseTimeString(timeValue) {
    const [rawHour, rawMinute] = String(timeValue || '').split(':');
    const hour = Math.max(0, Math.min(23, Number(rawHour) || 0));
    const minute = Math.max(0, Math.min(59, Number(rawMinute) || 0));
    return { hour, minute };
  }

  function normalizeReminderCycle(value) {
    return ['day', 'week', 'month'].includes(value) ? value : 'day';
  }

  function normalizeReminderWeekday(value) {
    const numericValue = Number(value);
    if (!Number.isFinite(numericValue) || numericValue < 1 || numericValue > 7) {
      return 1;
    }
    return Math.round(numericValue);
  }

  function reminderCycleFromLegacyMinutes(value) {
    const cycleMinutes = Number(value) || 1440;
    if (cycleMinutes >= 43200) {
      return 'month';
    }
    if (cycleMinutes >= 10080) {
      return 'week';
    }
    return 'day';
  }

  function getPushEncryptionMode() {
    if (pushConfig.useSsl) {
      return 'ssl';
    }
    if (pushConfig.startTls) {
      return 'starttls';
    }
    return 'none';
  }

  function applyPushEncryptionMode(mode) {
    const useSsl = mode === 'ssl';
    const startTls = mode === 'starttls';
    let smtpPort = Math.max(1, Number(pushConfig.smtpPort) || 0);

    if (useSsl && (!smtpPort || smtpPort === 587)) {
      smtpPort = 465;
    }
    if (startTls && (!smtpPort || smtpPort === 465)) {
      smtpPort = 587;
    }
    if (!useSsl && !startTls && !smtpPort) {
      smtpPort = 587;
    }

    pushConfig = {
      ...pushConfig,
      useSsl,
      startTls,
      smtpPort
    };
  }

  function getReminderDescription() {
    if (pushConfig.reminderCycle === 'week') {
      return 'Erinnerung jede Woche am ausgewählten Tag zur Uhrzeit.';
    }
    if (pushConfig.reminderCycle === 'month') {
      return 'Erinnerung am ersten ausgewählten Wochentag des Monats zur Uhrzeit.';
    }
    return 'Erinnerung jeden Tag zur ausgewählten Uhrzeit.';
  }

  function isMaskedPassword(value) {
    return value === '***' || value === PASSWORD_MASK;
  }

  function normalizeMdnsHostname(deviceName) {
    const normalized = String(deviceName || '')
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/^-+|-+$/g, '')
      .slice(0, 63);

    return normalized || 'salzstand';
  }

  function getWifiLocalUrl() {
    return `http://${normalizeMdnsHostname(wifiConfig.deviceName)}.local/`;
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

  function markSensorIntervalDirty() {
    sensorIntervalDirty = true;
    sensorConfigDirty = true;
  }

  function markSensorConfigDirty() {
    sensorConfigDirty = true;
  }

  function markWifiConfigDirty() {
    wifiConfigDirty = true;
  }

  function markMqttConfigDirty() {
    mqttConfigDirty = true;
  }

  function markPushConfigDirty() {
    pushConfigDirty = true;
  }

  function isCurrentModuleDirty() {
    if (module === 'sensor') {
      return sensorConfigDirty || sensorIntervalDirty;
    }
    if (module === 'wifi') {
      return wifiConfigDirty;
    }
    if (module === 'mqtt_ha') {
      return mqttConfigDirty;
    }
    if (module === 'push') {
      return pushConfigDirty;
    }
    return false;
  }

  function reportDirtyState(force = false, currentDirtyState = isCurrentModuleDirty()) {
    if (force || currentDirtyState !== lastReportedDirtyState) {
      onDirtyStateChange(currentDirtyState);
      lastReportedDirtyState = currentDirtyState;
    }
  }

  export async function saveCurrentModule() {
    if (module === 'sensor') {
      return await saveSensorConfig();
    }
    if (module === 'wifi') {
      return await saveWifiConfig();
    }
    if (module === 'mqtt_ha') {
      return await saveMqttConfig();
    }
    if (module === 'push') {
      return await savePushConfig();
    }
    return true;
  }

  export function discardCurrentModuleChanges() {
    if (module === 'sensor') {
      sensorConfigDirty = false;
      sensorIntervalDirty = false;
      sensorIntervalValidation = { attempted: false, touched: false };
    } else if (module === 'wifi') {
      wifiConfigDirty = false;
      wifiValidation = { attempted: false, touched: {} };
    } else if (module === 'mqtt_ha') {
      mqttConfigDirty = false;
    } else if (module === 'push') {
      pushConfigDirty = false;
    }

    loadAllConfig();
    reportDirtyState(true);
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
        if (wifiConfigDirty) {
          return;
        }
        wifiConfig = {
          ssid: c.ssid || '',
          password: isMaskedPassword(c.password || '') ? '' : (c.password || ''),
          deviceName: c.deviceName || 'Salzstand',
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
        if (mqttConfigDirty) {
          return;
        }
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

    fetch('/api/push', { cache: 'no-store' })
      .then(r => r.json())
      .then(c => {
        if (pushConfigDirty) {
          return;
        }
        pushConfig = {
          enabled: c.enabled ?? false,
          smtpServer: c.smtpServer || '',
          smtpPort: c.smtpPort || 587,
          useSsl: c.useSsl ?? false,
          startTls: c.startTls ?? false,
          smtpSkipCertVerify: c.smtpSkipCertVerify ?? true,
          authUser: c.authUser || '',
          authPassword: isMaskedPassword(c.authPassword || '') ? '' : (c.authPassword || ''),
          senderName: c.senderName || DEFAULT_PUSH_SENDER_NAME,
          senderEmail: c.senderEmail || '',
          recipientEmail: c.recipientEmail || '',
          triggerPercent: c.triggerPercent ?? 20,
          sendHour: c.sendHour ?? 8,
          sendMinute: c.sendMinute ?? 0,
          reminderCycle: normalizeReminderCycle(c.reminderCycle || reminderCycleFromLegacyMinutes(c.cycleMinutes)),
          reminderWeekday: normalizeReminderWeekday(c.reminderWeekday ?? 1),
          subjectTemplate: c.subjectTemplate || DEFAULT_PUSH_SUBJECT,
          bodyTemplate: c.bodyTemplate || DEFAULT_PUSH_BODY
        };
        pushHasAuthPassword = (c.hasAuthPassword ?? false) || isMaskedPassword(c.authPassword || '');
        selectedProvider = detectProvider(c.smtpServer || '', c.smtpPort || 587);
      });
  }

  $: if (module === 'sensor' && data.sampleIntervalSeconds) {
    const nextSampleIntervalSeconds = Math.max(MIN_SAMPLE_INTERVAL_SECONDS, Number(data.sampleIntervalSeconds) || MIN_SAMPLE_INTERVAL_SECONDS);
    const shouldSyncFromConfig = !sensorIntervalDirty && nextSampleIntervalSeconds !== lastSyncedSampleIntervalSeconds;
    if (shouldSyncFromConfig) {
      syncSensorIntervalFromSeconds(nextSampleIntervalSeconds);
      lastSyncedSampleIntervalSeconds = nextSampleIntervalSeconds;
    }
  }

  $: if (module === 'sensor') {
    const nextOffset = String(data.offset ?? '');
    const nextHeight = String(data.behaelterhoehe ?? '');
    const shouldSyncGeometry = !sensorConfigDirty && (nextOffset !== lastSyncedSensorOffset || nextHeight !== lastSyncedSensorHeight);
    if (shouldSyncGeometry) {
      sensorOffsetValue = nextOffset;
      sensorHeightValue = nextHeight;
      lastSyncedSensorOffset = nextOffset;
      lastSyncedSensorHeight = nextHeight;
    }
  }

  $: reportDirtyState(false, isCurrentModuleDirty());

  async function saveSensorConfig() {
    sensorIntervalValidation = { ...sensorIntervalValidation, attempted: true };
    const sampleIntervalSeconds = getSampleIntervalSeconds();
    if (sampleIntervalSeconds < MIN_SAMPLE_INTERVAL_SECONDS) {
      showNotice('error', 'Die Ultraschall-Abtastrate muss mindestens 5 Sekunden betragen.');
      return false;
    }

    try {
      const response = await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          behaelterhoehe: Number(sensorHeightValue),
          offset: Number(sensorOffsetValue),
          sampleIntervalSeconds
        })
      });

      if (!response.ok) {
        showNotice('error', 'Behälter-Konfiguration konnte nicht gespeichert werden.');
        return false;
      }

      showNotice('success', 'Behälter-Konfiguration gespeichert.');
      data.offset = Number(sensorOffsetValue);
      data.behaelterhoehe = Number(sensorHeightValue);
      data.sampleIntervalSeconds = sampleIntervalSeconds;
      lastSyncedSensorOffset = String(data.offset);
      lastSyncedSensorHeight = String(data.behaelterhoehe);
      lastSyncedSampleIntervalSeconds = sampleIntervalSeconds;
      sensorConfigDirty = false;
      sensorIntervalDirty = false;
      sensorIntervalValidation = { attempted: false, touched: false };
      return true;
    } catch (_) {
      showNotice('error', 'Behälter-Konfiguration konnte nicht gespeichert werden.');
      return false;
    }
  }

  async function saveWifiConfig() {
    if (wifiMode === 'static') {
      wifiValidation = { ...wifiValidation, attempted: true };
      if (!wifiConfig.staticIp.ip || !wifiConfig.staticIp.subnet || !wifiConfig.staticIp.dns) {
        showNotice('error', 'Für statische IP sind Statische IP, Subnetz und DNS erforderlich.');
        return false;
      }
    }

    try {
      const response = await fetch('/api/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          ssid: wifiConfig.ssid,
          password: wifiConfig.password,
          deviceName: wifiConfig.deviceName,
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
        return false;
      }

      showNotice('success', 'WiFi-Konfiguration gespeichert. Neustart erforderlich.');
      wifiConfigDirty = false;
      wifiValidation = { attempted: false, touched: {} };
      return true;
    } catch (_) {
      showNotice('error', 'WiFi-Konfiguration konnte nicht gespeichert werden.');
      return false;
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
        return false;
      }

      showNotice('success', 'MQTT-Konfiguration gespeichert. Verbindung wird sofort neu aufgebaut.');
      mqttConfigDirty = false;
      return true;
    } catch (_) {
      showNotice('error', 'MQTT-Konfiguration konnte nicht gespeichert werden.');
      return false;
    }
  }

  async function savePushConfig() {
    const triggerPercent = Math.max(0, Math.min(100, Number(pushConfig.triggerPercent) || 0));
    const time = parseTimeString(toTimeString(pushConfig.sendHour, pushConfig.sendMinute));
    const useSsl = Boolean(pushConfig.useSsl);
    const startTls = !useSsl && Boolean(pushConfig.startTls);
    const reminderCycle = normalizeReminderCycle(pushConfig.reminderCycle);
    const reminderWeekday = normalizeReminderWeekday(pushConfig.reminderWeekday);
    const cycleMinutes = reminderCycle === 'week' ? 10080 : reminderCycle === 'month' ? 43200 : 1440;
    let smtpPort = Math.max(1, Number(pushConfig.smtpPort) || 0);
    if (useSsl && (!smtpPort || smtpPort === 587)) {
      smtpPort = 465;
    }
    if (startTls && (!smtpPort || smtpPort === 465)) {
      smtpPort = 587;
    }
    if (!useSsl && !startTls && !smtpPort) {
      smtpPort = 587;
    }

    try {
      const response = await fetch('/api/push', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          ...pushConfig,
          smtpPort,
          useSsl,
          startTls,
          triggerPercent,
          cycleMinutes,
          reminderCycle,
          reminderWeekday,
          sendHour: time.hour,
          sendMinute: time.minute
        })
      });

      if (!response.ok) {
        showNotice('error', 'Push-Konfiguration konnte nicht gespeichert werden.');
        return false;
      }

      pushConfig = {
        ...pushConfig,
        smtpPort,
        useSsl,
        startTls,
        triggerPercent,
        reminderCycle,
        reminderWeekday,
        sendHour: time.hour,
        sendMinute: time.minute
      };
      showNotice('success', 'Push-Konfiguration gespeichert.');
      pushConfigDirty = false;
      return true;
    } catch (_) {
      showNotice('error', 'Push-Konfiguration konnte nicht gespeichert werden.');
      return false;
    }
  }

  async function sendPushTest() {
    try {
      const saved = await savePushConfig();
      if (!saved) {
        return;
      }
      const response = await fetch('/api/push/test', { method: 'POST' });
      if (!response.ok) {
        const raw = await response.text().catch(() => '');
        let detailedError = '';
        if (raw) {
          try {
            const payload = JSON.parse(raw);
            detailedError = payload?.error || '';
          } catch (_) {
            detailedError = raw;
          }
        }

        const fallback = `Test-E-Mail konnte nicht gesendet werden (HTTP ${response.status}).`;
        showNotice('error', detailedError || fallback);
        return;
      }
      showNotice('success', 'Test-E-Mail wurde versendet.');
    } catch (_) {
      showNotice('error', 'Test-E-Mail konnte nicht gesendet werden.');
    }
  }

  function selectProvider(id) {
    const provider = SMTP_PROVIDERS.find((p) => p.id === id);
    if (!provider) return;
    selectedProvider = id;
    if (id !== 'custom') {
      pushConfig = {
        ...pushConfig,
        smtpServer: provider.host,
        smtpPort: provider.port,
        useSsl: provider.security === 'ssl',
        startTls: provider.security === 'starttls'
      };
      markPushConfigDirty();
    }
  }

  async function runSmtpDiagnostic() {
    smtpDiagLoading = true;
    smtpDiagResult = null;

    if (pushConfigDirty) {
      const saved = await savePushConfig();
      if (!saved) {
        smtpDiagLoading = false;
        return;
      }
    }

    try {
      const response = await fetch('/api/push/smtp-check', { method: 'POST' });
      const data = await response.json().catch(() => null);
      smtpDiagResult = data || { success: false, steps: [{ name: 'HTTP', ok: false, detail: `HTTP ${response.status}` }] };
    } catch (_) {
      smtpDiagResult = { success: false, steps: [{ name: 'Verbindung', ok: false, detail: 'Keine Antwort vom Gerät.' }] };
    } finally {
      smtpDiagLoading = false;
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
  <div class="config-section" on:input={markSensorConfigDirty}>
    <h2>Konfiguration</h2>

    <label class="field-row">
      <span>Offset (cm):</span>
      <input class="sensor-fixed-width" type="number" bind:value={sensorOffsetValue} />
    </label>

    <label class="field-row">
      <span>Behälterhöhe (cm):</span>
      <input class="sensor-fixed-width" type="number" bind:value={sensorHeightValue} />
    </label>

    <label class="field-row field-row--top-align">
      <span><FieldLabel text="Abtastrate Ultraschall:" required={true} /></span>
      <div class="field-control field-control--inline sensor-inline-width">
        <input
          type="number"
          min="1"
          step="1"
          bind:value={sensorIntervalValue}
          on:input={markSensorIntervalDirty}
          aria-required="true"
          aria-invalid={hasSensorIntervalError()}
          class:input-invalid={hasSensorIntervalError()}
          on:blur={touchSensorInterval}
        />
        <select bind:value={sensorIntervalUnit} on:change={markSensorIntervalDirty} on:blur={touchSensorInterval}>
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
  <div class="config-section" on:input={markWifiConfigDirty} on:change={markWifiConfigDirty}>
    <h2>WiFi</h2>
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

    <label class="field-row">
      <span>Gerätename:</span>
      <input bind:value={wifiConfig.deviceName} placeholder="Salzstand" />
    </label>
    <p class="helper-text">Im Netzwerk erreichbar unter {getWifiLocalUrl()}</p>

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
{:else if module === 'push'}
  <div class="config-section" on:input={markPushConfigDirty} on:change={markPushConfigDirty}>
    <h2>Push Nachricht</h2>

    <label class="checkbox-row"><input type="checkbox" bind:checked={pushConfig.enabled} /> Push Benachrichtigung aktivieren</label>

    <label class="field-row">
      <span>Provider:</span>
      <select class="theme-select" value={selectedProvider} on:change={(e) => selectProvider(e.currentTarget.value)}>
        {#each SMTP_PROVIDERS as provider}
          <option value={provider.id}>{provider.label}{provider.certOk ? ' ✓' : ''}</option>
        {/each}
      </select>
    </label>
    {#if selectedProvider !== 'custom'}
      {#each SMTP_PROVIDERS.filter(p => p.id === selectedProvider && p.certNote) as p}
        <p class="helper-text error">{p.certNote}</p>
      {/each}
    {/if}

    <label class="field-row"><span>SMTP-Server:</span><input bind:value={pushConfig.smtpServer} on:input={() => { selectedProvider = detectProvider(pushConfig.smtpServer, pushConfig.smtpPort); }} /></label>
    <label class="field-row"><span>SMTP Port:</span><input type="number" min="1" bind:value={pushConfig.smtpPort} on:input={() => { selectedProvider = detectProvider(pushConfig.smtpServer, pushConfig.smtpPort); }} /></label>

    <label class="field-row field-row--top-align">
      <span>Verschlüsselung:</span>
      <div class="field-control field-control--choice">
        <div class="choice-grid choice-grid--triple" role="radiogroup" aria-label="SMTP-Verschlüsselung">
          <label class="choice-card"><input type="radio" name="push-encryption" checked={getPushEncryptionMode() === 'none'} on:change={() => applyPushEncryptionMode('none')} /><span>Keine</span></label>
          <label class="choice-card"><input type="radio" name="push-encryption" checked={getPushEncryptionMode() === 'ssl'} on:change={() => applyPushEncryptionMode('ssl')} /><span>SSL/TLS</span></label>
          <label class="choice-card"><input type="radio" name="push-encryption" checked={getPushEncryptionMode() === 'starttls'} on:change={() => applyPushEncryptionMode('starttls')} /><span>STARTTLS</span></label>
        </div>
        {#if getPushEncryptionMode() === 'starttls'}
          <p class="helper-text error">STARTTLS wird aktuell vom SMTP-Client noch nicht unterstützt. Für den Versand bitte derzeit SSL/TLS verwenden.</p>
        {/if}
      </div>
    </label>

    {#if getPushEncryptionMode() === 'ssl' || getPushEncryptionMode() === 'starttls'}
      <label class="checkbox-row checkbox-row--warning">
        <input type="checkbox" bind:checked={pushConfig.smtpSkipCertVerify} />
        Zertifikat überspringen (unsicher – nur für Provider ohne hinterlegtes Root-CA)
      </label>
      {#if !pushConfig.smtpSkipCertVerify}
        <p class="helper-text">Zertifikatsprüfung aktiv. Hetzner, IONOS und Strato werden unterstützt. Gmail, GMX, Web.de und Outlook benötigen "Zertifikat überspringen".</p>
      {:else}
        <p class="helper-text error">Zertifikatsprüfung deaktiviert – Verbindung ist anfällig für Man-in-the-Middle-Angriffe.</p>
      {/if}
    {/if}

    <label class="field-row"><span>SMTP-Benutzer:</span><input bind:value={pushConfig.authUser} /></label>
    <div class="field-row">
      <span>SMTP-Passwort:</span>
      <PasswordInput bind:value={pushConfig.authPassword} hasStoredPassword={pushHasAuthPassword} mask={PASSWORD_MASK} />
    </div>

    <div class="diag-section">
      <button class="secondary-sm" type="button" on:click={runSmtpDiagnostic} disabled={smtpDiagLoading}>
        {smtpDiagLoading ? 'Prüfe…' : 'SMTP-Verbindung prüfen'}
      </button>
      {#if smtpDiagResult}
        <div class="diag-result" class:diag-result--ok={smtpDiagResult.success} class:diag-result--fail={!smtpDiagResult.success}>
          <p class="diag-summary">{smtpDiagResult.success ? '✓ Verbindung erfolgreich' : '✗ Verbindung fehlgeschlagen'}</p>
          <ul class="diag-steps">
            {#each smtpDiagResult.steps as step}
              <li class="diag-step" class:diag-step--fail={!step.ok}>
                <span class="diag-icon" aria-hidden="true">{step.ok ? '✓' : '✗'}</span>
                <span class="diag-step-name">{step.name}</span>
                <span class="diag-step-detail">{step.detail}</span>
              </li>
            {/each}
          </ul>
        </div>
      {/if}
    </div>

    <h3>Absender / Empfänger</h3>
    <label class="field-row"><span>Absender Name:</span><input bind:value={pushConfig.senderName} /></label>
    <label class="field-row"><span>Absenderadresse:</span><input type="email" bind:value={pushConfig.senderEmail} /></label>
    <label class="field-row"><span>Empfängeradresse:</span><input type="email" bind:value={pushConfig.recipientEmail} /></label>

    <h3>Auslöser</h3>
    <label class="field-row"><span>Schwellwert &lt; (%):</span><input type="number" min="0" max="100" step="0.1" bind:value={pushConfig.triggerPercent} /></label>

    <h3>Zeitsteuerung</h3>
    <p class="helper-text">Beim Erreichen des Schwellwerts wird sofort gesendet. Danach laufen Erinnerungen nach dem gewählten Zyklus.</p>
    <label class="field-row"><span>Zyklus:</span><select class="theme-select" bind:value={pushConfig.reminderCycle}><option value="day">Tag</option><option value="week">Woche</option><option value="month">Monat</option></select></label>
    <label class="field-row"><span>Uhrzeit:</span><select class="theme-select time-select" value={toTimeString(pushConfig.sendHour, pushConfig.sendMinute)} on:change={(e) => {
      const parsed = parseTimeString(e.currentTarget.value);
      pushConfig = { ...pushConfig, sendHour: parsed.hour, sendMinute: parsed.minute };
    }}>{#each TIME_OPTIONS as option}<option value={option.value}>{option.label}</option>{/each}</select></label>
    <label class="field-row"><span>Tag:</span><select class="theme-select" bind:value={pushConfig.reminderWeekday} disabled={pushConfig.reminderCycle === 'day'}>{#each WEEKDAY_OPTIONS as option}<option value={option.value}>{option.label}</option>{/each}</select></label>
    <p class="helper-text">{getReminderDescription()}</p>

    <h3>Vorlage</h3>
    <label class="field-row"><span>Betreff:</span><input bind:value={pushConfig.subjectTemplate} /></label>
    <label class="field-row field-row--top-align"><span>Body:</span><textarea rows="6" bind:value={pushConfig.bodyTemplate}></textarea></label>
    <p class="helper-text">Platzhalter: <code>{'{level_percent}'}</code>, <code>{'{level_cm}'}</code>, <code>{'{raw_distance_m}'}</code>, <code>{'{timestamp}'}</code>, <code>{'{device_id}'}</code>, <code>{'{ip}'}</code>, <code>{'{ssid}'}</code></p>

    <div class="button-row button-row--push">
      <button class="primary" on:click={savePushConfig}>Speichern</button>
      <button class="secondary-sm" type="button" on:click={sendPushTest}>Test-E-Mail senden</button>
    </div>
  </div>
{:else if module === 'mqtt_ha'}
  <div class="config-section" on:input={markMqttConfigDirty} on:change={markMqttConfigDirty}>
    <h2>MQTT & HA</h2>
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
  .field-control--choice {
    width: min(560px, 100%);
  }
  .field-control--inline {
    display: grid;
    grid-template-columns: 80px minmax(0, 1fr);
    gap: 30px;
    align-items: start;
    width: min(360px, 100%);
  }
  .sensor-fixed-width {
    width: min(250px, 100%);
  }
  .sensor-inline-width {
    width: min(250px, 100%);
    grid-template-columns: 70px 150px;
    gap: 30px;
  }
  .field-control--inline.sensor-inline-width input {
    width: 70px;
  }
  .field-control--inline.sensor-inline-width select {
    width: 150px;
  }
  .field-control--inline input {
    width: 100%;
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
  textarea {
    margin-left: 0;
    width: min(360px, 100%);
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    padding: 8px 10px;
    background: rgba(255, 255, 255, 0.14);
    color: var(--text-main);
    resize: vertical;
    font-family: inherit;
  }
  .field-control input {
    width: 100%;
  }
  .field-control--inline select {
    width: 100px;
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
    padding: 8px 42px 8px 12px;
    background: rgba(255, 255, 255, 0.14);
    color: var(--text-main);
    appearance: none;
    background-image:
      linear-gradient(45deg, transparent 50%, var(--accent) 50%),
      linear-gradient(135deg, var(--accent) 50%, transparent 50%),
      linear-gradient(to right, transparent, transparent);
    background-position:
      calc(100% - 20px) calc(50% - 3px),
      calc(100% - 14px) calc(50% - 3px),
      100% 0;
    background-size: 6px 6px, 6px 6px, 2.5em 100%;
    background-repeat: no-repeat;
  }
  .theme-select {
    background-color: var(--surface-2);
    color: var(--text-main);
    border: 1px solid var(--surface-border);
  }
  .theme-select:focus {
    outline: none;
    border-color: var(--accent);
    box-shadow: 0 0 0 2px rgba(98, 184, 221, 0.2);
  }
  :global(html[data-theme='night']) .theme-select {
    background-color: rgba(10, 24, 48, 0.96);
    color: #e8f0ff;
  }
  :global(html[data-theme='day']) .theme-select {
    background-color: rgba(242, 255, 247, 0.96);
    color: #163326;
  }
  .theme-select option {
    color: inherit;
  }
  select option {
    background-color: #0d1b33;
    color: #e8f0ff;
  }
  :global(html[data-theme='night']) select {
    background-color: rgba(10, 24, 48, 0.92);
    color: #e8f0ff;
  }
  :global(html[data-theme='day']) select option {
    background-color: #f2fff7;
    color: #183326;
  }
  :global(html[data-theme='day']) select {
    background-color: rgba(242, 255, 247, 0.92);
    color: #163326;
  }
  .input-action-row {
    display: grid;
    grid-template-columns: minmax(0, 360px) auto;
    justify-content: start;
    gap: 30px;
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
  .choice-grid--triple {
    grid-template-columns: repeat(3, minmax(0, 1fr));
    margin-top: 0;
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
  .button-row {
    display: flex;
    gap: 10px;
    align-items: center;
    flex-wrap: wrap;
    margin-top: 10px;
  }
  .button-row--push {
    gap: 400px;
    flex-wrap: nowrap;
  }
  .checkbox-row--warning {
    color: var(--text-muted);
    font-weight: normal;
  }
  .diag-section {
    margin: 14px 0 4px;
  }
  .diag-result {
    margin-top: 10px;
    padding: 10px 14px;
    border-radius: 10px;
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.05);
  }
  .diag-result--ok {
    border-color: #4caf7d;
    background: rgba(76, 175, 80, 0.08);
  }
  .diag-result--fail {
    border-color: #e05252;
    background: rgba(224, 82, 82, 0.08);
  }
  .diag-summary {
    font-weight: 600;
    margin: 0 0 6px;
  }
  .diag-result--ok .diag-summary { color: #4caf7d; }
  .diag-result--fail .diag-summary { color: #e05252; }
  .diag-steps {
    list-style: none;
    padding: 0;
    margin: 0;
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  .diag-step {
    display: grid;
    grid-template-columns: 18px 90px 1fr;
    gap: 6px;
    align-items: start;
    font-size: 0.88em;
    color: var(--text-muted);
  }
  .diag-step--fail .diag-icon { color: #e05252; }
  .diag-step:not(.diag-step--fail) .diag-icon { color: #4caf7d; }
  .diag-step-name {
    font-weight: 600;
    color: var(--text-main);
  }
  .diag-step-detail {
    word-break: break-word;
  }
  .time-select {
    width: min(180px, 100%);
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
    .input-action-row {
      grid-template-columns: minmax(0, 1fr) auto;
      gap: 12px;
    }

    .field-control--inline {
      grid-template-columns: 80px 1fr;
    }
    .choice-grid {
      grid-template-columns: 1fr;
    }
    .choice-grid--triple {
      grid-template-columns: 1fr;
    }
    .button-row--push {
      gap: 12px;
      flex-wrap: wrap;
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