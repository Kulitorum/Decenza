#include "accessibilitymanager.h"
#include "translationmanager.h"
#include "settings.h"
#include <QDebug>
#include <QCoreApplication>
#include <QLocale>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QWindow>

#ifndef QT_NO_ACCESSIBILITY
#include <QAccessible>
#endif

#include "core/accessibilitylogging.h"
#include "core/screenreaderprobe.h"

std::atomic<int> AccessibilityManager::s_instanceCount{0};

AccessibilityManager::AccessibilityManager(QObject *parent)
    : QObject(parent)
    // m_settings is the app-wide store (see appsettings.h), matching every
    // settings_*.cpp domain class. Was QSettings("Decenza","DE1") — an isolated
    // third store that broke accessibility backup/restore and survived factory
    // reset. Existing values are carried over by migrateLegacyStore().
{
    // Exactly one of these should ever exist. Builds 3574 and 3575 had two, and
    // this counter is what named the second — it fired on 3575 and the cause is
    // now known and closed: the QML engine was constructing its own.
    //
    // Qt tests is_default_constructible BEFORE it looks for a create() factory
    // when it picks a QML_SINGLETON's construction mode (qqmlprivate.h:161-164),
    // and this class's constructor took `QObject *parent = nullptr`. So Qt chose
    // `new T` (:190), create() was never called, and the instance main.cpp
    // main.cpp published was ignored — QML talked to Qt's orphan
    // while the MCP server and announceCoaching held main.cpp's object, each with
    // its own live QTextToSpeech. Closed by moving the registration to a
    // QML_FOREIGN wrapper (AccessibilityManagerForeign, contextsingletons_qml.h),
    // which is what Qt documents for exposing an object the app already owns and
    // which cannot hit this at all — a foreign type takes Qt's FactoryWrapper
    // branch before constructibility is ever tested.
    // docs/CLAUDE_MD/QML_GOTCHAS.md has the full account.
    //
    // So this is now a REGRESSION guard for a closed bug rather than a hunt for an
    // open one, and that is why it stays: the failure it catches is silent, and it
    // costs an int.
    //
    // Never decremented, deliberately: the production object is main.cpp's stack
    // object and lives for the process, so there is no construct-destroy-construct
    // path to confuse this with a second live instance. The test-only
    // TestSkipAudioInit constructor does not increment at all — tests legitimately
    // build many, and counting them would bury the real signal in permanent noise.
    //
    // Atomic is now belt-and-braces rather than necessary — the construction that
    // caused this was Qt's, on the GUI thread during engine.load() — but it costs
    // nothing and a counter that races would be a poor witness.
    //
    // Kept permanently rather than as a probe: it asserts an invariant that
    // holds today, costs an int, and says nothing at all when the app is well.
    // That is the difference between this and the #582 diagnostics, which
    // narrated a healthy startup 14 lines at a time.
    const int instanceOrdinal = ++s_instanceCount;
    if (instanceOrdinal > 1) {
        A11Y_WARN_STDERR("Lifetime",
            QStringLiteral("instance %1 constructed — the app owns exactly one (main.cpp, "
                           "handed to QML through AccessibilityManagerForeign). This one "
                           "builds a second TTS "
                           "engine that QML will never reach. The known cause was a "
                           "default-constructible QML_SINGLETON letting Qt build its own "
                           "(see QML_GOTCHAS.md); check that first.")
                .arg(instanceOrdinal));
    }

    migrateLegacyStore();
    loadSettings();
    initTts();
    if (m_enabled && m_tickEnabled)
        initTickSound();
    // The capture ding is a general UI cue (not accessibility-gated), so pre-load
    // it at startup regardless of accessibility/tick state — otherwise the very
    // first capture would play nothing while the sound is still loading.
    initDingSound();
}

