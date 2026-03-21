#include "PushNotificationManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cctype>
#include <ctime>

#include "ConfigStore.h"
#include "DebugLogger.h"
#include "MqttManager.h"
#include "SensorManager.h"

namespace {
constexpr unsigned long kConfigReloadIntervalMs = 30000UL;
constexpr unsigned long kProcessIntervalMs = 10000UL;
constexpr time_t kRetryAfterFailureSeconds = 300;
constexpr time_t kMinValidEpoch = 1700000000;
constexpr uint32_t kSmtpTimeoutMs = 15000;

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }

    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.length(), to);
        pos += to.length();
    }
}

std::string formatFloat(float value, int decimals) {
    char buffer[32] = {0};
    snprintf(buffer, sizeof(buffer), (std::string("%.") + std::to_string(decimals) + "f").c_str(), value);
    return buffer;
}

std::string formatTimestamp(time_t epoch) {
    if (epoch < kMinValidEpoch) {
        return "n/a";
    }

    struct tm timeInfo;
    localtime_r(&epoch, &timeInfo);
    char buffer[32] = {0};
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &timeInfo);
    return buffer;
}

std::string toBase64(const std::string& input) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i < input.size()) {
        const uint32_t octetA = i < input.size() ? static_cast<unsigned char>(input[i++]) : 0;
        const uint32_t octetB = i < input.size() ? static_cast<unsigned char>(input[i++]) : 0;
        const uint32_t octetC = i < input.size() ? static_cast<unsigned char>(input[i++]) : 0;

        const uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

        output.push_back(alphabet[(triple >> 18) & 0x3F]);
        output.push_back(alphabet[(triple >> 12) & 0x3F]);
        output.push_back(i - 1 > input.size() ? '=' : alphabet[(triple >> 6) & 0x3F]);
        output.push_back(i > input.size() ? '=' : alphabet[triple & 0x3F]);
    }

    const size_t mod = input.size() % 3;
    if (mod > 0) {
        output[output.size() - 1] = '=';
        if (mod == 1) {
            output[output.size() - 2] = '=';
        }
    }

    return output;
}

bool readSmtpResponse(Client& client, int expectedCode, std::string& error) {
    unsigned long start = millis();
    std::string lastLine;

    while (millis() - start < kSmtpTimeoutMs) {
        while (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            lastLine = line.c_str();

            if (lastLine.size() < 4) {
                continue;
            }

            if (!isdigit(static_cast<unsigned char>(lastLine[0]))
                || !isdigit(static_cast<unsigned char>(lastLine[1]))
                || !isdigit(static_cast<unsigned char>(lastLine[2]))) {
                continue;
            }

            const int code = atoi(lastLine.substr(0, 3).c_str());
            const char separator = lastLine[3];
            if (separator == '-') {
                continue;
            }

            if (code == expectedCode) {
                return true;
            }

            error = "SMTP " + std::to_string(code) + ": " + lastLine;
            return false;
        }

        delay(10);
    }

    error = lastLine.empty() ? "SMTP Antwort Timeout" : ("SMTP Timeout: " + lastLine);
    return false;
}

bool sendSmtpCommand(Client& client, const std::string& command, int expectedCode, std::string& error) {
    client.print(command.c_str());
    return readSmtpResponse(client, expectedCode, error);
}

std::string normalizeBody(const std::string& body) {
    std::string result = body;
    replaceAll(result, "\r\n", "\n");
    replaceAll(result, "\r", "\n");
    replaceAll(result, "\n", "\r\n");
    return result;
}

std::string trimCopy(const std::string& value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}
}

PushNotificationManager& PushNotificationManager::getInstance() {
    static PushNotificationManager instance;
    return instance;
}

void PushNotificationManager::init() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    reloadConfig(true);
}

bool PushNotificationManager::reloadConfig(bool force) {
    const unsigned long now = millis();
    if (!force && now - lastConfigReloadMs_ < kConfigReloadIntervalMs) {
        return true;
    }

    Config config;
    const bool loaded = ConfigStore::getInstance().load(config);
    lastConfigReloadMs_ = now;
    return loaded;
}

bool PushNotificationManager::isConfigured() const {
    Config config;
    if (!ConfigStore::getInstance().load(config)) {
        return false;
    }

    return !config.push.smtpServer.empty()
        && !config.push.senderEmail.empty()
        && !config.push.recipientEmail.empty()
        && !config.push.authUser.empty()
        && !config.push.authPassword.empty();
}

