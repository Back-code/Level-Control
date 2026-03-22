#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include <cstdint>
#include <vector>

struct HistoryEntry {
    uint32_t timestamp; ///< Unix-Zeit in Sekunden
    float    value;     ///< Salzstand in Prozent (0..100)
};

/**
 * HistoryManager – speichert Salzstand-Messwerte dauerhaft in LittleFS.
 *
 * Binäres Ringpuffer-Format (/history.bin):
 *   Header   8 Byte :  magic(4) | head(2) | count(2)
 *   Daten    n×8 B  :  timestamp(4) + value(4) pro Eintrag
 *
 * Kapazität : 1460 Einträge (4/Tag × 365 Tage = 12 Monate)
 * Intervall : mindestens 6 Stunden zwischen zwei Datenpunkten
 * Retention : Einträge älter als 365 Tage werden automatisch verworfen
 */
class HistoryManager {
public:
    static HistoryManager& getInstance();

    /** LittleFS muss vor init() gemountet sein (wird von WebServerDashboard gemacht). */
    void init();

    /**
     * Fügt einen Messwert hinzu, falls:
     *  - NTP-Zeit gültig (> Jahr 2001)
     *  - seit dem letzten gespeicherten Punkt ≥ 6 Stunden vergangen
     */
    void addSample(float value);

    /** Gibt alle gespeicherten Einträge in chronologischer Reihenfolge zurück. */
    std::vector<HistoryEntry> getHistory() const;

    /** Löscht alle Daten (RAM + Datei). */
    void clear();

    /** Gibt true zurück, wenn die Systemzeit gültig (NTP synchronisiert) ist. */
    static bool isTimeValid();

    static constexpr uint16_t kCapacity         = 1460;
    static constexpr uint32_t kSampleIntervalSec = 6UL * 3600UL;   ///< 6 Stunden
    static constexpr uint32_t kRetentionSec      = 365UL * 24UL * 3600UL; ///< 12 Monate

private:
    HistoryManager() = default;

    struct Record {
        uint32_t ts;
        float    value;
    };

    struct FileHeader {
        uint32_t magic;
        uint16_t head;
        uint16_t count;
    };

    static constexpr uint32_t kMagic    = 0x53414C54UL; // 'SALT'
    static constexpr const char* kPath  = "/history.bin";

    bool   initialized_ = false;
    uint16_t head_  = 0; ///< Index des ältesten Eintrags
    uint16_t count_ = 0; ///< Anzahl gültiger Einträge
    Record   records_[kCapacity];

    void loadFile();
    void saveFile() const;
    void pruneOld(uint32_t now);
};

#endif // HISTORY_MANAGER_H