#ifdef DECENZA_TESTING
AccessibilityManager::AccessibilityManager(TestSkipAudioInit, QObject *parent)
    : QObject(parent)
{
    // Deliberately skip migrateLegacyStore()/loadSettings() so tests
    // don't inherit whatever the dev machine has persisted. Member
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

    A11Y_LOG_STDERR("Lifetime", QStringLiteral("shutting down"));

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

// Pure, store-injected so it is unit-testable without touching the
// machine's real QSettings (caller passes the stores by reference).
// Matches the static-helper testability pattern used elsewhere
// (e.g. ShotHistoryStorage::reconcileVisualizerLinksStatic).
AccessibilityManager::LegacyMigrationOutcome
AccessibilityManager::migrateAccessibilityLegacyStore(QSettings& primary,
                                                      QSettings& legacy)
{
    LegacyMigrationOutcome out;

    constexpr const char* kMigratedFlag = "accessibility/_migratedFromLegacyV1";
    if (primary.value(kMigratedFlag, false).toBool()) {
        out.alreadyDone = true;
        return out;
    }

    static const char* kKeys[] = {
        "accessibility/enabled",
        "accessibility/ttsEnabled",
        "accessibility/tickEnabled",
        "accessibility/tickSoundIndex",
        "accessibility/tickVolume",
        "accessibility/extractionAnnouncementsEnabled",
        "accessibility/extractionAnnouncementInterval",
        "accessibility/extractionAnnouncementMode",
    };

    legacy.sync();  // force a read so status() is meaningful below
    const QSettings::Status legacyStatus = legacy.status();
    out.legacyKeyCount = static_cast<int>(legacy.allKeys().size());

    // copy-if-absent: never clobber a newer primary value (a re-run, or
    // a user who already changed a setting post-migration).
    for (const char* key : kKeys) {
        if (legacy.contains(QLatin1String(key))
            && !primary.contains(QLatin1String(key))) {
            primary.setValue(QLatin1String(key), legacy.value(QLatin1String(key)));
            ++out.copied;
        }
    }

    if (legacyStatus != QSettings::NoError) {
        // The legacy read provably failed (corrupt INI / access error).
        // Whatever keys parsed were already copied above (copy-if-absent
        // makes re-copying on the retry safe); we just don't stamp the
        // one-shot guard on a read we know failed — retry next launch
        // instead of permanently losing a user's accessibility settings
        // to a transient unreadable store. (NativeFormat on Windows/
        // macOS can't always prove failure: there status() returns
        // NoError despite a real failure and the guard IS stamped below;
        // legacyKeyCount is logged purely as a post-hoc breadcrumb, not
        // a mitigation.)
        out.deferredOnError = true;
        return out;
    }

    primary.setValue(kMigratedFlag, true);
    primary.sync();
    out.guardStamped = true;
    return out;
}

void AccessibilityManager::migrateLegacyStore()
{
    // One-time: carry accessibility/* from the old isolated
    // QSettings("Decenza","DE1") store into the primary store.
    //
    // This function does not remove the legacy store, but it no longer survives
    // either: once this migration has stamped its guard, runSettingsStoreMigrationOnce()
    // (settingsstoremigration.cpp, run from main() before this class is constructed)
    // destroys it on a subsequent launch. factoryReset() also clears it, for
    // installations that have not reached that point.
    QSettings legacy(QStringLiteral("Decenza"), QStringLiteral("DE1"));
    const LegacyMigrationOutcome r =
        migrateAccessibilityLegacyStore(m_settings, legacy);

    if (r.alreadyDone)
        return;
    if (r.deferredOnError) {
        A11Y_WARN_STDERR("Migration",
            QStringLiteral("legacy store unreadable — deferring migration, guard NOT set"));
        return;
    }
    // qInfo (not qDebug) + legacy key count so a support log can tell
    // "nothing to migrate" (legacyKeyCount==0) apart from "all already
    // present" (copied==0 && legacyKeyCount>0) — an irreversible
    // one-time migration deserves a durable, unambiguous breadcrumb.
    A11Y_INFO_STDERR("Migration",
        QStringLiteral("migrated %1 of %2 legacy accessibility key(s) into the primary store")
            .arg(r.copied).arg(r.legacyKeyCount));
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
    // Idempotent, like initTickSound() and initDingSound() above. A second run
    // would leak the first QTextToSpeech (parented, so it lives to shutdown)
    // AND leave its stateChanged handler connected, so every Ready would run
    // onLanguageChanged() twice.
    //
    // This is not hypothetical: session 2026-08-30T14:05:45 on the SM-X210 shows
    // the whole block twice, at 0.908 s and again at 2.034 s, from a build whose
    // only construction site is main.cpp's stack AccessibilityManager. WHO calls
    // it twice is not established — the singleton's create() never constructs,
    // and the type is not QML-creatable — so the warning below is here to name
    // the second caller in the next log rather than to assert a cause.
    if (m_tts) {
        A11Y_WARN_STDERR("Tts",
            QStringLiteral("initTts() called again — TTS is already initialised; ignoring. "
                           "The first call built the engine and connected its stateChanged "
                           "handler."));
        return;
    }

    auto engines = QTextToSpeech::availableEngines();
    A11Y_LOG_STDERR("Tts", QStringLiteral("available engines: %1").arg(engines.join(u", ")));

    // On Android, use "android" engine which delegates to system TTS settings
    // This respects the user's preferred engine and voice from Android preferences
#ifdef Q_OS_ANDROID
    if (engines.contains("android")) {
        m_tts = new QTextToSpeech("android", this);
        A11Y_LOG_STDERR("Tts", QStringLiteral("using Android system TTS"));
    } else {
        m_tts = new QTextToSpeech(this);
    }
#else
    m_tts = new QTextToSpeech(this);
#endif

    connect(m_tts, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State state) {
        A11Y_LOG_STDERR("Tts", QStringLiteral("state changed: %1").arg(int(state)));
        if (state == QTextToSpeech::Error) {
            A11Y_WARN_STDERR("Tts", QStringLiteral("error: %1").arg(m_tts->errorString()));
        } else if (state == QTextToSpeech::Ready) {
            A11Y_LOG_STDERR("Tts", QStringLiteral("ready"));
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

void AccessibilityManager::initDingSound()
{
    if (m_dingSound)
        return;  // already loaded
    // Weight-capture confirmation ding — near-full volume (0.9), independent of
    // the tick / accessibility volume since it is a general UI cue.
    m_dingSound = new QSoundEffect(this);
    m_dingSound->setSource(QUrl("qrc:/sounds/ding.wav"));
    m_dingSound->setVolume(0.9);
}

void AccessibilityManager::playCaptureDing()
{
    if (m_shuttingDown) return;
    initDingSound();  // no-op after the first call / startup pre-load
    // Gate on Ready like playTick() — playing during the async load window (or on
    // a failed load) is a silent no-op, so don't bother. The startup pre-load
    // means this is almost always Ready by the first capture.
    if (m_dingSound && m_dingSound->status() == QSoundEffect::Ready)
        m_dingSound->play();
}

void AccessibilityManager::setEnabled(bool enabled)
{
    setEnabledImpl(enabled, /*announce=*/true);
}

void AccessibilityManager::setEnabledImpl(bool enabled, bool announce)
{
    if (m_shuttingDown || m_enabled == enabled) return;
    m_enabled = enabled;
    saveSettings();
    emit enabledChanged();

    A11Y_LOG_STDERR("Settings",
        QStringLiteral("accessibility %1").arg(m_enabled ? u"enabled" : u"disabled"));

    if (!announce) return;

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
        A11Y_INFO_STDERR("Route", QStringLiteral("path=platform isActive=true len=%1 preview=%2")
                                      .arg(text.size()).arg(preview));
        return;
    }

    if (m_ttsEnabled) {
        // dispatchTtsAnnouncement() handles the m_tts null check internally.
        // Don't gate the call on m_tts here — tests override the virtual and
        // need it called even when m_tts is intentionally absent (the
        // TestSkipAudioInit ctor leaves it null on purpose).
        dispatchTtsAnnouncement(text, interrupt);
        A11Y_INFO_STDERR("Route", QStringLiteral("path=tts isActive=false len=%1 preview=%2")
                                      .arg(text.size()).arg(preview));
        return;
    }

    A11Y_INFO_STDERR("Route", QStringLiteral("path=silent isActive=false ttsEnabled=%1 len=%2")
                                  .arg(m_ttsEnabled).arg(text.size()));
}

void AccessibilityManager::announce(const QString& text, bool interrupt)
{
    if (!m_enabled) return;
    routeAnnouncement(text, interrupt);
}

void AccessibilityManager::announceCoaching(const QString& text, bool interrupt)
{
    // Deliberately bypasses BOTH accessibility voice gates — m_enabled (the
    // master switch) AND m_ttsEnabled (the "Voice Announcements" toggle).
    // Coaching voice has its own opt-in (Settings.app.steamCoachAudioEnabled,
    // gated at the emitter) and must speak with all accessibility settings
    // off — like playCaptureDing(), it is a product feature, not an
    // accessibility cue. Going through routeAnnouncement() would silently
    // re-gate on m_ttsEnabled (the exact silent-dead-voice defect class this
    // entry point exists to fix), so route directly: prefer the screen reader
    // when active (no TTS overlap — same rule as routeAnnouncement), else TTS.
    if (m_shuttingDown) return;
    const QString preview = a11yLogPreview(text);
    if (isScreenReaderActive()) {
        dispatchPlatformAnnouncement(text, interrupt);
        A11Y_INFO_STDERR("Route", QStringLiteral("path=platform coaching=true len=%1 preview=%2")
                                      .arg(text.size()).arg(preview));
        return;
    }
    // dispatchTtsAnnouncement() handles the m_tts null check internally (and
    // tests override the virtual with m_tts intentionally absent).
    dispatchTtsAnnouncement(text, interrupt);
    A11Y_INFO_STDERR("Route", QStringLiteral("path=tts coaching=true len=%1 preview=%2")
                                  .arg(text.size()).arg(preview));
}

bool AccessibilityManager::isScreenReaderActive() const
{
#ifndef QT_NO_ACCESSIBILITY
    // Ask the platform whether a reader is actually RUNNING before falling back
    // to Qt's "an assistive client attached" flag. See screenreaderprobe.h —
    // the two are different questions, and answering routing with the second
    // one is what left this app silent on a Mac with VoiceOver off.
    if (const std::optional<bool> platform = decenzaPlatformScreenReaderActive())
        return *platform;
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
        A11Y_INFO_STDERR("Route", QStringLiteral("announce path=dropped reason=no-window len=%1")
                                      .arg(text.size()));
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
        A11Y_INFO_STDERR("Route", QStringLiteral("announceLabel path=platform isActive=true len=%1 preview=%2")
                                      .arg(text.size()).arg(a11yLogPreview(text)));
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
    A11Y_INFO_STDERR("Route", QStringLiteral("announceLabel path=tts isActive=false len=%1 preview=%2")
                                  .arg(text.size()).arg(a11yLogPreview(text)));
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

QString AccessibilityManager::cleanForSpeech(const QString &text) const
{
    if (text.isEmpty())
        return QString();

    // A direct port of main.qml's cleanForSpeech(), in the same order — the order
    // matters, because the unit expansions insert spaces that the final whitespace
    // collapse then normalises. QRegularExpression's "\\1" is JS's "$1".
    static const QRegularExpression profileExt(QStringLiteral("\\.(json|tcl|txt)$"),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression separators(QStringLiteral("[_-]"));
    // Both the °C/°F symbols and a bare "88C" (common in Celsius-authored profile
    // names like gagne_88C) always denote their own unit regardless of the display
    // setting, so they map literally. The app's own converted read-outs always emit
    // an explicit "°F"/"°C", never a bare number+C, so this never mislabels a
    // converted value.
    static const QRegularExpression bareCelsius(QStringLiteral("(\\d)\\s*C\\b"));
    static const QRegularExpression millilitres(QStringLiteral("(\\d)\\s*ml\\b"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression grams(QStringLiteral("(\\d)\\s*g\\b"));
    static const QRegularExpression bar(QStringLiteral("(\\d)\\s*bar\\b"),
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression seconds(QStringLiteral("(\\d)\\s*s\\b"));
    static const QRegularExpression percent(QStringLiteral("(\\d)\\s*%"));
    static const QRegularExpression runsOfSpace(QStringLiteral("\\s+"));

    QString cleaned = text;
    cleaned.remove(profileExt);
    cleaned.replace(separators, QStringLiteral(" "));
    cleaned.replace(QStringLiteral("°F"), QStringLiteral(" degrees Fahrenheit"));
    cleaned.replace(QStringLiteral("°C"), QStringLiteral(" degrees Celsius"));
    cleaned.replace(bareCelsius, QStringLiteral("\\1 degrees Celsius"));
    cleaned.replace(millilitres, QStringLiteral("\\1 milliliters"));
    cleaned.replace(grams, QStringLiteral("\\1 grams"));
    cleaned.replace(bar, QStringLiteral("\\1 bar"));
    cleaned.replace(seconds, QStringLiteral("\\1 seconds"));
    cleaned.replace(percent, QStringLiteral("\\1 percent"));
    cleaned.replace(runsOfSpace, QStringLiteral(" "));
    return cleaned.trimmed();
}

void AccessibilityManager::toggleEnabled()
{
    if (m_shuttingDown) return;

    // Skip setEnabled()'s own announcement and emit a single Assertive one
    // here. Otherwise both fire on the platform path (TalkBack would hear
    // Polite + Assertive back to back — no platform-level cancellation
    // exists between QAccessibleAnnouncementEvent dispatches). On the TTS
    // path, interrupt=true maps to stop()+say() so the announcement always
    // wins out — appropriate for a backdoor-gesture confirmation.
    setEnabledImpl(!m_enabled, /*announce=*/false);
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
        A11Y_LOG_STDERR("Tts", QStringLiteral("not ready yet, skipping locale update"));
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
    A11Y_LOG_STDERR("Tts", QStringLiteral("locale set to %1 for language %2")
                               .arg(locale.name(), langCode));
#else
    // On desktop, check available locales before setting
    QList<QLocale> availableLocales = m_tts->availableLocales();
    if (availableLocales.isEmpty()) {
        A11Y_LOG_STDERR("Tts", QStringLiteral("no locales available — using system default"));
        return;
    }

    bool found = false;
    for (const QLocale& available : availableLocales) {
        if (available.language() == locale.language()) {
            m_tts->setLocale(available);
            A11Y_LOG_STDERR("Tts", QStringLiteral("locale set to %1 for language %2")
                                       .arg(available.name(), langCode));
            found = true;
            break;
        }
    }

    if (!found) {
        A11Y_LOG_STDERR("Tts", QStringLiteral("locale not available for %1 — using system default")
                                   .arg(langCode));
    }
#endif
}