bool PushNotificationManager::isSendWindowOpen(time_t nowEpoch) const {
    Config config;
    if (!ConfigStore::getInstance().load(config)) {
        return false;
    }

    if (nowEpoch < kMinValidEpoch) {
        return false;
    }

    struct tm timeInfo;
    localtime_r(&nowEpoch, &timeInfo);

    const int currentMinutes = (timeInfo.tm_hour * 60) + timeInfo.tm_min;
    const int startMinutes = (config.push.sendHour * 60) + config.push.sendMinute;

    return currentMinutes >= startMinutes;
}

std::string PushNotificationManager::renderTemplate(const std::string& input, float levelPercent, float levelCm, float rawDistanceM, time_t nowEpoch) const {
    std::string text = input;
    replaceAll(text, "{level_percent}", formatFloat(levelPercent, 1));
    replaceAll(text, "{level_cm}", formatFloat(levelCm, 1));
    replaceAll(text, "{raw_distance_m}", formatFloat(rawDistanceM, 3));
    replaceAll(text, "{timestamp}", formatTimestamp(nowEpoch));
    replaceAll(text, "{device_id}", MqttManager::getInstance().getDeviceId());
    replaceAll(text, "{ip}", WiFi.localIP().toString().c_str());
    replaceAll(text, "{ssid}", WiFi.SSID().c_str());
    return text;
}

bool PushNotificationManager::sendEmailInternal(const std::string& subject, const std::string& body, std::string& error, bool requireEnabled) {
    if (WiFi.status() != WL_CONNECTED) {
        error = "WLAN nicht verbunden";
        return false;
    }

    Config config;
    if (!ConfigStore::getInstance().load(config)) {
        error = "Push-Konfiguration konnte nicht geladen werden";
        return false;
    }

    if (requireEnabled && !config.push.enabled) {
        error = "Push-Benachrichtigung ist deaktiviert";
        return false;
    }

    if (!isConfigured()) {
        error = "SMTP-Konfiguration unvollstaendig";
        return false;
    }

    if (config.push.startTls && !config.push.useSsl) {
        error = "STARTTLS wird aktuell nicht unterstuetzt. Bitte SSL/TLS aktivieren.";
        return false;
    }

    const std::string smtpServer = trimCopy(config.push.smtpServer);
    if (smtpServer.empty()) {
        error = "SMTP-Server fehlt";
        return false;
    }

    if (config.push.useSsl && config.push.smtpPort == 587) {
        error = "SSL/TLS aktiv, aber Port 587 gesetzt. Bitte Port 465 verwenden.";
        return false;
    }

    if (config.push.startTls && config.push.smtpPort == 465) {
        error = "STARTTLS aktiv, aber Port 465 gesetzt. Bitte Port 587 verwenden.";
        return false;
    }

    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    Client* client = nullptr;

    if (config.push.useSsl) {
        secureClient.setInsecure();
        secureClient.setTimeout(kSmtpTimeoutMs / 1000);
        client = &secureClient;
    } else {
        plainClient.setTimeout(kSmtpTimeoutMs / 1000);
        client = &plainClient;
    }

    IPAddress smtpIp;
    if (!WiFi.hostByName(smtpServer.c_str(), smtpIp)) {
        error = "SMTP DNS-Aufloesung fehlgeschlagen: " + smtpServer;
        return false;
    }

    if (!client->connect(smtpServer.c_str(), config.push.smtpPort)) {
        error = "SMTP Verbindung fehlgeschlagen (" + smtpServer + ":" + std::to_string(config.push.smtpPort) + ")";
        return false;
    }

    if (!readSmtpResponse(*client, 220, error)) return false;
    if (!sendSmtpCommand(*client, "EHLO salzstand.local\r\n", 250, error)) return false;
    if (!sendSmtpCommand(*client, "AUTH LOGIN\r\n", 334, error)) return false;
    if (!sendSmtpCommand(*client, toBase64(config.push.authUser) + "\r\n", 334, error)) return false;
    if (!sendSmtpCommand(*client, toBase64(config.push.authPassword) + "\r\n", 235, error)) return false;
    if (!sendSmtpCommand(*client, "MAIL FROM:<" + config.push.senderEmail + ">\r\n", 250, error)) return false;
    if (!sendSmtpCommand(*client, "RCPT TO:<" + config.push.recipientEmail + ">\r\n", 250, error)) return false;
    if (!sendSmtpCommand(*client, "DATA\r\n", 354, error)) return false;

    std::string payload;
    payload.reserve(body.size() + 512);
    payload += "From: ";
    payload += config.push.senderName.empty() ? "Salzstand" : config.push.senderName;
    payload += " <" + config.push.senderEmail + ">\r\n";
    payload += "To: <" + config.push.recipientEmail + ">\r\n";
    payload += "Subject: " + subject + "\r\n";
    payload += "MIME-Version: 1.0\r\n";
    payload += "Content-Type: text/plain; charset=UTF-8\r\n";
    payload += "\r\n";
    payload += normalizeBody(body);
    payload += "\r\n.\r\n";

    if (!sendSmtpCommand(*client, payload, 250, error)) return false;
    sendSmtpCommand(*client, "QUIT\r\n", 221, error);
    client->stop();
    return true;
}

