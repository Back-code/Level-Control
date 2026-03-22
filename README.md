# Salzstand – ESP32-C3 Ultraschall-Füllstandssensor

Dieses Projekt implementiert einen WLAN-fähigen Füllstandssensor für Salzbehälter auf Basis eines ESP32-C3 mit HC-SR04-Ultraschallsensor. Die Messwerte werden über eine Svelte-Web-UI dargestellt, per WebSocket in Echtzeit übertragen und über MQTT (inkl. Home Assistant Auto-Discovery) bereitgestellt.

## About

Salzstand ist ein ESP32-C3-Projekt zur kontinuierlichen Füllstandsmessung von Salzbehältern mit Ultraschall. Es kombiniert Firmware, Web-Dashboard (Svelte), MQTT-Telemetrie und Home-Assistant-Discovery in einem System.

**Highlights:**
- Live-Dashboard mit Tag/Nacht-Theme und Modulnavigation
- MQTT mit Sensor-, System- und Konfigurations-Topics
- Home Assistant Auto-Discovery (Sensoren + Konfig-Entitäten)
- Persistente Konfiguration in NVS (WiFi, MQTT, Behälterhöhe, Offset)

**Tech-Stack:** ESP32-C3, Arduino, PlatformIO, Svelte, PubSubClient, ESPAsyncWebServer, ArduinoJson

---

## Hardware

| Komponente | Details |
|---|---|
| Mikrocontroller | ESP32-C3 DevKitM-1, 160 MHz, 320 KB RAM, 4 MB Flash |
| Sensor | HC-SR04 Ultraschall-Abstandssensor |
| Trigger-Pin | GPIO 4 |
| Echo-Pin | GPIO 5 |

---

## Features

### Sensor-Messung
- Eigene `ping()`-Implementierung über `pulseIn()` (keine externe Bibliothek)
- Median aus 10 aufeinanderfolgenden Messungen zur Rauschunterdrückung
- Gültigkeitsbereich: 0–25 000 µs Ping-Zeit
- Alle Messwerte werden in jedem `loop()`-Durchlauf aktualisiert
- 5 Testmessungen beim Start mit Serial-Ausgabe

### Messwerte
| Wert | Berechnung |
|---|---|
| Rohdistanz (m) | Direkt aus `ping_us / 2 / 29.1 / 100` |
| Salzstand (cm) | `(behaelterhoehe + offset) - gemessene_cm` |
| Füllstand (%) | `salzstand_cm / behaelterhoehe * 100` |
| Ping-Zeit (µs) | Roher `pulseIn`-Wert |
| Messung gültig | `true` wenn Ping-Zeit im Gültigkeitsbereich |

---

## Konfiguration (NVS)

Alle Einstellungen werden persistent im NVS-Flash gespeichert (Namespace `config`, JSON-Format).

| Feld | Typ | Beschreibung |
|---|---|---|
| `wifi.ssid` | String | WLAN-Netzwerkname |
| `wifi.password` | String | WLAN-Passwort |
| `staticIp.ip` | String | Statische IP (leer = DHCP) |
| `staticIp.gateway` | String | Gateway |
| `staticIp.subnet` | String | Subnetzmaske |
| `staticIp.dns` | String | DNS-Server |
| `mqtt.server` | String | MQTT-Broker-Adresse |
| `mqtt.port` | uint16 | MQTT-Port (Standard: 1883) |
| `mqtt.user` | String | MQTT-Benutzername |
| `mqtt.password` | String | MQTT-Passwort |
| `mqtt.discovery` | bool | Home Assistant Auto-Discovery aktivieren |
| `behaelterhoehe` | float | Innenhöhe des Behälters in cm |
| `offset` | float | Korrekturwert in cm (für Sensorposition) |

---

## Boot-Verhalten

```
Erster Start / keine SSID konfiguriert  ODER  WLAN-Verbindung schlägt fehl
  → SETUP_MODE: Access Point "Salzstand-Setup" (kein Passwort)
  → Webseite auf 192.168.4.1: WiFi-Konfigurationsformular

Normaler Betrieb
  → NORMAL_MODE: Verbindet mit WLAN, startet Dashboard + MQTT
```

