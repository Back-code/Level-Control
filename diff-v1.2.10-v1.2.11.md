# Vergleich v1.2.10 -> v1.2.11

## Basis

- Vergleich der Git-Tags `v1.2.10` -> `v1.2.11`
- Relevante Commits in diesem Bereich:
  - `e3766d0` - inhaltliche Änderungen
  - `1afb7ab` - Versionssprung auf `v1.2.11`

## Kurzfazit

Zwischen `v1.2.10` und `v1.2.11` wurde das OTA-/Update-System grundlegend umgebaut.
Der Schwerpunkt lag auf einem Wechsel von einem signierten `manifest.json` auf direkt in die Binärdateien eingebettete Signaturen für App und Web-UI.

Betroffene Dateien laut Git-Diff:

- `scripts/prepare-release.js`
- `scripts/run-release.ps1`
- `src/web/WebServerDashboard.cpp`
- `src/web/WebServerDashboard.h`
- `ui/src/Update.svelte`
- `data/assets/index.js`
- `version.json`

Diff-Statistik:

- 7 Dateien geändert
- 276 Einfügungen
- 342 Löschungen

## Inhaltliche Änderungen im Detail

### 1. Release-Erstellung wurde umgestellt

Datei: `scripts/prepare-release.js`

Vorher:

- Es wurde ein `manifest.json` erzeugt.
- Dieses Manifest enthielt Versionsdaten, Asset-URLs, SHA256-Werte und eine separate Signatur.

Nachher:

- `manifest.json` wird nicht mehr erzeugt.
- Stattdessen bekommen `app.bin` und `web-ui.bin` beim Release-Bau direkt einen eingebetteten Signatur-Trailer.
- Der Trailer verwendet die Magic-Bytes `LCSIGV1!`.
- Die Signatur wird direkt über den Binärinhalt erzeugt.
- `SHA256SUMS.txt` bleibt erhalten.
- Die Release-Notes wurden entsprechend angepasst.

Konkrete Auswirkung:

- Das gesamte Vertrauensmodell für OTA wurde geändert.
- Nicht mehr ein externes Manifest bestätigt die Datei, sondern die Datei bestätigt sich selbst über den eingebetteten Signatur-Trailer.

### 2. GitHub-Release lädt kein `manifest.json` mehr hoch

Datei: `scripts/run-release.ps1`

Änderung:

- `manifest.json` wurde aus den GitHub-Release-Assets entfernt.

Konkrete Auswirkung:

- Geräte oder UI-Logik, die noch ein Manifest erwarten, funktionieren mit dem alten Ablauf nicht mehr.

### 3. Update-Backend auf GitHub Release API statt Manifest-Download umgestellt

Datei: `src/web/WebServerDashboard.cpp`

Vorher:

- Es wurde `manifest.json` von GitHub geladen.
- URL vorher: `releases/latest/download/manifest.json`

Nachher:

- Es wird die GitHub Release API abgefragt.
- Neue URL: `https://api.github.com/repos/Back-code/Level-Control/releases/latest`
- Die Firmware extrahiert App/Web-UI-Assets direkt aus dem JSON der Release-API.

Konkrete Auswirkung:

- Der komplette Update-Datenfluss wurde geändert.
- Fehlerquellen verschieben sich von Manifest-Struktur/Signatur auf API-Antwort, Asset-Erkennung und Trailer-Prüfung.

### 4. Offline-Manifest-Import wurde entfernt

Datei: `src/web/WebServerDashboard.cpp`

Entfernt wurde unter anderem:

- Route für lokalen Manifest-Upload: `/api/update/manifest/local`
- lokales Zwischenspeichern eines geprüften Offline-Manifests
- Auswahllogik, die zuerst lokales Manifest und sonst Cache verwendete

Konkrete Auswirkung:

- Der bisherige Offline-OTA-Weg mit lokalem Manifest ist in `v1.2.11` nicht mehr vorhanden.

### 5. Manifest-Signaturprüfung wurde komplett entfernt und durch Datei-Signaturprüfung ersetzt

Dateien:

- `src/web/WebServerDashboard.cpp`
- `src/web/WebServerDashboard.h`

Entfernt:

- Manifest-Felder `sha256`, `signatureAlgorithm`, `signatureValue`
- Aufbau eines Signier-Payloads für Manifestdaten
- `verifyManifestSignature(...)`

Neu:

- `verifyDetachedFileSignature(...)`
- Verifikation direkt gegen den aus der Datei extrahierten Trailer
- neue Konstanten für Trailer-Größe und Signaturformat

Konkrete Auswirkung:

- Die Integrität wird jetzt vollständig pro Asset geprüft.
- Das ist architektonisch ein tiefer Eingriff in die OTA- und Upload-Logik.

### 6. Remote-OTA-Update schreibt jetzt die Datei ohne Trailer in die Zielpartition und prüft danach die Signatur

Datei: `src/web/WebServerDashboard.cpp`

Neu in der Download-/Flash-Logik:

- Download berücksichtigt die letzten Trailer-Bytes separat.
- Die letzten Bytes werden nicht mit in die App- oder LittleFS-Partition geschrieben.
- Stattdessen werden sie gepuffert.
- Über den tatsächlichen Payload wird SHA256 berechnet.
- Danach wird die Signatur aus dem Trailer validiert.

