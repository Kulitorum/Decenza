#include "settings_visualizer.h"
#include "settings.h"

SettingsVisualizer::SettingsVisualizer(QObject* parent)
    : QObject(parent)
{
    connect(this, &SettingsVisualizer::visualizerUsernameChanged, this, &SettingsVisualizer::visualizerActiveChanged);
    connect(this, &SettingsVisualizer::visualizerPasswordChanged, this, &SettingsVisualizer::visualizerActiveChanged);
    connect(this, &SettingsVisualizer::visualizerEnabledChanged, this, &SettingsVisualizer::visualizerActiveChanged);
}

bool SettingsVisualizer::visualizerConnected() const {
    return !visualizerUsername().isEmpty() && !visualizerPassword().isEmpty();
}

bool SettingsVisualizer::visualizerActive() const {
    return visualizerEnabled() && visualizerConnected();
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

bool SettingsVisualizer::visualizerEnabled() const {
    return m_settings.value("visualizer/enabled", true).toBool();
}

void SettingsVisualizer::setVisualizerEnabled(bool enabled) {
    if (visualizerEnabled() != enabled) {
        m_settings.setValue("visualizer/enabled", enabled);
        emit visualizerEnabledChanged();
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
