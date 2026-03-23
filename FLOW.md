# Entwicklungs-Flow – Salzstand

Dieser Flow beschreibt den kompletten Entwicklungsablauf vom ersten Code bis zum GitHub Release.

---

## Übersicht

```
Code schreiben
    │
    ▼
*commit  ←──────────────────┐
    │                       │ (beliebig viele Commits)
    ▼                       │
*commit  ───────────────────┘
    │
    ▼
  *test           ← Firmware + UI auf ESP flashen (ohne Versionsbump)
    │
    │  (manuell testen, bei Bedarf weitere Commits + erneut *test)
    ▼
  *push           ← Version bumpen, Versions-Commit erstellen, nach GitHub pushen
    │
    ▼
 *deploy          ← Gepushte Version (neue Versionsnummer) auf ESP flashen
    │
    ▼
 *release         ← GitHub Release mit Artefakten veröffentlichen
```

---

## Schritt-für-Schritt

### 1. Code schreiben und committen

Änderungen implementieren, dann mit dem VS Code Task committen:

```
Terminal → Run Task → Salzstand: Commit
```

Oder im Chat: Ich führe den Commit automatisch nach jeder erledigten Aufgabe aus.

**Was passiert:**
- `git add -A`
- `git commit --no-verify -m "<Message>"`
- Kein Build, kein Flash, kein Versionsbump

**Beliebig viele Commits möglich** — die Version steigt erst beim Push.

---

### 2. *test — Testen auf dem ESP

Wenn die Änderungen auf dem ESP geprüft werden sollen:

```
Terminal → Run Task → Salzstand: Test
```

Oder im Chat: `*test`

**Was passiert:**
- UI bauen (`npm run build`)
- Firmware kompilieren und flashen (`pio upload`)
- LittleFS flashen (`pio uploadfs`)
- **Keine Versionserhöhung** — der ESP zeigt die bisherige Versionsnummer

> Der *test-Schritt ist Optional, aber empfohlen vor jedem Push.

---

### 3. *push — Bedingt Version erhöhen und pushen

Wenn die Tests erfolgreich waren:

```
Terminal → Run Task → Salzstand: Push
```

Oder im Chat: `*push`

**Was passiert:**
- Wenn Firmware/UI-relevante Dateien geändert wurden (`src/`, `include/`, `ui/`, `data/`, `lib/`, `platformio.ini`, `partitions.csv`):
  - Version in `version.json` um 1 erhöhen (`commit`-Zähler)
  - UI neu bauen (`npm run build`)
  - Versions-Commit erstellen: `git commit --no-verify -m "v1.1.X"`
  - Alle Commits nach GitHub pushen (`git push --no-verify origin main`)
- Wenn nur Doku-/Repo-Änderungen vorliegen:
  - **kein** Versionsbump
  - **kein** zusätzlicher Build
  - bestehende lokale Commits werden einfach nach GitHub gepusht

**Versionsschema:** `Major.Minor.Commit` (z. B. `1.1.28`)
- `commit` wird bei Pushes mit Firmware/UI-relevanten Änderungen um 1 erhöht
- `minor` wird bei `commit == 99` um 1 erhöht (commit reset auf 0)
- `major` wird bei `minor == 9` um 1 erhöht (minor reset auf 0)

---

### 4. *deploy — Gepushte Version auf ESP flashen

Nach dem Push läuft auf dem ESP noch die alte Version. Damit der ESP die neue Versionsnummer zeigt:

```
Terminal → Run Task → Salzstand: Deploy
```

Oder im Chat: `*deploy`

**Was passiert:**
- UI bauen (`npm run build`)
- Firmware kompilieren und flashen (`pio upload`)
- LittleFS flashen (`pio uploadfs`)
- Der ESP zeigt jetzt die offizielle Version (z. B. `v1.1.28`)

---

### 5. *release — GitHub Release erstellen

Wenn ein offizielles Release veröffentlicht werden soll:

