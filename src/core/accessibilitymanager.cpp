#include "accessibilitymanager.h"
#include "translationmanager.h"
#include <QDebug>
#include <QCoreApplication>
#include <QApplication>
#include <QLocale>

AccessibilityManager::AccessibilityManager(QObject *parent)
    : QObject(parent)
    , m_settings("Decenza", "DE1")
{
    loadSettings();
    initTts();
    initTickSound();
}

AccessibilityManager::~AccessibilityManager()
{
    // Don't call m_tts->stop() here - it causes race conditions with Android TTS
    // shutdown() should have been called already via aboutToQuit
    // The QObject parent-child relationship will handle deletion
}

void AccessibilityManager::shutdown()
{
    if (m_shuttingDown) return;
    m_shuttingDown = true;

    qDebug() << "AccessibilityManager shutting down";

    // Disconnect all signals from TTS to prevent callbacks during shutdown
    if (m_tts) {
        disconnect(m_tts, nullptr, this, nullptr);

        // Only try to stop if TTS is in a valid state
        // This minimizes the window for race conditions
        if (m_tts->state() == QTextToSpeech::Speaking ||
            m_tts->state() == QTextToSpeech::Synthesizing) {
            m_tts->stop();
        }

        // Don't delete m_tts - it's a child QObject and will be cleaned up
        // Setting to nullptr prevents any further use
        m_tts = nullptr;
    }

    for (int i = 0; i < 4; i++) {
        if (m_tickSounds[i]) {
            m_tickSounds[i]->stop();
            m_tickSounds[i] = nullptr;
        }
    }
}

void AccessibilityManager::loadSettings()
{
    m_enabled = m_settings.value("accessibility/enabled", false).toBool();
    m_ttsEnabled = m_settings.value("accessibility/ttsEnabled", true).toBool();
    m_tickEnabled = m_settings.value("accessibility/tickEnabled", true).toBool();
    m_tickSoundIndex = m_settings.value("accessibility/tickSoundIndex", 1).toInt();
    m_tickVolume = m_settings.value("accessibility/tickVolume", 100).toInt();

    // Extraction announcement settings
    m_extractionAnnouncementsEnabled = m_settings.value("accessibility/extractionAnnouncementsEnabled", true).toBool();
    m_extractionAnnouncementInterval = m_settings.value("accessibility/extractionAnnouncementInterval", 5).toInt();
    m_extractionAnnouncementMode = m_settings.value("accessibility/extractionAnnouncementMode", "both").toString();
}

void AccessibilityManager::saveSettings()
{
    m_settings.setValue("accessibility/enabled", m_enabled);
    m_settings.setValue("accessibility/ttsEnabled", m_ttsEnabled);
    m_settings.setValue("accessibility/tickEnabled", m_tickEnabled);
    m_settings.setValue("accessibility/tickSoundIndex", m_tickSoundIndex);
    m_settings.setValue("accessibility/tickVolume", m_tickVolume);

    // Extraction announcement settings
    m_settings.setValue("accessibility/extractionAnnouncementsEnabled", m_extractionAnnouncementsEnabled);
    m_settings.setValue("accessibility/extractionAnnouncementInterval", m_extractionAnnouncementInterval);
    m_settings.setValue("accessibility/extractionAnnouncementMode", m_extractionAnnouncementMode);

    m_settings.sync();
}

void AccessibilityManager::initTts()
{
    auto engines = QTextToSpeech::availableEngines();
    qDebug() << "Available TTS engines:" << engines;

    // On Android, use "android" engine which delegates to system TTS settings
    // This respects the user's preferred engine and voice from Android preferences
#ifdef Q_OS_ANDROID
    if (engines.contains("android")) {
        m_tts = new QTextToSpeech("android", this);
        qDebug() << "Using Android system TTS";
    } else {
        m_tts = new QTextToSpeech(this);
    }
#else
    m_tts = new QTextToSpeech(this);
#endif

    connect(m_tts, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State state) {
        qDebug() << "TTS state changed:" << state;
        if (state == QTextToSpeech::Error) {
            qWarning() << "TTS error:" << m_tts->errorString();
        } else if (state == QTextToSpeech::Ready) {
            qDebug() << "TTS ready";
            // Sync locale with app language
            if (m_translationManager) {
                updateTtsLocale();
            }
        }
    });
}

void AccessibilityManager::initTickSound()
{
    // Pre-load all 4 tick sounds for instant playback
    qreal vol = m_tickVolume / 100.0;
    for (int i = 0; i < 4; i++) {
        m_tickSounds[i] = new QSoundEffect(this);
        m_tickSounds[i]->setSource(QUrl(QString("qrc:/sounds/frameclick%1.wav").arg(i + 1)));
        m_tickSounds[i]->setVolume(vol);
    }
}

void AccessibilityManager::setEnabled(bool enabled)
{
    if (m_shuttingDown || m_enabled == enabled) return;
    m_enabled = enabled;
    saveSettings();
    emit enabledChanged();

    qDebug() << "Accessibility" << (m_enabled ? "enabled" : "disabled");

    // Announce the change
    if (m_tts && m_ttsEnabled) {
        m_tts->say(m_enabled ? "Accessibility enabled" : "Accessibility disabled");
    }
}

void AccessibilityManager::setTtsEnabled(bool enabled)
{
    if (m_ttsEnabled == enabled) return;
    m_ttsEnabled = enabled;
    saveSettings();
    emit ttsEnabledChanged();
}