---

## Web-UI (Svelte)

Die UI ist als Svelte 4-Anwendung gebaut (Vite 5), wird aus LittleFS ausgeliefert und kommuniziert über WebSocket (Echtzeit) sowie REST-API (Konfiguration).

### Tab: Dashboard

**Anzeigefelder:**

| Karte | Inhalt |
|---|---|
| Aktuelle Distanz | Rohdistanz in m (Ultraschall-Messwert) |
| Salzstand | Füllstand in cm und Prozent |
| WiFi-Signal | RSSI in dBm |
| Uptime | Betriebszeit in Stunden und Minuten |
| Netzwerk | IP-Adresse, SSID, BSSID |

**Schnelleinstellungen:**
- Behälterhöhe (cm) und Offset (cm) direkt anpassbar
- Speichern via `POST /api/config`

**Weitere Aktionen:**
- `Neustart`-Button → `POST /api/restart`
- `Ctrl+D` → öffnet/schließt das Debug-Overlay

### Tab: Konfiguration

Unterteilt in 4 Bereiche:

**Sensor:**
- Behälterhöhe (cm) einstellen
- Offset (cm) einstellen
- Speichern via `POST /api/config`

**WiFi:**
- SSID und Passwort
- Optionale statische IP-Konfiguration (IP, Gateway, Subnetz, DNS)
- Speichern via `POST /api/wifi` (Neustart erforderlich)

**MQTT:**
- Server-Adresse, Port, Benutzer, Passwort
- Home Assistant Discovery aktivieren/deaktivieren
- Speichern via `POST /api/mqtt` (löst sofortigen Reconnect aus)

**Home Assistant:**
- Informationsseite mit Hinweis zur Discovery-Konfiguration
- Auflistung der veröffentlichten Discovery-Topics

### Debug-Overlay (`Ctrl+D`)

- Rechts eingeblendetes Panel (400 px breit, dunkler Hintergrund)
- Zeigt die letzten 50 Log-Nachrichten in Echtzeit (via WebSocket-Typ `log`)
- Log-Level-Färbung: `ERROR` rot, `WARN` orange, `INFO` weiß, `DEBUG` grau
- Format: `Timestamp [LEVEL] Nachricht`
- Ruft beim Öffnen `GET /api/nvs` ab (NVS-Rohdaten)

---

## REST-API

Alle Endpunkte laufen auf Port 80.

| Methode | Endpoint | Beschreibung |
|---|---|---|
| `GET` | `/api/config` | Sensor-Konfiguration lesen (`behaelterhoehe`, `offset`) |
| `POST` | `/api/config` | Sensor-Konfiguration speichern |
| `GET` | `/api/wifi` | WiFi-Konfiguration lesen (Passwort wird nicht zurückgegeben) |
| `POST` | `/api/wifi` | WiFi-Konfiguration speichern |
| `GET` | `/api/mqtt` | MQTT-Konfiguration lesen (Passwort als `***` maskiert) |
| `POST` | `/api/mqtt` | MQTT-Konfiguration speichern + sofortiger Reconnect |
| `GET` | `/api/nvs` | Gesamte NVS-Konfiguration als JSON (Passwörter maskiert) |
| `POST` | `/api/restart` | ESP32 neu starten |
| `GET` | `/*` | Statische Dateien aus LittleFS (Svelte-UI) |

---

## WebSocket (`/ws`)

Broadcast-Intervalle aus dem `loop()`:

| Typ | Intervall | Felder |
|---|---|---|
| `sensor` | 5 s | `ping_us`, `valid`, `rohdistanz`, `salzstandCm`, `salzstandPercent` |
| `wifi` | 5 s | `signal` (dBm), `ip`, `ssid`, `bssid` |
| `uptime` | 5 s | `uptime` (Sekunden) |
| `log` | bei jedem Log-Eintrag | `level`, `timestamp`, `message` |

---

## MQTT

Broker-Verbindung über PubSubClient 2.8.  
Reconnect-Versuch alle 5 Sekunden bei Verbindungsverlust.  
Keepalive: 30 Sekunden, Buffer: 1024 Bytes.  
Geräte-ID: `salzstand_` + MAC-Adresse ohne Doppelpunkte (z. B. `salzstand_A4CF121E3B00`).

