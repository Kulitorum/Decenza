#include "translationcatalog.h"

#include <QJsonDocument>

namespace TranslationCatalog {

QString resolve(const QString& fallback, const QString& translation)
{
    return translation.isEmpty() ? fallback : translation;
}

ExportResult exportSnapshot(const QString& language, quint64 revision,
                            const QMap<QString, QString>& registry,
                            const QMap<QString, QString>& translations)
{
    ExportResult result;
    if (revision > MaximumExactJsonInteger) {
        result.error = QStringLiteral("Translation catalog revision is outside the JSON-safe range");
        return result;
    }

    if (language.size() > MaximumLanguageLength) {
        result.error = QStringLiteral("Translation catalog language code exceeds the size limit");
        return result;
    }

    QMap<QString, QString> fallbacks = registry;
    for (auto it = translations.constBegin(); it != translations.constEnd(); ++it) {
        if (!fallbacks.contains(it.key()))
            fallbacks.insert(it.key(), QString());
    }
    if (fallbacks.size() > MaximumEntries) {
        result.error = QStringLiteral("Translation catalog contains too many entries");
        return result;
    }

    QJsonObject entries;
    qsizetype combinedTextUnits = 0;
    for (auto it = fallbacks.constBegin(); it != fallbacks.constEnd(); ++it) {
        const QString& key = it.key();
        const QString& fallback = it.value();
        const QString translation = translations.value(key);
        if (key.size() > MaximumKeyLength || fallback.size() > MaximumTextLength
            || translation.size() > MaximumTextLength) {
            result.error = QStringLiteral("Translation catalog entry exceeds the size limit");
            return result;
        }
        combinedTextUnits += key.size() + fallback.size() + translation.size();
        if (combinedTextUnits > MaximumCombinedTextUnits) {
            result.error = QStringLiteral("Translation catalog exceeds the response size limit");
            return result;
        }
        entries.insert(key, QJsonObject{
            {QStringLiteral("fallback"), fallback},
            {QStringLiteral("translation"), translation}
        });
    }

    result.payload = QJsonObject{
        {QStringLiteral("language"), language},
        {QStringLiteral("revision"), static_cast<double>(revision)},
        {QStringLiteral("entries"), entries}
    };
    if (QJsonDocument(result.payload).toJson(QJsonDocument::Compact).size() > MaximumPayloadBytes) {
        result.payload = {};
        result.error = QStringLiteral("Translation catalog exceeds the response size limit");
    }
    return result;
}

} // namespace TranslationCatalog
