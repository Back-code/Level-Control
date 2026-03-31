## Tech
**Frameworks**:
- PlatformIO (Firmware Build & Flash)
- Svelte (Web-UI)

**Styling**:
- CSS (custom, keine Frameworks)

**Backend**:
- ESP32 (C++), AsyncWebServer, ArduinoJson, LittleFS, NVS

**Frontend**:
- Svelte, Vite, Vanilla JS, Fetch API

**Deployment**:
- PlatformIO Upload/UploadFS (ESP32)
- GitHub Releases (OTA)

---

## Project Structure

- `src/` – Firmware (C++ für ESP32)
- `include/` – Header-Dateien
- `ui/` – Web-UI (Svelte, JS, CSS)
- `data/` – Webserver-Assets (LittleFS)
- `scripts/` – Build-/Release-Skripte (Python, JS, PowerShell)
- `signing/` – Release-Signaturen
- `API.md` – API-Endpunkt-Übersicht
- `ToDo.md` – Aufgaben & Roadmap

---

## Deployment Workflow

1. Änderungen committen (inkl. Version bump)
2. Web-UI bauen (`npm run build` im ui/)
3. Firmware & LittleFS uploaden (PlatformIO)
4. Testen (*test)
5. Push/Deploy/Release manuell per Task
6. OTA-Update via GitHub Release

**Hinweis:** Push/Deploy/Release werden nie automatisch ausgeführt, sondern nur auf explizite Nutzeranweisung.

---

## Feature Tracking

Siehe `ToDo.md` für aktuelle Aufgaben, Roadmap und erledigte Features.

- API-Änderungen werden in `API.md` dokumentiert und gepflegt
- Release- und Build-Workflow in `FLOW.md` beschrieben

---