### Topics

| Topic | Richtung | Retain | Beschreibung |
|---|---|---|---|
| `salzstand/status` | pub | ✓ | `online` / `offline` (LWT) |
| `salzstand/sensor/state` | pub | ✓ | Alle Messwerte als JSON (alle 30 s) |
| `salzstand/config/behaelterhoehe/state` | pub | ✓ | Aktuelle Behälterhöhe in cm |
| `salzstand/config/behaelterhoehe/set` | **sub** | – | Neue Behälterhöhe setzen (1–1000 cm) |
| `salzstand/config/offset/state` | pub | ✓ | Aktueller Offset-Wert |
| `salzstand/config/offset/set` | **sub** | – | Neuen Offset setzen (–500 bis +500 cm) |
| `salzstand/system/state` | pub | ✓ | WiFi, Uptime & ESP32-Systemdaten (alle 30 s) |

### Sensor-State-Payload (`salzstand/sensor/state`)

```json
{
  "fill_level": 73.5,
  "distance_cm": 69.8,
  "raw_distance_m": 0.2520,
  "ping_us": 1468,
  "valid": true,
  "status": "ok"
}
```

**`status`-Werte:**

| Wert | Bedeutung |
|---|---|
| `ok` | Messung gültig |
| `timeout` | Kein Echo empfangen (Ping-Zeit = 0) |
| `out_of_range` | Echo außerhalb des Gültigkeitsbereichs |

### System-State-Payload (`salzstand/system/state`)

```json
{
  "ip": "192.168.1.100",
  "ssid": "MyNetwork",
  "rssi": -65,
  "uptime_s": 3600,
  "free_heap": 245000,
  "min_free_heap": 220000,
  "cpu_freq_mhz": 160,
  "flash_size_kb": 4096,
  "sketch_size_kb": 980,
  "chip_rev": 3
}
```

### Eingehende Befehle

Über `/set`-Topics können Konfigurationswerte live geändert werden:
- Wert wird validiert (Bereich geprüft)
- Im NVS gespeichert
- Im laufenden `SensorManager` sofort übernommen
- Neuer Wert sofort auf dem zugehörigen `/state`-Topic veröffentlicht

---

## Home Assistant Auto-Discovery

Wenn `mqtt.discovery = true`, werden beim Connect 6 Entitäten veröffentlicht:

| HA-Entität | Domain | Object-ID | Einheit | Kategorie |
|---|---|---|---|---|
| Füllstand | `sensor` | `fill_level` | % | – |
| Salzstand | `sensor` | `distance_cm` | cm | – |
| Rohdistanz | `sensor` | `raw_distance` | m | diagnostic |
| Ultraschall Pingzeit | `sensor` | `ping_us` | µs | diagnostic |
| Behälterhöhe | `number` | `behaelterhoehe` | cm | config |
| Sensor Offset | `number` | `offset` | cm | config |
| WiFi Signal | `sensor` | `rssi` | dBm | diagnostic |
| IP-Adresse | `sensor` | `ip_address` | – | diagnostic |
| SSID | `sensor` | `ssid` | – | diagnostic |
| Betriebszeit | `sensor` | `uptime` | s | diagnostic |
| Freier Heap | `sensor` | `free_heap` | B | diagnostic |
| CPU-Frequenz | `sensor` | `cpu_freq` | MHz | diagnostic |

Jede Entität enthält:
- `unique_id` (Geräte-ID + Feldname) für stabile HA-Identifikation
- `availability_topic` → `salzstand/status`
- Device-Block: Name `Salzstand`, Manufacturer `DIY`, Model `ESP32-C3`

Discovery-Topic-Schema:
```
homeassistant/{domain}/{deviceId}/{objectId}/config
```

---

## Architektur

