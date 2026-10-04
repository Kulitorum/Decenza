#include "settings_upload.h"

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
