#include "settings_upload.h"

SettingsUpload::SettingsUpload(QObject* parent)
    : QObject(parent)
{
}

bool SettingsUpload::autoUpload() const {
    return m_settings.value("visualizer/autoUpload", true).toBool();
}

void SettingsUpload::setAutoUpload(bool enabled) {
    if (autoUpload() != enabled) {
        m_settings.setValue("visualizer/autoUpload", enabled);
        emit autoUploadChanged();
    }
}

bool SettingsUpload::autoUpdate() const {
    return m_settings.value("visualizer/autoUpdate", true).toBool();
}

void SettingsUpload::setAutoUpdate(bool enabled) {
    if (autoUpdate() != enabled) {
        m_settings.setValue("visualizer/autoUpdate", enabled);
        emit autoUpdateChanged();
    }
}

double SettingsUpload::minDuration() const {
    return m_settings.value("visualizer/minDuration", 6.0).toDouble();
}

void SettingsUpload::setMinDuration(double seconds) {
    if (minDuration() != seconds) {
        m_settings.setValue("visualizer/minDuration", seconds);
        emit minDurationChanged();
    }
}
