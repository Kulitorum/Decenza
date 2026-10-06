#include "settings_upload.h"

#include <QDateTime>

namespace {
// Pre-split keys, kept so stored values carry over.
const QString kAutoUploadKey = QStringLiteral("visualizer/autoUpload");
const QString kAutoUpdateKey = QStringLiteral("visualizer/autoUpdate");
const QString kMinDurationKey = QStringLiteral("visualizer/minDuration");
}

SettingsUpload::SettingsUpload(QObject* parent)
    : QObject(parent)
{
}

bool SettingsUpload::autoUpload() const {
    return m_settings.value(kAutoUploadKey, true).toBool();
}

void SettingsUpload::setAutoUpload(bool enabled) {
    if (autoUpload() != enabled) {
        m_settings.setValue(kAutoUploadKey, enabled);
        emit autoUploadChanged();
    }
}

bool SettingsUpload::autoUpdate() const {
    return m_settings.value(kAutoUpdateKey, true).toBool();
}

void SettingsUpload::setAutoUpdate(bool enabled) {
    if (autoUpdate() != enabled) {
        m_settings.setValue(kAutoUpdateKey, enabled);
        emit autoUpdateChanged();
    }
}

double SettingsUpload::minDuration() const {
    return m_settings.value(kMinDurationKey, 6.0).toDouble();
}

void SettingsUpload::setMinDuration(double seconds) {
    if (minDuration() != seconds) {
        m_settings.setValue(kMinDurationKey, seconds);
        emit minDurationChanged();
    }
}

qint64 SettingsUpload::missingRunStartedAt(const QString& destination) const {
    return m_settings.value(QStringLiteral("upload/missingRun/") + destination, 0).toLongLong();
}

qint64 SettingsUpload::rateLimitedUntilMs(const QString& destination) const {
    return m_settings.value(QStringLiteral("upload/rateLimitedUntil/") + destination, 0).toLongLong();
}

void SettingsUpload::setRateLimitedUntilMs(const QString& destination, qint64 msSinceEpoch) {
    const QString key = QStringLiteral("upload/rateLimitedUntil/") + destination;
    if (msSinceEpoch > 0)
        m_settings.setValue(key, msSinceEpoch);
    else
        m_settings.remove(key);
    emit rateLimitedUntilChanged(destination);
}

qint64 SettingsUpload::rateLimitRemainingMs(const QString& destination) const {
    return qMax<qint64>(0, rateLimitedUntilMs(destination) - QDateTime::currentMSecsSinceEpoch());
}

void SettingsUpload::noteRateLimited(const QString& destination) {
    if (rateLimitRemainingMs(destination) == 0)
        setRateLimitedUntilMs(destination, QDateTime::currentMSecsSinceEpoch() + kRateLimitWaitMs);
}

void SettingsUpload::setMissingRunStartedAt(const QString& destination, qint64 secsSinceEpoch) {
    const QString key = QStringLiteral("upload/missingRun/") + destination;
    if (secsSinceEpoch > 0)
        m_settings.setValue(key, secsSinceEpoch);
    else
        m_settings.remove(key);
}

