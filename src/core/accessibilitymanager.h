#ifndef ACCESSIBILITYMANAGER_H
#define ACCESSIBILITYMANAGER_H

#include <atomic>
#include <type_traits>
#include <QObject>
#include <QPointer>
#include <QTextToSpeech>
#include <QSoundEffect>
#include "appsettings.h"
#include <QtQml/qqmlregistration.h>

#ifndef QT_NO_ACCESSIBILITY
#include <QAccessible>
#endif

class QQmlEngine;
class QJSEngine;
class TranslationManager;

class AccessibilityManager : public QObject
{
    Q_OBJECT

    // A compile-time-registered QML singleton. The macros are what put the type in the module's
    // generated Decenza.qmltypes — the only place qmllint, qmlcachegen and the language server
    // learn about C++ types. A runtime qmlRegisterSingletonInstance() would be invisible to all
    // three, which is why this replaced a setContextProperty.
    //
    // Registering the type is necessary but NOT sufficient: main.cpp must also call
    // qml_register_types_Decenza() explicitly, or no declarative type in this module reaches the
    // runtime registry at all. See the comment at that call site.
    QML_ELEMENT
    QML_SINGLETON
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
    // `parent` is deliberately NOT defaulted, and the static_assert below holds
    // it that way. Qt picks a QML_SINGLETON's construction mode by testing
    // is_default_constructible BEFORE it looks for a create() factory
    // (qtdeclarative/src/qml/qml/qqmlprivate.h:161-164), so a default-
    // constructible singleton silently gets `new T` and its create() is never
    // called at all. That shipped: build 3575 logged a second instance built by
    // the QML engine during engine.load(), which meant QML's AccessibilityManager
    // was Qt's orphan while main.cpp's wiring — the MCP server, announceCoaching —
    // pointed at a different object, each with its own live TTS engine.
    explicit AccessibilityManager(QObject *parent);
    ~AccessibilityManager();

    // QML_SINGLETON hooks. The engine does not create this object: main.cpp owns it on the stack
    // and wires it into the MCP server and the live-coaching signal path before QML exists, so
    // main publishes the instance and create() hands that same one back.
    static void setQmlInstance(AccessibilityManager *instance);
    static AccessibilityManager *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

#ifdef DECENZA_TESTING
    // Test-only ctor sentinel: skip QTextToSpeech / QSoundEffect construction
    // so unit tests don't depend on a real OS TTS engine. The base ctor's
    // QTextToSpeech::stateChanged handler emits qWarning("TTS error: ...")
    // when the platform has no engine available — banned by TESTING.md's
    // strict-warnings policy. Tests subclass AccessibilityManager and call
    // this overload to bypass audio init entirely.
    enum class TestSkipAudioInit { SkipAudio };
    explicit AccessibilityManager(TestSkipAudioInit, QObject *parent = nullptr);
#endif

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
    Q_INVOKABLE void announcePolite(const QString& text) { announce(text, false); }
    Q_INVOKABLE void announceAssertive(const QString& text) { announce(text, true); }
    // Coaching announcements (steam-coach voice): the user opted into these via
    // their own dedicated setting, so they bypass BOTH accessibility voice
    // gates — the master switch (m_enabled) and the Voice Announcements toggle
    // (m_ttsEnabled). Like playCaptureDing(), they are a general product
    // feature, not an accessibility cue. Prefers the screen reader when one is
    // active (no TTS overlap), otherwise speaks via TTS; logs each dispatch.
    void announceCoaching(const QString& text, bool interrupt = false);
    Q_INVOKABLE void announceLabel(const QString& text);  // Lower pitch + faster rate for non-interactive text
    Q_INVOKABLE void playTick();
    // Pleasant confirmation "ding" for weight auto-capture (milk/beans). Plays
    // independent of the accessibility-enabled gate — it is a general UI
    // confirmation, not an accessibility cue.
    Q_INVOKABLE void playCaptureDing();
    Q_INVOKABLE void toggleEnabled();  // For backdoor gesture

    // Make a machine-shaped string speakable: strip profile-file extensions, turn
    // separators into spaces, and expand units a screen reader would otherwise
    // spell out ("88C" -> "88 degrees Celsius", "18g" -> "18 grams").
    //
    // Lived in main.qml as root.cleanForSpeech() and was called from two pages by
    // creation-context lookup, which no tool could resolve. It is a pure function of
    // its argument with no UI state, so it belongs on the accessibility singleton —
    // typed at every call site, and unit-testable, which the QML version was not.
    Q_INVOKABLE QString cleanForSpeech(const QString &text) const;

