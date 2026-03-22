#include "HistoryManager.h"
#include "DebugLogger.h"
#include <LittleFS.h>
#include <ctime>
#include <cstring>
#include <algorithm>

HistoryManager& HistoryManager::getInstance() {
    static HistoryManager instance;
    return instance;
}

bool HistoryManager::isTimeValid() {
    return time(nullptr) > 1000000000L; // nach Jahr 2001 => NTP synchronisiert
}

void HistoryManager::init() {
    if (initialized_) return;
    initialized_ = true;
    head_  = 0;
    count_ = 0;
    loadFile();
}

void HistoryManager::loadFile() {
    File f = LittleFS.open(kPath, "r");
    if (!f) {
        DebugLogger::getInstance().log(LogLevel::INFO, "HistoryManager: keine Datei vorhanden, starte leer");
        return;
    }

    FileHeader hdr;
    if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
        f.close();
        DebugLogger::getInstance().log(LogLevel::WARN, "HistoryManager: Header unlesbar, starte leer");
        return;
    }

    if (hdr.magic != kMagic || hdr.count > kCapacity || hdr.head >= kCapacity) {
        f.close();
        DebugLogger::getInstance().log(LogLevel::WARN, "HistoryManager: ungültige Datei, starte leer");
        return;
    }

    const size_t dataBytes = hdr.count * sizeof(Record);
    if (f.read(reinterpret_cast<uint8_t*>(records_), dataBytes) != dataBytes) {
        f.close();
        DebugLogger::getInstance().log(LogLevel::WARN, "HistoryManager: Daten unlesbar, starte leer");
        return;
    }

    head_  = hdr.head;
    count_ = hdr.count;
    f.close();
    DebugLogger::getInstance().log(LogLevel::INFO,
        "HistoryManager: " + std::to_string(count_) + " Einträge geladen");
}

void HistoryManager::saveFile() const {
    File f = LittleFS.open(kPath, "w");
    if (!f) {
        DebugLogger::getInstance().log(LogLevel::ERROR, "HistoryManager: Datei konnte nicht geschrieben werden");
        return;
    }

    FileHeader hdr{kMagic, head_, count_};
    f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    f.write(reinterpret_cast<const uint8_t*>(records_), count_ * sizeof(Record));
    f.close();
}

void HistoryManager::pruneOld(uint32_t now) {
    // Entferne Einträge am Anfang des Ringpuffers, die älter als kRetentionSec sind
    while (count_ > 0) {
        const Record& oldest = records_[head_];
        if (now >= oldest.ts && (now - oldest.ts) > kRetentionSec) {
            head_ = (head_ + 1) % kCapacity;
            count_--;
        } else {
            break;
        }
    }
}

void HistoryManager::addSample(float value) {
    if (!initialized_) return;
    if (!isTimeValid())  return;

    const uint32_t now = static_cast<uint32_t>(time(nullptr));

    // Zu alten Einträge entfernen
    pruneOld(now);

    // Intervall-Check: letzten gespeicherten Punkt prüfen
    if (count_ > 0) {
        const uint16_t lastIdx = static_cast<uint16_t>((head_ + count_ - 1) % kCapacity);
        if (now - records_[lastIdx].ts < kSampleIntervalSec) {
            return; // Noch nicht 6 Stunden her
        }
    }

    // Schreiben: entweder freien Slot nutzen oder ältesten überschreiben
    const uint16_t writeIdx = static_cast<uint16_t>((head_ + count_) % kCapacity);
    records_[writeIdx] = {now, value};

    if (count_ < kCapacity) {
        count_++;
    } else {
        head_ = static_cast<uint16_t>((head_ + 1) % kCapacity);
    }

    saveFile();
    DebugLogger::getInstance().log(LogLevel::INFO,
        "HistoryManager: Datenpunkt gespeichert, total=" + std::to_string(count_) +
        " pct=" + std::to_string(static_cast<int>(value)));
}

std::vector<HistoryEntry> HistoryManager::getHistory() const {
    std::vector<HistoryEntry> result;
    result.reserve(count_);
    for (uint16_t i = 0; i < count_; i++) {
        const Record& r = records_[(head_ + i) % kCapacity];
        result.push_back({r.ts, r.value});
    }
    return result;
}

void HistoryManager::clear() {
    head_  = 0;
    count_ = 0;
    LittleFS.remove(kPath);
    DebugLogger::getInstance().log(LogLevel::INFO, "HistoryManager: Verlaufsdaten gelöscht");
}
