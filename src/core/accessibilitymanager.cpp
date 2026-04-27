#include "accessibilitymanager.h"
#include "translationmanager.h"
#include <QDebug>
#include <QCoreApplication>
#include <QLocale>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QWindow>

#ifndef QT_NO_ACCESSIBILITY
#include <QAccessible>
#endif

AccessibilityManager::AccessibilityManager(QObject *parent)
    : QObject(parent)
    , m_settings("Decenza", "DE1")
{
    loadSettings();
    initTts();
    if (m_enabled && m_tickEnabled)
        initTickSound();
}

#ifdef DECENZA_TESTING
AccessibilityManager::AccessibilityManager(TestSkipAudioInit, QObject *parent)
    : QObject(parent)
    , m_settings("Decenza", "DE1")
{
    // Deliberately skip loadSettings() so tests don't inherit whatever the
    // dev machine has persisted in QSettings("Decenza", "DE1"). Member
    // defaults from the header (m_enabled=false, m_ttsEnabled=true, etc.)
    // give a deterministic starting state. Skip initTts() / initTickSound()
    // for the same reason — tests override the dispatch virtuals.
}
#endif

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
                onLanguageChanged();
            }
        }
    });
}

void AccessibilityManager::initTickSound()
{
    if (m_tickSounds[0])
        return;  // Already initialized

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

    // Announce the change. Bypass announce()'s m_enabled guard intentionally —
    // we want "Accessibility disabled" to play even though m_enabled is now
    // false. routeAnnouncement() still respects isScreenReaderActive(), so we
    // don't double-speak when TalkBack/VoiceOver is on.
    routeAnnouncement(m_enabled ? QStringLiteral("Accessibility enabled")
                                : QStringLiteral("Accessibility disabled"),
                      /*interrupt=*/false);
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

    initTickSound();

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

    initTickSound();

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

// Truncate announcement text for diagnostic logs. Announcement text often
// contains user-entered content (bean brand, profile name, grinder model);
// log a length and a short preview rather than the full string.
static QString a11yLogPreview(const QString& text)
{
    constexpr int kMax = 40;
    if (text.size() <= kMax) return text;
    return text.left(kMax) + "...";
}

void AccessibilityManager::routeAnnouncement(const QString& text, bool interrupt)
{
    if (m_shuttingDown) return;

    const bool screenReader = isScreenReaderActive();
    const QString preview = a11yLogPreview(text);

    if (screenReader) {
        // Route to the OS screen reader. Suppress QTextToSpeech even if
        // ttsEnabled is true — that's the bug fix (no overlap with TalkBack /
        // VoiceOver). dispatchPlatformAnnouncement() handles the empty-window
        // null guard internally and logs path=dropped if it can't deliver.
        dispatchPlatformAnnouncement(text, interrupt);
        qInfo().noquote() << "[a11y] route path=platform isActive=true len=" << text.size()
                          << " preview=" << preview;
        return;
    }

    if (m_ttsEnabled) {
        // dispatchTtsAnnouncement() handles the m_tts null check internally.
        // Don't gate the call on m_tts here — tests override the virtual and
        // need it called even when m_tts is intentionally absent (the
        // TestSkipAudioInit ctor leaves it null on purpose).
        dispatchTtsAnnouncement(text, interrupt);
        qInfo().noquote() << "[a11y] route path=tts isActive=false len=" << text.size()
                          << " preview=" << preview;
        return;
    }

    qInfo().noquote() << "[a11y] route path=silent isActive=false ttsEnabled=" << m_ttsEnabled
                      << " len=" << text.size();
}

void AccessibilityManager::announce(const QString& text, bool interrupt)
{
    if (!m_enabled) return;
    routeAnnouncement(text, interrupt);
}

bool AccessibilityManager::isScreenReaderActive() const
{
#ifndef QT_NO_ACCESSIBILITY
    return QAccessible::isActive();
#else
    return false;
#endif
}

void AccessibilityManager::dispatchPlatformAnnouncement(const QString& text, bool assertive)
{
#ifndef QT_NO_ACCESSIBILITY
    // Prefer the focused window so AT-SPI / Narrator associate the event with
    // the active UIA tree. Fall back to scanning topLevelWindows() if there's
    // no focused window (very early startup, or backgrounded). Decenza opens
    // GHCSimulatorWindow as a separate top-level in debug builds, so the
    // first-match scan can pick the wrong target.
    QQuickWindow* target = qobject_cast<QQuickWindow*>(QGuiApplication::focusWindow());
    if (!target) {
        const auto windows = QGuiApplication::topLevelWindows();
        for (QWindow* w : windows) {
            if (auto* qw = qobject_cast<QQuickWindow*>(w)) {
                target = qw;
                break;
            }
        }
    }

    if (!target) {
        // qInfo (not qDebug) so dropped announcements show up in transcripts —
        // this is the case most likely to be reported as a "missed announcement".
        qInfo().noquote() << "[a11y] announce path=dropped reason=no-window len=" << text.size();
        return;
    }

    QAccessibleAnnouncementEvent event(target, text);
    event.setPoliteness(assertive ? QAccessible::AnnouncementPoliteness::Assertive
                                  : QAccessible::AnnouncementPoliteness::Polite);
    QAccessible::updateAccessibility(&event);
#else
    Q_UNUSED(text);
    Q_UNUSED(assertive);
#endif
}

void AccessibilityManager::dispatchTtsAnnouncement(const QString& text, bool interrupt)
{
    // Match the m_shuttingDown guard pattern used by every public method on
    // this class — ~DE1Device-style teardown can fire signals into here if a
    // queued announcement is delivered between m_shuttingDown=true and
    // m_tts=nullptr inside shutdown().
    if (m_shuttingDown || !m_tts) return;
    if (interrupt) {
        m_tts->stop();
    }
    m_tts->say(text);
}

void AccessibilityManager::announceLabel(const QString& text)
{
    if (m_shuttingDown || !m_enabled) return;

    // When a screen reader is active, route through it and skip the local
    // pitch/rate trick — TalkBack/VoiceOver handle their own prosody, and we
    // must not double-speak. Same fix as announce().
    if (isScreenReaderActive()) {
        dispatchPlatformAnnouncement(text, /*assertive=*/false);
        qInfo().noquote() << "[a11y] announceLabel path=platform isActive=true len=" << text.size()
                          << " preview=" << a11yLogPreview(text);
        return;
    }

    if (!m_ttsEnabled) return;

    if (m_tts) {
        // Sighted-user TTS path. Lower pitch + faster rate for labels so
        // they're distinguishable from interactive announcements.
        double originalPitch = m_tts->pitch();
        double originalRate = m_tts->rate();
        m_tts->setPitch(-0.3);
        m_tts->setRate(0.2);
        m_tts->say(text);
        // QTextToSpeech queues the settings, so restoring here applies to the
        // next say(), not the in-flight one.
        m_tts->setPitch(originalPitch);
        m_tts->setRate(originalRate);
    } else {
        // m_tts is only null in tests (TestSkipAudioInit). Route through the
        // same dispatcher the tests override — there's no pitch/rate dance
        // available without a real QTextToSpeech, but this preserves the
        // "TTS path was chosen" assertion for unit tests.
        dispatchTtsAnnouncement(text, /*interrupt=*/false);
    }
    qInfo().noquote() << "[a11y] announceLabel path=tts isActive=false len=" << text.size()
                      << " preview=" << a11yLogPreview(text);
}

void AccessibilityManager::playTick()
{
    if (m_shuttingDown || !m_enabled || !m_tickEnabled) return;

    initTickSound();

    int idx = m_tickSoundIndex - 1;  // Convert 1-4 to 0-3
    if (idx >= 0 && idx < 4 && m_tickSounds[idx] && m_tickSounds[idx]->status() == QSoundEffect::Ready) {
        m_tickSounds[idx]->play();
    }
}

void AccessibilityManager::toggleEnabled()
{
    if (m_shuttingDown) return;

    bool wasEnabled = m_enabled;
    Q_UNUSED(wasEnabled);
    setEnabled(!m_enabled);

    // setEnabled() already announced the new state. The previous code re-said
    // it here with stop() first to be more aggressive on the backdoor gesture;
    // we now reuse the same routed path with interrupt=true so a screen reader
    // gets an Assertive announcement and TTS gets stop()+say() — no overlap.
    routeAnnouncement(m_enabled ? QStringLiteral("Accessibility enabled")
                                : QStringLiteral("Accessibility disabled"),
                      /*interrupt=*/true);
}

void AccessibilityManager::setTranslationManager(TranslationManager* translationManager)
{
    if (m_translationManager) {
        disconnect(m_translationManager, nullptr, this, nullptr);
    }

    m_translationManager = translationManager;

    if (m_translationManager) {
        connect(m_translationManager, &TranslationManager::currentLanguageChanged,
                this, &AccessibilityManager::onLanguageChanged);

        // Set initial locale
        onLanguageChanged();
    }
}

void AccessibilityManager::onLanguageChanged()
{
    if (!m_tts || !m_translationManager) return;

    if (m_tts->state() != QTextToSpeech::Ready) {
        qDebug() << "TTS not ready yet, skipping locale update";
        return;
    }

    QString langCode = m_translationManager->currentLanguage();
    QLocale locale(langCode);

#ifdef Q_OS_ANDROID
    // On Android, just set the locale directly without calling availableLocales().
    // availableLocales() triggers getAvailableLocales() in Java which returns null
    // on some devices (e.g. Decent tablets), causing a fatal JNI abort that
    // C++ try/catch cannot intercept. setLocale() is safe — if the locale isn't
    // supported, Android TTS silently falls back to the system default.
    m_tts->setLocale(locale);
    qDebug() << "TTS locale set to:" << locale.name() << "for language:" << langCode;
#else
    // On desktop, check available locales before setting
    QList<QLocale> availableLocales = m_tts->availableLocales();
    if (availableLocales.isEmpty()) {
        qDebug() << "No TTS locales available — using system default";
        return;
    }

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
#endif
}
