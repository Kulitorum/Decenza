#pragma once

#include <QJsonObject>
#include <QStringList>

class Settings;

/**
 * @brief Serializes Settings to/from portable JSON format for cross-platform migration.
 *
 * Handles conversion of QSettings data between platform-specific storage formats
 * (Windows Registry, macOS plist, Linux config files) to a portable JSON format.
 */
class SettingsSerializer {
public:
    /**
     * @brief Export all settings to a JSON object.
     * @param settings The Settings instance to export.
     * @param includeSensitive If true, includes credentials — API keys, passwords,
     *        and the visualizer/MQTT usernames. Keep false for anything served over
     *        the (default-unauthenticated) LAN web endpoints.
     * @return JSON object containing all exported settings.
     */
    static QJsonObject exportToJson(Settings* settings, bool includeSensitive = false);

    /**
     * @brief Import settings from a JSON object.
     * @param settings The Settings instance to import into.
     * @param json The JSON object containing settings data.
     * @param excludeKeys List of settings keys to skip during import.
     * @return true if import was successful.
     */
    static bool importFromJson(Settings* settings, const QJsonObject& json,
                               const QStringList& excludeKeys = {});

    /**
     * @brief Get list of sensitive settings keys that are excluded by default.
     * @return List of keys for sensitive data (API keys, passwords).
     */
    static QStringList sensitiveKeys();

private:
    // Helper to convert QVariant to JSON-compatible value
    static QJsonValue variantToJson(const QVariant& value);

    // Helper to convert JSON value back to QVariant
    static QVariant jsonToVariant(const QJsonValue& value, const QString& key);
};
