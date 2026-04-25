#include "settings_visualizer.h"

SettingsVisualizer::SettingsVisualizer(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
}

QString SettingsVisualizer::visualizerUsername() const {
    return m_settings.value("visualizer/username", "").toString();
}

void SettingsVisualizer::setVisualizerUsername(const QString& username) {
    if (visualizerUsername() != username) {
        m_settings.setValue("visualizer/username", username);
        emit visualizerUsernameChanged();
    }
}

QString SettingsVisualizer::visualizerPassword() const {
    return m_settings.value("visualizer/password", "").toString();
}

void SettingsVisualizer::setVisualizerPassword(const QString& password) {
    if (visualizerPassword() != password) {
        m_settings.setValue("visualizer/password", password);
        emit visualizerPasswordChanged();
    }
}

bool SettingsVisualizer::visualizerAutoUpload() const {
    return m_settings.value("visualizer/autoUpload", true).toBool();
}

void SettingsVisualizer::setVisualizerAutoUpload(bool enabled) {
    if (visualizerAutoUpload() != enabled) {
        m_settings.setValue("visualizer/autoUpload", enabled);
        emit visualizerAutoUploadChanged();
    }
}

double SettingsVisualizer::visualizerMinDuration() const {
    return m_settings.value("visualizer/minDuration", 6.0).toDouble();
}

void SettingsVisualizer::setVisualizerMinDuration(double seconds) {
    if (visualizerMinDuration() != seconds) {
        m_settings.setValue("visualizer/minDuration", seconds);
        emit visualizerMinDurationChanged();
    }
}

bool SettingsVisualizer::visualizerExtendedMetadata() const {
    return m_settings.value("visualizer/extendedMetadata", false).toBool();
}

void SettingsVisualizer::setVisualizerExtendedMetadata(bool enabled) {
    if (visualizerExtendedMetadata() != enabled) {
        m_settings.setValue("visualizer/extendedMetadata", enabled);
        emit visualizerExtendedMetadataChanged();
    }
}

bool SettingsVisualizer::visualizerShowAfterShot() const {
    return m_settings.value("visualizer/showAfterShot", true).toBool();
}

void SettingsVisualizer::setVisualizerShowAfterShot(bool enabled) {
    if (visualizerShowAfterShot() != enabled) {
        m_settings.setValue("visualizer/showAfterShot", enabled);
        emit visualizerShowAfterShotChanged();
    }
}

bool SettingsVisualizer::visualizerClearNotesOnStart() const {
    return m_settings.value("visualizer/clearNotesOnStart", false).toBool();
}

void SettingsVisualizer::setVisualizerClearNotesOnStart(bool enabled) {
    if (visualizerClearNotesOnStart() != enabled) {
        m_settings.setValue("visualizer/clearNotesOnStart", enabled);
        emit visualizerClearNotesOnStartChanged();
    }
}

int SettingsVisualizer::defaultShotRating() const {
    return m_settings.value("shot/defaultRating", 75).toInt();
}

void SettingsVisualizer::setDefaultShotRating(int rating) {
    if (defaultShotRating() != rating) {
        m_settings.setValue("shot/defaultRating", rating);
        emit defaultShotRatingChanged();
        // Note: side-effect of also updating the current shot's enjoyment rating
        // (formerly: setDyeEspressoEnjoyment(rating)) is now handled by callers
        // via signal connection or explicit cross-domain call.
    }
}