void AccessibilityManager::setTickEnabled(bool enabled)
{
    if (m_tickEnabled == enabled) return;
    m_tickEnabled = enabled;
    saveSettings();
    emit tickEnabledChanged();
}

void AccessibilityManager::setTickSoundIndex(int index)
{
    index = qBound(1, index, 4);
    if (m_tickSoundIndex == index) return;
    m_tickSoundIndex = index;
    saveSettings();
    emit tickSoundIndexChanged();

    // Play the selected sound immediately (all sounds are pre-loaded)
    int idx = index - 1;
    if (idx >= 0 && idx < 4 && m_tickSounds[idx] && m_tickSounds[idx]->status() == QSoundEffect::Ready) {
        m_tickSounds[idx]->play();
    }
}

void AccessibilityManager::setTickVolume(int volume)
{
    volume = qBound(0, volume, 100);
    if (m_tickVolume == volume) return;
    m_tickVolume = volume;
    saveSettings();
    emit tickVolumeChanged();

    // Update all sound volumes
    qreal vol = volume / 100.0;
    for (int i = 0; i < 4; i++) {
        if (m_tickSounds[i]) {
            m_tickSounds[i]->setVolume(vol);
        }
    }

    // Play preview
    playTick();
}

void AccessibilityManager::setLastAnnouncedItem(QObject* item)
{
    if (m_lastAnnouncedItem == item) return;
    m_lastAnnouncedItem = item;
    emit lastAnnouncedItemChanged();
}

void AccessibilityManager::setExtractionAnnouncementsEnabled(bool enabled)
{
    if (m_extractionAnnouncementsEnabled == enabled) return;
    m_extractionAnnouncementsEnabled = enabled;
    saveSettings();
    emit extractionAnnouncementsEnabledChanged();
}

void AccessibilityManager::setExtractionAnnouncementInterval(int seconds)
{
    seconds = qBound(5, seconds, 30);  // 5-30 seconds
    if (m_extractionAnnouncementInterval == seconds) return;
    m_extractionAnnouncementInterval = seconds;
    saveSettings();
    emit extractionAnnouncementIntervalChanged();
}

void AccessibilityManager::setExtractionAnnouncementMode(const QString& mode)
{
    // Valid modes: "timed", "milestones_only", "both"
    QString validMode = mode;
    if (mode != "timed" && mode != "milestones_only" && mode != "both") {
        validMode = "both";  // Default
    }
    if (m_extractionAnnouncementMode == validMode) return;
    m_extractionAnnouncementMode = validMode;
    saveSettings();
    emit extractionAnnouncementModeChanged();
}

void AccessibilityManager::announce(const QString& text, bool interrupt)
{
    if (m_shuttingDown || !m_enabled || !m_ttsEnabled || !m_tts) return;

    if (interrupt) {
        m_tts->stop();
    }

    m_tts->say(text);
    qDebug() << "Accessibility announcement:" << text;
}

void AccessibilityManager::announceLabel(const QString& text)
{
    if (m_shuttingDown || !m_enabled || !m_ttsEnabled || !m_tts) return;

    // Save current settings
    double originalPitch = m_tts->pitch();
    double originalRate = m_tts->rate();

    // Lower pitch + faster rate for labels (distinguishes from interactive elements)
    m_tts->setPitch(-0.3);  // Slightly lower pitch
    m_tts->setRate(0.2);    // Slightly faster

    m_tts->say(text);
    qDebug() << "Accessibility label:" << text;

    // Restore settings after speech starts
    // Note: QTextToSpeech queues the settings, so this works
    m_tts->setPitch(originalPitch);
    m_tts->setRate(originalRate);
}

void AccessibilityManager::playTick()
{
    if (m_shuttingDown || !m_enabled || !m_tickEnabled) return;

    int idx = m_tickSoundIndex - 1;  // Convert 1-4 to 0-3
    if (idx >= 0 && idx < 4 && m_tickSounds[idx] && m_tickSounds[idx]->status() == QSoundEffect::Ready) {
        m_tickSounds[idx]->play();
    }
}

void AccessibilityManager::toggleEnabled()
{
    if (m_shuttingDown) return;

    bool wasEnabled = m_enabled;
    setEnabled(!m_enabled);

    // Always announce toggle result
    if (m_tts && m_ttsEnabled) {
        m_tts->stop();
        m_tts->say(m_enabled ? "Accessibility enabled" : "Accessibility disabled");
    }
}

void AccessibilityManager::setTranslationManager(TranslationManager* translationManager)
{
    if (m_translationManager) {
        disconnect(m_translationManager, nullptr, this, nullptr);
    }

    m_translationManager = translationManager;

    if (m_translationManager) {
        connect(m_translationManager, &TranslationManager::currentLanguageChanged,
                this, &AccessibilityManager::updateTtsLocale);

        // Set initial locale
        updateTtsLocale();
    }
}

void AccessibilityManager::updateTtsLocale()
{
    if (!m_tts || !m_translationManager) return;

    QString langCode = m_translationManager->currentLanguage();
    QLocale locale(langCode);

    // Check if this locale is available for TTS
    QList<QLocale> availableLocales = m_tts->availableLocales();

    // Try exact match first
    bool found = false;
    for (const QLocale& available : availableLocales) {
        if (available.language() == locale.language()) {
            m_tts->setLocale(available);
            qDebug() << "TTS locale set to:" << available.name() << "for language:" << langCode;
            found = true;
            break;
        }
    }

    if (!found) {
        qDebug() << "TTS locale not available for:" << langCode << "- using system default";
    }
}
