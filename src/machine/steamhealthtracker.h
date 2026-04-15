#pragma once

#include <QObject>
#include <QDateTime>
#include <QSettings>

class SteamDataModel;

struct SteamSessionSummary {
    QDateTime timestamp;
    double avgPressure = 0.0;      // bar, after 2s trim
    double peakPressure = 0.0;     // bar, after 2s trim
    double avgTemperature = 0.0;   // °C
    double peakTemperature = 0.0;  // °C
    int steamFlow = 0;             // setting at time of session (0.01 mL/s units)
    int steamTemperature = 0;      // setting at time of session (°C)
    int durationSeconds = 0;       // actual session duration
};

class SteamHealthTracker : public QObject {
    Q_OBJECT

public:
    // Baseline lifecycle state surfaced to QML so the settings/calibration
    // screen can show a meaningful message while trends are not yet
    // available, instead of going silent.
    //
    //   Empty                   — no sessions recorded (fresh install / manual reset)
    //   EstablishingInitial     — 1..4 sessions, collecting first baseline
    //   EstablishingAfterReset  — 1..4 sessions since an auto-reset trimmed
    //                             history after a detected pressure drop
    //                             (likely descale / steam-wand clean)
    //   Ready                   — 5+ sessions; trend detection is active
    enum BaselineState {
        Empty,
        EstablishingInitial,
        EstablishingAfterReset,
        Ready,
    };
    Q_ENUM(BaselineState)

    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY sessionHistoryChanged)
    Q_PROPERTY(double baselinePressure READ baselinePressure NOTIFY sessionHistoryChanged)
    Q_PROPERTY(double baselineTemperature READ baselineTemperature NOTIFY sessionHistoryChanged)
    Q_PROPERTY(double currentPressure READ currentPressure NOTIFY sessionHistoryChanged)
    Q_PROPERTY(double currentTemperature READ currentTemperature NOTIFY sessionHistoryChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY sessionHistoryChanged)
    Q_PROPERTY(double pressureThreshold READ pressureThreshold NOTIFY sessionHistoryChanged)
    Q_PROPERTY(double temperatureThreshold READ temperatureThreshold CONSTANT)
    // Number of sessions required before hasData flips true.
    Q_PROPERTY(int minSessionsForTrend READ minSessionsForTrend CONSTANT)
    Q_PROPERTY(BaselineState baselineState READ baselineState NOTIFY sessionHistoryChanged)

    explicit SteamHealthTracker(QObject* parent = nullptr);

    int sessionCount() const { return m_sessionCount; }
    double baselinePressure() const { return m_baselinePressure; }
    double baselineTemperature() const { return m_baselineTemperature; }
    double currentPressure() const { return m_currentPressure; }
    double currentTemperature() const { return m_currentTemperature; }
    bool hasData() const { return m_sessionCount >= MIN_SESSIONS_FOR_TREND; }
    double pressureThreshold() const { return m_baselinePressure > 0 ? qMin(m_baselinePressure * PRESSURE_WARN_MULTIPLIER, PRESSURE_HARD_LIMIT) : PRESSURE_HARD_LIMIT; }
    double temperatureThreshold() const { return TEMPERATURE_THRESHOLD; }
    double trendProgressThreshold() const { return TREND_PROGRESS_THRESHOLD; }
    int minSessionsForTrend() const { return MIN_SESSIONS_FOR_TREND; }
    BaselineState baselineState() const;

    // Called per BLE sample during steaming (live threshold checks)
    void onSample(double pressure, double temperature);

    // Called when steaming phase ends — runs post-session analysis and trend detection
    void onSessionComplete(SteamDataModel* model, int steamFlowSetting, int steamTempSetting);

    // Reset live warning flags for a new session
    void resetSession();

    // Clear all stored session history (e.g., after descaling, or manual reset)
    Q_INVOKABLE void clearHistory();