```
main.cpp
 ├── SystemStateManager   → Bestimmt Boot-Modus (SETUP / NORMAL / FALLBACK)
 ├── WifiManager          → Verbindungsverwaltung, exponentielle Backoff-Wiederverbindung
 ├── WebServerDashboard   → REST-API, WebSocket, LittleFS-Dateiserving
 ├── WebServerSetup       → Captive-Portal-artige WiFi-Erstkonfiguration (AP-Modus)
 ├── SensorManager        → Ultraschall-Messung, Median-Filter, Kalkulation
 ├── MqttManager          → PubSubClient-Wrapper, Discovery, Subscribe/Publish
 ├── ConfigStore          → NVS-Persistenz (ArduinoJson, Preferences)
 ├── EventBus             → Pub/Sub-System für interne Ereignisse
 └── DebugLogger          → Log-Weiterleitung an Serial + WebSocket
```

**Singleton-Muster** für alle Manager — Zugriff immer via `XManager::getInstance()`.

**EventBus-Ereignisse:**

| EventType | Ausgelöst von | Beschreibung |
|---|---|---|
| `WIFI_CONNECTED` | WifiManager | WLAN-Verbindung hergestellt |
| `WIFI_DISCONNECTED` | WifiManager | WLAN-Verbindung getrennt |
| `SYSTEM_MQTT_CONNECTED` | MqttManager | MQTT-Verbindung hergestellt |
| `SYSTEM_MQTT_DISCONNECTED` | MqttManager | MQTT-Verbindung getrennt |
| `SENSOR_TIMEOUT` | SensorManager | Sensor antwortet nicht |
| `SENSOR_OUT_OF_RANGE` | SensorManager | Messwert außerhalb Bereich |
| `CONFIG_SAVED` | ConfigStore | Konfiguration gespeichert |

---

## Build & Flash

### Voraussetzungen
- [PlatformIO](https://platformio.org/) (VS Code Extension oder CLI)
- [Node.js](https://nodejs.org/) ≥ 18 für die UI

### Abhängigkeiten (platformio.ini)

| Bibliothek | Version |
|---|---|
| ESP32Async/ESPAsyncWebServer | ^3.6.0 |
| ESP32Async/AsyncTCP | ^3.3.2 |
| bblanchon/ArduinoJson | ^6.21.2 |
| knolleary/PubSubClient | ^2.8 |

### UI bauen

```bash
cd ui
npm install
npm run build
```

Die gebauten Dateien werden per `extra_scripts` automatisch nach `data/` geschrieben.

### Firmware kompilieren und flashen

```bash
# Dateisystem-Image erstellen und flashen
pio run --target uploadfs

# Firmware kompilieren und flashen
pio run --target upload
```

Hinweis zur Speicheraufteilung auf ESP32-C3:
- Das Projekt nutzt eine benutzerdefinierte Partitionstabelle in `partitions.csv`.
- Ergebnis: deutlich größere OTA-App-Slots (je `0x1A0000`) und weiterhin ausreichend LittleFS (`0x0B0000`).

### Seriell-Monitor

```bash
pio device monitor --baud 115200
```

---

## Erstkonfiguration

1. ESP32 einschalten → Access Point **"Salzstand-Setup"** erscheint
2. Mit dem AP verbinden (kein Passwort)
3. Browser öffnen: `http://192.168.4.1`
4. SSID und WLAN-Passwort eintragen → Speichern
5. ESP32 startet neu und verbindet sich mit dem WLAN
6. IP-Adresse aus dem Router-DHCP ermitteln (oder im Serial-Monitor ablesen)
7. Dashboard unter `http://<IP>/` aufrufen
8. Unter **Konfiguration → Sensor**: Behälterhöhe eintragen und ggf. Offset anpassen
9. Unter **Konfiguration → MQTT**: Broker, Port, Zugangsdaten eingeben

---

## Technische Kenndaten

| Eigenschaft | Wert |
|---|---|
| Flash-Nutzung | ~74,8 % (4 MB) |
| RAM-Nutzung | ~12,3 % (320 KB) |
| Sensor-Messintervall | kontinuierlich (jeder loop-Durchlauf) |
| MQTT-Publish-Intervall | 30 Sekunden |
| WebSocket-Broadcast | 5 Sekunden |
| MQTT-Reconnect-Prüfung | 5 Sekunden |
| Serial-Diagnose | 10 Sekunden |
| MQTT Keepalive | 30 Sekunden |
| MQTT Buffer | 1024 Bytes |