bool PushNotificationManager::sendThresholdEmail(float levelPercent, float levelCm, float rawDistanceM, std::string& error) {
    const time_t nowEpoch = time(nullptr);

    Config config;
    if (!ConfigStore::getInstance().load(config)) {
        error = "Push-Konfiguration konnte nicht geladen werden";
        return false;
    }

    const std::string subject = renderTemplate(config.push.subjectTemplate, levelPercent, levelCm, rawDistanceM, nowEpoch);
    const std::string body = renderTemplate(config.push.bodyTemplate, levelPercent, levelCm, rawDistanceM, nowEpoch);
    return sendEmailInternal(subject, body, error, true);
}

void PushNotificationManager::process(float levelPercent, float levelCm, float rawDistanceM, bool validReading) {
    const unsigned long nowMs = millis();
    if (nowMs - lastProcessMs_ < kProcessIntervalMs) {
        return;
    }
    lastProcessMs_ = nowMs;

    reloadConfig();

    Config config;
    if (!ConfigStore::getInstance().load(config) || !config.push.enabled) {
        wasBelowThreshold_ = false;
        return;
    }

    if (!validReading) {
        return;
    }

    const bool isBelowThreshold = levelPercent <= config.push.triggerPercent;
    if (!isBelowThreshold) {
        wasBelowThreshold_ = false;
        return;
    }

    const time_t nowEpoch = time(nullptr);
    if (!isSendWindowOpen(nowEpoch)) {
        return;
    }

    const time_t cycleSeconds = static_cast<time_t>(config.push.cycleMinutes) * 60;
    if (cycleSeconds > 0 && lastSentEpoch_ > 0 && (nowEpoch - lastSentEpoch_) < cycleSeconds) {
        return;
    }

    if (lastFailureEpoch_ > 0 && (nowEpoch - lastFailureEpoch_) < kRetryAfterFailureSeconds) {
        return;
    }

    std::string error;
    if (sendThresholdEmail(levelPercent, levelCm, rawDistanceM, error)) {
        lastSentEpoch_ = nowEpoch;
        lastFailureEpoch_ = 0;
        wasBelowThreshold_ = true;
        DebugLogger::getInstance().log(LogLevel::INFO,
            "Push-Mail versendet bei " + formatFloat(levelPercent, 1) + "% Fuellstand");
    } else {
        lastFailureEpoch_ = nowEpoch;
        DebugLogger::getInstance().log(LogLevel::WARN,
            "Push-Mail Versand fehlgeschlagen: " + error);
    }
}

bool PushNotificationManager::sendTestEmail(std::string& error) {
    const float levelPercent = SensorManager::getInstance().getDistancePercent();
    const float levelCm = SensorManager::getInstance().getDistanceCm();
    const float rawDistanceM = SensorManager::getInstance().getRawDistance();

    Config config;
    if (!ConfigStore::getInstance().load(config)) {
        error = "Push-Konfiguration konnte nicht geladen werden";
        return false;
    }

    const time_t nowEpoch = time(nullptr);
    const std::string subject = renderTemplate(config.push.subjectTemplate, levelPercent, levelCm, rawDistanceM, nowEpoch) + " [TEST]";
    const std::string body = renderTemplate(config.push.bodyTemplate, levelPercent, levelCm, rawDistanceM, nowEpoch);

    return sendEmailInternal(subject, body, error, false);
}
