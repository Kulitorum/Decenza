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

bool SettingsVisualizer::communitySearchEnabled() const {
    return m_settings.value("visualizer/communitySearchEnabled", true).toBool();
}

void SettingsVisualizer::setCommunitySearchEnabled(bool enabled) {
    if (communitySearchEnabled() != enabled) {
        m_settings.setValue("visualizer/communitySearchEnabled", enabled);
        emit communitySearchEnabledChanged();
    }
}

bool SettingsVisualizer::communityRelativeGrind() const {
    return m_settings.value("visualizer/communityRelativeGrind", false).toBool();
}

void SettingsVisualizer::setCommunityRelativeGrind(bool enabled) {
    if (communityRelativeGrind() != enabled) {
        m_settings.setValue("visualizer/communityRelativeGrind", enabled);
        emit communityRelativeGrindChanged();
    }
}

int SettingsVisualizer::communityMinEnjoyment() const {
    return m_settings.value("visualizer/communityMinEnjoyment", 70).toInt();
}

void SettingsVisualizer::setCommunityMinEnjoyment(int value) {
    if (communityMinEnjoyment() != value) {
        m_settings.setValue("visualizer/communityMinEnjoyment", value);
        emit communityMinEnjoymentChanged();
    }
}

QString SettingsVisualizer::communityApiBaseUrl() const {
    return m_settings.value("visualizer/communityApiBaseUrl", "https://visualizer.coffee").toString();
}

void SettingsVisualizer::setCommunityApiBaseUrl(const QString& url) {
    if (communityApiBaseUrl() != url) {
        m_settings.setValue("visualizer/communityApiBaseUrl", url);
        emit communityApiBaseUrlChanged();
    }
}
