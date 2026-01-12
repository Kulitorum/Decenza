#ifndef ACCESSIBILITYMANAGER_H
#define ACCESSIBILITYMANAGER_H

#include <QObject>
#include <QPointer>
#include <QTextToSpeech>
#include <QSoundEffect>
#include <QSettings>

class TranslationManager;

class AccessibilityManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool ttsEnabled READ ttsEnabled WRITE setTtsEnabled NOTIFY ttsEnabledChanged)
    Q_PROPERTY(bool tickEnabled READ tickEnabled WRITE setTickEnabled NOTIFY tickEnabledChanged)
    Q_PROPERTY(int tickSoundIndex READ tickSoundIndex WRITE setTickSoundIndex NOTIFY tickSoundIndexChanged)
    Q_PROPERTY(int tickVolume READ tickVolume WRITE setTickVolume NOTIFY tickVolumeChanged)
    Q_PROPERTY(QObject* lastAnnouncedItem READ lastAnnouncedItem WRITE setLastAnnouncedItem NOTIFY lastAnnouncedItemChanged)

    // Extraction announcement settings
    Q_PROPERTY(bool extractionAnnouncementsEnabled READ extractionAnnouncementsEnabled WRITE setExtractionAnnouncementsEnabled NOTIFY extractionAnnouncementsEnabledChanged)
    Q_PROPERTY(int extractionAnnouncementInterval READ extractionAnnouncementInterval WRITE setExtractionAnnouncementInterval NOTIFY extractionAnnouncementIntervalChanged)
    Q_PROPERTY(QString extractionAnnouncementMode READ extractionAnnouncementMode WRITE setExtractionAnnouncementMode NOTIFY extractionAnnouncementModeChanged)

public:
    explicit AccessibilityManager(QObject *parent = nullptr);
    ~AccessibilityManager();

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool ttsEnabled() const { return m_ttsEnabled; }
    void setTtsEnabled(bool enabled);

    bool tickEnabled() const { return m_tickEnabled; }
    void setTickEnabled(bool enabled);

    int tickSoundIndex() const { return m_tickSoundIndex; }
    void setTickSoundIndex(int index);

    int tickVolume() const { return m_tickVolume; }
    void setTickVolume(int volume);

    QObject* lastAnnouncedItem() const { return m_lastAnnouncedItem; }
    void setLastAnnouncedItem(QObject* item);

    // Extraction announcement settings
    bool extractionAnnouncementsEnabled() const { return m_extractionAnnouncementsEnabled; }
    void setExtractionAnnouncementsEnabled(bool enabled);

    int extractionAnnouncementInterval() const { return m_extractionAnnouncementInterval; }
    void setExtractionAnnouncementInterval(int seconds);

    QString extractionAnnouncementMode() const { return m_extractionAnnouncementMode; }
    void setExtractionAnnouncementMode(const QString& mode);

    // Called from QML
    Q_INVOKABLE void announce(const QString& text, bool interrupt = false);
    Q_INVOKABLE void announceLabel(const QString& text);  // Lower pitch + faster rate for non-interactive text
    Q_INVOKABLE void playTick();
    Q_INVOKABLE void toggleEnabled();  // For backdoor gesture

    // Must be called before app shutdown to avoid TTS race conditions
    void shutdown();

    // Connect to TranslationManager to sync TTS language with app language
    void setTranslationManager(TranslationManager* translationManager);

public slots:
    void updateTtsLocale();

signals:
    void enabledChanged();
    void ttsEnabledChanged();
    void tickEnabledChanged();
    void tickSoundIndexChanged();
    void tickVolumeChanged();
    void lastAnnouncedItemChanged();
    void extractionAnnouncementsEnabledChanged();
    void extractionAnnouncementIntervalChanged();
    void extractionAnnouncementModeChanged();

private:
    void loadSettings();
    void saveSettings();
    void initTts();
    void initTickSound();

    bool m_enabled = false;
    bool m_ttsEnabled = true;
    bool m_tickEnabled = true;
    int m_tickSoundIndex = 1;  // 1-4, default to first sound
    int m_tickVolume = 100;    // 0-100%, default full volume
    QPointer<QObject> m_lastAnnouncedItem;
    bool m_shuttingDown = false;

    // Extraction announcement settings
    bool m_extractionAnnouncementsEnabled = true;  // Default: enabled
    int m_extractionAnnouncementInterval = 5;      // Default: 5 seconds
    QString m_extractionAnnouncementMode = "both"; // "timed", "milestones_only", "both"

    QTextToSpeech* m_tts = nullptr;
    QSoundEffect* m_tickSounds[4] = {nullptr, nullptr, nullptr, nullptr};  // Pre-loaded sounds
    QSettings m_settings;

    TranslationManager* m_translationManager = nullptr;
};

#endif // ACCESSIBILITYMANAGER_H
