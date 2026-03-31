# Salzstand API Übersicht

Diese Datei listet alle HTTP-API-Endpunkte des Projekts (Backend/Frontend) und wird bei Änderungen aktualisiert.

## Konfiguration & System
- `GET /api/config` – Sensor-Konfiguration lesen
- `POST /api/config` – Sensor-Konfiguration speichern
- `GET /api/wifi` – WiFi-Konfiguration lesen
- `POST /api/wifi` – WiFi-Konfiguration speichern
- `POST /api/wifi/scan` – WiFi-Netzwerke scannen
- `GET /api/mqtt` – MQTT-Konfiguration lesen
- `POST /api/mqtt` – MQTT-Konfiguration speichern
- `GET /api/mqtt/status` – MQTT-Status abfragen
- `POST /api/mqtt/reconnect` – MQTT neu verbinden
- `GET /api/push` – Push-/SMTP-Konfiguration lesen
- `POST /api/push` – Push-/SMTP-Konfiguration speichern
- `POST /api/push/test` – Test-E-Mail versenden
- `POST /api/push/smtp-check` – SMTP-Diagnose
- `GET /api/nvs` – Gesamte NVS-Konfiguration (Passwörter maskiert)
- `POST /api/restart` – ESP32 neu starten
- `POST /api/factory-reset` – Werksreset

## Update/OTA
- `GET /api/update/status` – OTA-Status
- `GET /api/update/manifest` – OTA-Manifest
- `POST /api/update/repo` – OTA aus GitHub Release
- `POST /api/update/upload/app` – Firmware lokal hochladen
- `POST /api/update/upload/webui` – Web-UI lokal hochladen

## Daten
- `GET /api/history` – Messverlauf lesen
- `DELETE /api/history` – Messverlauf löschen
- `GET /api/export` – Backup exportieren
- `POST /api/import` – Backup importieren

## Setup-Modus (nur Access Point/Ersteinrichtung)
- `POST /scan` – WiFi-Scan
- `POST /save` – Setup speichern

## WebSocket
- `/ws` – Live-Daten (Sensor, WiFi, MQTT, Uptime)

---
*Diese Datei wird automatisch gepflegt. Änderungen an API-Endpunkten bitte hier nachziehen!*