Neue Prüfungen:

- Asset zu klein oder ohne Trailer
- ungültige Magic-Bytes im Trailer
- ungültige Signaturlänge
- App/Web-UI erkennt weiterhin falschen Dateityp
- SHA256-Kontextfehler und Update-Write-Fehler werden expliziter behandelt

Konkrete Auswirkung:

- Dieser Bereich ist hoch relevant, weil hier das OTA-Verhalten fundamental geändert wurde.
- Fehler in Trailer-Handling, Größenberechnung oder Partition-Write würden direkt das Update-Verhalten beeinflussen.

### 7. Lokaler Upload von App/Web-UI wurde ebenfalls auf Trailer-Signaturen umgebaut

Datei: `src/web/WebServerDashboard.cpp`

Vorher:

- Upload erwartete ein geprüftes Manifest.
- Daraus wurde die erwartete SHA256 geholt.
- Die hochgeladene Datei wurde gegen die Manifest-SHA geprüft.

Nachher:

- Upload nutzt kein Manifest mehr.
- Der Upload puffert das Dateiende.
- Die letzten Trailer-Bytes werden separat validiert.
- Die Signatur wird direkt aus dem Trailer geprüft.

Entfernt wurde:

- `uploadExpectedSha_`
- Manifest-Auflösung vor Upload

Neu hinzugefügt wurde:

- `uploadTailBuffer_`

Konkrete Auswirkung:

- Auch der manuelle lokale Upload-Weg wurde vollständig geändert.
- Wenn es Probleme nur ab `v1.2.11` gibt, ist dieser Umbau einer der wahrscheinlichsten Kandidaten.

### 8. Update-Status im Frontend wurde vereinfacht

Datei: `ui/src/Update.svelte`

Entfernt wurden UI-Zustände für:

- `localManifestLoaded`
- `localManifestVersion`
- `localManifestHasApp`
- `localManifestHasWebui`
- Label-Funktion für lokale Manifest-Ziele

Konkrete Auswirkung:

- Die UI wurde an das neue Modell ohne Offline-Manifest angepasst.
- Die generierte Build-Datei `data/assets/index.js` hat sich dadurch ebenfalls geändert.

### 9. Header-Strukturen des Update-Backends wurden angepasst

Datei: `src/web/WebServerDashboard.h`

Änderungen:

- `#include <vector>` hinzugefügt
- Manifest-Asset enthält keine `sha256` mehr
- Release-Manifest enthält keine Signaturfelder mehr
- `localManifest_` entfernt
- `uploadExpectedSha_` entfernt
- `uploadTailBuffer_` hinzugefügt
- Methodensignaturen auf neues API-/Signaturmodell umgestellt

Konkrete Auswirkung:

- Das bestätigt, dass der Umbau nicht kosmetisch, sondern strukturell war.

### 10. Versionsnummer erhöht

Datei: `version.json`

- `commit: 10` -> `commit: 11`

## Technische Einordnung bezogen auf dein Problem

Da `v1.2.10` bei dir funktioniert und `v1.2.11` nicht, ist der größte auffällige Unterschied nicht WLAN-Logik im engeren Sinn, sondern der massive Umbau im Update-/Release-System.

Trotzdem kann dieser Umbau indirekt Bootprobleme verursachen, wenn zum Beispiel:

- ein OTA- oder Upload-Vorgang ein formal gültiges, aber inhaltlich falsches Image schreibt
- ein App- oder LittleFS-Image mit Trailer falsch verarbeitet wird
- Release-Assets anders aufgebaut sind als vom Laufzeitcode erwartet
- durch den Update-Umbau versehentlich Seiteneffekte in `WebServerDashboard.cpp` entstanden sind

Zusätzlich wichtig:

- Im Diff zwischen `v1.2.10` und `v1.2.11` ist keine große Änderung in der eigentlichen WiFi-Initialisierung zu sehen.
- Der dominante Unterschied sitzt klar im OTA-/Upload-/Release-Pfad.

## Wahrscheinlich kritischste Änderungen

Wenn du gezielt eingrenzen willst, würde ich zuerst diese Punkte verdächtigen:

1. Entfernen von `manifest.json` und Wechsel auf GitHub Release API
2. Eingebetteter Signatur-Trailer in `app.bin` und `web-ui.bin`
3. Neues Streaming-/Puffer-Handling beim OTA-Flashen
4. Neues Streaming-/Puffer-Handling beim lokalen Upload
5. Wegfall des alten Offline-Manifest-Pfads

## Rohdaten aus Git

Commit-Reihenfolge zwischen den Tags:

- `e3766d0` - `chore: update data, scripts, src +1 (6 files)`
- `1afb7ab` - `v1.2.11`

Dateiliste aus `git diff --name-only v1.2.10..v1.2.11`:

- `data/assets/index.js`
- `scripts/prepare-release.js`
- `scripts/run-release.ps1`
- `src/web/WebServerDashboard.cpp`
- `src/web/WebServerDashboard.h`
- `ui/src/Update.svelte`
- `version.json`