    // Must be called before app shutdown to avoid TTS race conditions
    void shutdown();

    // Connect to TranslationManager to sync TTS language with app language
    void setTranslationManager(TranslationManager* translationManager);

public slots:
    void onLanguageChanged();

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

protected:
    // Test seams. Production implementations live in the .cpp; tests subclass
    // AccessibilityManager and override these to record calls without touching
    // real Qt accessibility / TTS state.
    virtual bool isScreenReaderActive() const;
    virtual void dispatchPlatformAnnouncement(const QString& text, bool assertive);
    virtual void dispatchTtsAnnouncement(const QString& text, bool interrupt);

    // The routing entry point for ACCESSIBILITY announcements (announce(),
    // announceLabel(), setEnabledImpl()). Decides between platform / TTS /
    // silent based on isScreenReaderActive() and m_ttsEnabled. NOTE:
    // announceCoaching() implements the same screen-reader-preference rule
    // directly (it must skip the m_ttsEnabled gate) — if the routing rule
    // changes here, update it there too. Internally guards
    // m_shuttingDown but does NOT check m_enabled — that's the caller's
    // responsibility. announce() and announceLabel() check m_enabled;
    // setEnabledImpl() (called by both setEnabled() and toggleEnabled())
    // intentionally bypasses it so the confirmation message plays even when
    // accessibility is being turned off.
    void routeAnnouncement(const QString& text, bool interrupt);

public:
    // Result of the one-time legacy-store carry-over. Public so the
    // pure static below is unit-testable.
    struct LegacyMigrationOutcome {
        bool alreadyDone = false;     // guard already set — no-op
        bool deferredOnError = false; // legacy read failed; guard NOT set
        bool guardStamped = false;    // completed; guard now set
        int  copied = 0;              // keys copied (absent in primary)
        int  legacyKeyCount = 0;      // total keys in the legacy store
    };
    // Pure, store-injected migration: copy-if-absent accessibility/*
    // from `legacy` into `primary`, guarded + idempotent. Copies happen
    // BEFORE the status check, so on a provable legacy read failure it
    // keeps whatever parsed but does NOT stamp the guard (a later run
    // retries; copy-if-absent makes the re-copy safe). Stores passed by
    // ref so tests need not touch real QSettings.
    static LegacyMigrationOutcome migrateAccessibilityLegacyStore(
        QSettings& primary, QSettings& legacy);

private:
    // The instance create() hands to the engine. Not owned here — main's stack object outlives
    // the engine, which is why create() pins CppOwnership.
    static AccessibilityManager *s_qmlInstance;

    // How many have been built. The app owns exactly one (main.cpp's stack
    // object, published via setQmlInstance); create() never constructs and the
    // type is not QML-creatable, so a second is a defect. See the constructor
    // for what the second one costs and why this counter exists at all.
    static std::atomic<int> s_instanceCount;

    void loadSettings();
    void saveSettings();
    // Constructs the real legacy QSettings("Decenza","DE1") and
    // delegates to migrateAccessibilityLegacyStore(m_settings, …),
    // logging the outcome. Called once from the normal ctor.
    void migrateLegacyStore();
    // Internal setter. Externally setEnabled() always announces; toggleEnabled()
    // calls this with announce=false to avoid double-speak (it then issues a
    // single Assertive announcement itself).
    void setEnabledImpl(bool enabled, bool announce);
    void initTts();
    void initTickSound();
    void initDingSound();

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
    QSoundEffect* m_dingSound = nullptr;  // Weight-capture confirmation ding
    AppSettings m_settings;

    TranslationManager* m_translationManager = nullptr;
};

// Qt tests is_default_constructible BEFORE HasSingletonFactory when choosing a
// QML_SINGLETON's construction mode (qqmlprivate.h:161-164). If this type ever
// becomes default-constructible again, Qt stops calling create() and silently
// builds its own second instance instead — with no compiler, qmllint or test
// complaint. tst_qmlregistration asserts every factory-bearing singleton carries
// one of these; docs/CLAUDE_MD/QML_GOTCHAS.md has the account.
static_assert(!std::is_default_constructible_v<AccessibilityManager>,
              "AccessibilityManager is a QML_SINGLETON with a create() factory: it must NOT be "
              "default-constructible, or Qt will 'new' its own instance and never call create(). "
              "Do not give the constructor a defaulted parent.");

#endif // ACCESSIBILITYMANAGER_H
