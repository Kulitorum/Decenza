#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

namespace TranslationCatalog {

inline constexpr qsizetype MaximumEntries = 30000;
inline constexpr qsizetype MaximumKeyLength = 512;
inline constexpr qsizetype MaximumTextLength = 16384;
inline constexpr qsizetype MaximumPayloadBytes = 8 * 1024 * 1024;
inline constexpr qsizetype MaximumCombinedTextUnits = 800 * 1024;
inline constexpr qsizetype MaximumLanguageLength = 64;
inline constexpr quint64 MaximumExactJsonInteger = 9007199254740991ULL;

struct ExportResult {
    QJsonObject payload;
    QString error;
    bool isValid() const { return error.isEmpty(); }
};

// Keep this pure lookup rule shared between the native manager and any future
// client-side catalog adapter: empty translations fall back exactly as they do
// in TranslationManager::translateString(). Whitespace is meaningful.
QString resolve(const QString& fallback, const QString& translation);

// Export the union of registered fallback strings and active-language
// translations. The hard limits prevent a malformed/local registry from
// producing an unbounded HTTP response; the endpoint fails closed rather than
// returning a silently incomplete catalog.
ExportResult exportSnapshot(const QString& language, quint64 revision,
                            const QMap<QString, QString>& registry,
                            const QMap<QString, QString>& translations);

} // namespace TranslationCatalog
