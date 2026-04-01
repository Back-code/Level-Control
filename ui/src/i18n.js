export const TRANSLATIONS = {
  de: {
    appTitle: 'Stand Control',
    appTagline: 'Smart Reservoir Monitor',
    updateAvailableTitle: 'Update verfügbar',
    restart: 'Neustart',
    day: 'Tag',
    night: 'Nacht',
    moreModules: 'Weitere Module',
    more: 'Mehr',
    language: 'Sprache',
    modules: {
      dashboard: { label: 'Dashboard', subtitle: 'Live-Messwerte und Netzwerkstatus in einer kompakten Übersicht.' },
      sensor: { label: 'Konfiguration', subtitle: 'Behälterparameter und Sensor-Abtastrate zentral konfigurieren.' },
      wifi: { label: 'WiFi', subtitle: 'WLAN-Zugang und optionale statische Netzwerkdaten verwalten.' },
      mqtt_ha: { label: 'MQTT & HA', subtitle: 'Broker, Discovery und Home-Assistant-Anbindung zentral steuern.' },
      push: { label: 'Push Nachricht', subtitle: 'E-Mail Benachrichtigungen bei Schwellwert, Zeit und Zyklus konfigurieren.' },
      update: { label: 'Update', subtitle: 'OTA aus Releases oder lokales BIN-Upload mit Dateikontrolle durchführen.' },
      backup: { label: 'Sicherung', subtitle: 'Konfiguration und Messverlauf exportieren und importieren.' }
    },
    restartDialogTitle: 'ESP neu starten?',
    restartDialogMessage: 'Das Gerät wird sofort neu gestartet. Die Weboberfläche ist für kurze Zeit nicht erreichbar.',
    restartDialogConfirm: 'Jetzt neu starten',
    cancel: 'Abbrechen',
    restartFailed: 'Neustart konnte nicht ausgelöst werden.',
    restartSuccess: 'Neustart wurde ausgelöst. Das Gerät ist gleich kurz nicht erreichbar.',
    dashboard: {
      historyTitle: 'Stand Verlauf',
      resetHistory: 'Verlauf löschen',
      distance: 'Aktuelle Distanz',
      level: 'Stand',
      chartAria: 'Verlauf des Standes in Prozent',
      disconnected: 'Verbindung getrennt - Werte werden nicht aktualisiert',
      stale: 'Wert möglicherweise veraltet',
      period: 'Zeitraum',
      recordStart: 'Start der Aufzeichnung',
      lastMeasurement: 'Letzte Messung',
      noHistory: 'Noch keine Historie',
      system: 'System',
      network: 'Netzwerk',
      wifiSignal: 'WiFi Signal',
      uptime: 'Uptime',
      qualityGood: 'Gut',
      qualityMedium: 'Mittel',
      qualityBad: 'Schlecht'
    },
    config: {
      sensorTitle: 'Konfiguration',
      hardware: 'Hardware-Ausführung:',
      laserVersion: 'Laser-Version:',
      offset: 'Offset (cm):',
      height: 'Behälterhöhe (cm):',
      sampleRate: 'Abtastrate:',
      minRateHint: 'Kleinster zulässiger Wert: 5 Sekunden.',
      save: 'Speichern',
      deviceNamePlaceholder: 'Stand',
      pushTitle: 'Push Nachricht',
      sketchNote: 'Offset verschiebt die Referenz der Sensorposition. Positive Werte vergrößern, negative Werte verkleinern den berechneten Füllstand.'
    }
  },
  en: {
    appTitle: 'Level Control',
    appTagline: 'Smart Reservoir Monitor',
    updateAvailableTitle: 'Update available',
    restart: 'Restart',
    day: 'Day',
    night: 'Night',
    moreModules: 'More modules',
    more: 'More',
    language: 'Language',
    modules: {
      dashboard: { label: 'Dashboard', subtitle: 'Live telemetry and network status in one compact overview.' },
      sensor: { label: 'Configuration', subtitle: 'Configure container parameters and sensor interval in one place.' },
      wifi: { label: 'WiFi', subtitle: 'Manage WiFi access and optional static network settings.' },
      mqtt_ha: { label: 'MQTT & HA', subtitle: 'Control broker, discovery and Home Assistant integration.' },
      push: { label: 'Push Message', subtitle: 'Configure email notifications by threshold, schedule and cycle.' },
      update: { label: 'Update', subtitle: 'Run OTA from releases or local BIN uploads with validation.' },
      backup: { label: 'Backup', subtitle: 'Export and import configuration and measurement history.' }
    },
    restartDialogTitle: 'Restart ESP?',
    restartDialogMessage: 'The device restarts immediately. The web UI will be unavailable for a short moment.',
    restartDialogConfirm: 'Restart now',
    cancel: 'Cancel',
    restartFailed: 'Could not trigger restart.',
    restartSuccess: 'Restart triggered. Device will be unavailable briefly.',
    dashboard: {
      historyTitle: 'Level History',
      resetHistory: 'Clear history',
      distance: 'Current Distance',
      level: 'Level',
      chartAria: 'Level history in percent',
      disconnected: 'Connection lost - values are not updating',
      stale: 'Value might be stale',
      period: 'Period',
      recordStart: 'Recording start',
      lastMeasurement: 'Last measurement',
      noHistory: 'No history yet',
      system: 'System',
      network: 'Network',
      wifiSignal: 'WiFi Signal',
      uptime: 'Uptime',
      qualityGood: 'Good',
      qualityMedium: 'Medium',
      qualityBad: 'Poor'
    },
    config: {
      sensorTitle: 'Configuration',
      hardware: 'Hardware type:',
      laserVersion: 'Laser version:',
      offset: 'Offset (cm):',
      height: 'Container height (cm):',
      sampleRate: 'Sampling interval:',
      minRateHint: 'Minimum allowed value: 5 seconds.',
      save: 'Save',
      deviceNamePlaceholder: 'Stand',
      pushTitle: 'Push Message',
      sketchNote: 'Offset shifts the reference at sensor position. Positive values increase and negative values reduce the calculated level.'
    }
  }
};

export const SUPPORTED_LANGS = [
  { value: 'de', label: 'Deutsch' },
  { value: 'en', label: 'English' }
];

export function createTranslator(lang) {
  const dictionary = TRANSLATIONS[lang] || TRANSLATIONS.de;
  return function t(path, fallback = '') {
    const value = path.split('.').reduce((acc, segment) => (acc && acc[segment] !== undefined ? acc[segment] : undefined), dictionary);
    return value === undefined ? fallback : value;
  };
}