signals:
    // Live warnings (emitted at most once per session)
    void pressureTooHigh();
    void temperatureTooHigh();

    // Post-session warnings
    void descaleWarning();
    void temperatureWarning(const QString& message);
    void scaleBuildupWarning(const QString& message);

    void sessionHistoryChanged();

private:
    // Normalize pressure to REFERENCE_FLOW for cross-setting comparison.
    // Clamped to 0.1 bar minimum to prevent negative baselines at extreme flows.
    double normalizePressure(double avgPressure, int steamFlow) const;

    QList<SteamSessionSummary> loadHistory() const;
    void saveHistory(const QList<SteamSessionSummary>& history);
    void checkTrend(QList<SteamSessionSummary>& history, int steamFlow, int steamTemp);
    void updateCachedStats(const QList<SteamSessionSummary>& history, int steamTemp);

    QSettings m_settings;
    int m_sessionCount = 0;
    int m_lastWarnedSession = -99;  // Warning cooldown: session count when last warned

    // Cached baseline/current stats (for QML property access without JSON parse)
    double m_baselinePressure = 0.0;
    double m_baselineTemperature = 0.0;
    double m_currentPressure = 0.0;
    double m_currentTemperature = 0.0;
    int m_lastSteamFlow = 0;       // settings used for cached stats
    int m_lastSteamTemp = 0;

    // Live warning flags (reset per session)
    bool m_pressureWarningEmitted = false;
    bool m_temperatureWarningEmitted = false;

    // Auto-reset two-step confirmation (persisted across restart).
    // A single low-pressure session sets m_pendingAutoReset; the trim only
    // fires when the *next* session also drops. Prevents one-off outliers
    // (partial-full boiler, normalization edge cases) from wiping history.
    bool m_pendingAutoReset = false;

    // True after auto-reset has trimmed history, cleared once sessionCount
    // reaches MIN_SESSIONS_FOR_TREND. Drives the "Establishing new, improved
    // baseline" state so the UI can explain the transient empty state.
    bool m_establishingAfterReset = false;

    // Thresholds
    static constexpr double PRESSURE_HARD_LIMIT = 8.0;      // bar — absolute safety net for live onSample() check
    static constexpr double PRESSURE_WARN_MULTIPLIER = 3.0;  // warn when pressure reaches this multiple of baseline
    static constexpr double TEMPERATURE_THRESHOLD = 180.0;   // °C
    static constexpr int CLOG_SAMPLE_THRESHOLD = 10;         // samples exceeding threshold
    static constexpr int MIN_SAMPLES_FOR_ANALYSIS = 30;      // minimum sample count
    static constexpr int MIN_DURATION_FOR_ANALYSIS = 20;     // minimum seconds — filters aborted cycles
    static constexpr int MIN_SESSIONS_FOR_TREND = 5;         // minimum comparable sessions
    static constexpr int MAX_HISTORY_SIZE = 150;             // sessions to keep
    static constexpr double TREND_PROGRESS_THRESHOLD = 0.6;   // warn at 60% of the way from baseline to warn level
    static constexpr double AUTO_RESET_DROP_THRESHOLD = 0.3;  // auto-reset baseline if drop >= 30% of range (likely descale) — must persist across 2 consecutive sessions
    static constexpr int AUTO_RESET_KEEP_SESSIONS = 2;       // sessions to keep after auto-reset (the two consecutive triggering sessions; older scaled-up sessions are discarded)
    static constexpr double TRIM_SECONDS = 2.0;              // skip first 2s of samples
    static constexpr int REFERENCE_FLOW = 150;               // 1.5 mL/s — normalization reference
    static constexpr double PRESSURE_PER_FLOW_UNIT = 0.012;  // bar per 0.01 mL/s (from RO-water baseline data)
    static constexpr double CLEAN_NORMALIZED_PRESSURE = 3.0;  // bar at reference flow on a clean machine (conservative, covers 110V/220V variance)
    static constexpr int WARN_COOLDOWN_SESSIONS = 5;         // re-warn at most every N sessions
};
