#include "shotfileexport.h"

#include "shothistorystorage.h"
#include "network/visualizeruploader.h"

#include <QJsonDocument>
#include <QJsonParseError>

QByteArray ShotFileExport::data(const ShotRecord& record, Format format, QString* reason)
{
    reason->clear();
    if (record.summary.id <= 0) {
        *reason = QStringLiteral("Shot not found.");
        return {};
    }
    if (format == Format::DebugLog) {
        if (record.debugLog.isEmpty())
            *reason = QStringLiteral("No debug log is available for this shot.");
        return record.debugLog.toUtf8();
    }
    if (format == Format::Shot)
        return VisualizerUploader::buildHistoryShotJson(
            ShotHistoryStorage::convertShotRecord(record), true);

    QJsonParseError error;
    const QJsonDocument profile = QJsonDocument::fromJson(record.profileJson.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !profile.isObject()) {
        *reason = QStringLiteral("No valid recorded profile is available for this shot.");
        return {};
    }
    return profile.toJson(QJsonDocument::Indented);
}
