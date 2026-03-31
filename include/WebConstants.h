
#pragma once
#include <cstdint>

// Zentrale Konstanten und Fehlertexte für Webserver und UI
namespace WebConstants {
    // Beispiel-Konstanten
    // Fehlertexte
    constexpr const char* ERROR_WIFI_FAILED = "WLAN-Verbindung fehlgeschlagen";
    constexpr const char* ERROR_OTA_FAILED = "OTA-Update fehlgeschlagen";
    constexpr const char* ERROR_CONFIG_NVS = "Konfiguration konnte nicht aus NVS geladen werden";
    constexpr const char* ERROR_HISTORY_NVS = "Historie konnte nicht aus NVS geladen werden";
    constexpr const char* ERROR_OTA_SIGNATURE = "OTA-Signaturprüfung fehlgeschlagen";
    constexpr const char* ERROR_SMTP_SEND = "SMTP-Versand fehlgeschlagen";
    constexpr const char* ERROR_WIFI_SCAN = "WiFi-Scan fehlgeschlagen";
    constexpr const char* ERROR_FACTORY_RESET = "Werksreset fehlgeschlagen";
    constexpr const char* WARN_LEVEL_REACHED = "Salzstand Control Warnung: Stand hat {level_percent}% erreicht. Salz nachfüllen!";

    // Status-Strings
    constexpr const char* STATUS_OK = "ok";

    // NVS-Namen und Partitionen
    constexpr const char* NVS_NAMESPACE_CONFIG = "config";
    constexpr const char* NVS_NAMESPACE_HISTORY = "history";
    constexpr const char* NVS_PARTITION_HISTORY = "histnvs";
    constexpr const char* NVS_KEY_HISTORY = "ring";

    // Dateipfade
    constexpr const char* LEGACY_HISTORY_PATH = "/history.bin";

    // Intervall- und Timing-Konstanten
    constexpr uint16_t HISTORY_CAPACITY = 1460;
    constexpr uint32_t HISTORY_SAMPLE_INTERVAL_SEC = 6UL * 3600UL;   // 6 Stunden
    constexpr uint32_t HISTORY_RETENTION_SEC = 365UL * 24UL * 3600UL; // 12 Monate
    constexpr unsigned long MQTT_CONNECT_RETRY_INTERVAL_MS = 5000;
    constexpr unsigned long WIFI_RECONNECT_BACKOFF_MIN_MS = 2000;
    constexpr unsigned long WIFI_RECONNECT_BACKOFF_MAX_MS = 60000;
}