```
Terminal → Run Task → Salzstand: Release
```

Oder im Chat: `*release`

**Voraussetzung:** `*deploy` wurde bereits erfolgreich durchgeführt oder der gepushte Stand wurde direkt danach frisch gebaut.

**Was passiert:**
- Release-Artefakte packen (`node scripts/prepare-release.js`)
- GitHub Release erstellen mit allen Binaries (`gh release create vX.Y.Z`)
- Lokalen `release/`-Ordner wieder aufräumen

**Wichtig:** `*release` baut nicht selbst neu. Es verpackt nur die vorhandenen Artefakte aus `.pio/build`. Nach `*push` muss daher vor `*release` mindestens einmal `*deploy` oder ein gleichwertiger frischer Build des gepushten Stands gelaufen sein.

**Artefakte im Release:**
- `salzstand-vX.Y.Z-app.bin` — Firmware
- `salzstand-vX.Y.Z-web-ui.bin` — LittleFS (Web-UI)
- `salzstand-vX.Y.Z-bootloader.bin`
- `salzstand-vX.Y.Z-partitions.bin`
- `manifest.json` — OTA-Manifest
- `SHA256SUMS.txt` — Prüfsummen

---

## Befehlsreferenz

| Befehl im Chat | VS Code Task | Aktion |
|----------------|-------------|--------|
| *(Copilot committet automatisch)* | `Salzstand: Commit` | Änderungen committen |
| `*test` | `Salzstand: Test` | Aktuellen Stand auf ESP flashen (kein Bump) |
| `*push` | `Salzstand: Push` | Bei Firmware/UI-Änderungen bumpen, sonst ohne Bump pushen |
| `*deploy` | `Salzstand: Deploy` | Gepushte Version auf ESP flashen |
| `*release` | `Salzstand: Release` | GitHub Release erstellen |

> **Wichtig:** `*push`, `*deploy` und `*release` werden von Copilot **niemals automatisch** ausgeführt — immer nur auf expliziten Befehl.

---

## Datenpersistenz

- **Konfiguration** (WiFi, MQTT, Sensor, Push) → Default-NVS (`nvs`, Namespace `config`)
- **Messverlauf** (Historie) → separate NVS-Partition `histnvs` (Namespace `history`)
- **Web-UI** → LittleFS (`littlefs`, Start `0x360000`)

Dadurch bleiben Konfiguration und Historie bei `*test`, `*deploy` und OTA-Updates erhalten, ohne dass die kleine Default-NVS durch den Ringpuffer der Historie gefüllt wird. Nur ein bewusstes Löschen über die API/UI oder ein Full-Erase entfernt diese Daten.

Wenn sich die Partitionstabelle ändert, ist vor dem nächsten Flash ein einmaliger Full-Erase erforderlich.

---

## Git-Hooks

| Hook | Inhalt |
|------|--------|
| `pre-commit` | No-Op (`exit 0`) — kein automatischer Build |
| `pre-push` | No-Op (`exit 0`) — Bump läuft in `run-push.ps1` |

---

## Projektstruktur (relevant für den Flow)

```
scripts/
  run-commit.ps1    ← Schritt 1: committen
  run-test.ps1      ← Schritt 2: auf ESP flashen (kein Bump)
  run-push.ps1      ← Schritt 3: Version bumpen + pushen
  run-deploy.ps1    ← Schritt 4: gepushte Version auf ESP
  run-release.ps1   ← Schritt 5: GitHub Release
  bump-version.js   ← wird nur von run-push.ps1 aufgerufen
  prepare-release.js← wird nur von run-release.ps1 aufgerufen
  inject_version.py ← wird von PlatformIO beim Build aufgerufen
.vscode/
  tasks.json        ← 5 VS Code Tasks (Commit/Test/Push/Deploy/Release)
version.json        ← { "major": 1, "minor": 1, "commit": 35 }
```
