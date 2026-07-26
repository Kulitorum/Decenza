#pragma once

#include <QObject>
#include "appsettings.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>
#include <QPair>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>

class Settings;

// Auto flow calibration + SAW (stop-at-weight) learning. Split from Settings as
// part of the Tier 3 domain decomposition (issue #860). Holds a non-owning
// pointer to Settings so sawLearnedLag() / getExpectedDrip() can read the
// current scaleType() without a public-API change. The owner pointer is used
// ONLY for that single lookup — no other Settings surface is reached through
// it.
//
// SCALE KEY RESOLUTION — read this before adding a SAW call site.
//
// Every per-scale SAW read takes an OPTIONAL scaleType. Leave it empty and the
// class resolves it itself, via currentScaleType(); that is the correct answer
// and the one every other consumer gets. Pass a value only when you genuinely
// mean a specific pool, which is true in exactly two places: the learning path
// (main.cpp latches the key at shot start so a scale swapped mid-shot still
// learns under the key that made the prediction) and the MCP
// reset_saw_learning_for_profile tool (the user names a scale).
//
// This is deliberately the inverse of the older design, where the key was a
// required parameter every caller derived for itself. Four consumers derived it
// three different ways: the learner wrote one pool while the Calibration tab
// displayed another and the AI advisor reported a third, each invisible from the
// others. Making the resolved answer the DEFAULT means a new call site is right
// by omission, and an explicit key now reads as a deliberate choice rather than
// as boilerplate.
class SettingsCalibration : public QObject {
    Q_OBJECT

    // Flow calibration
    Q_PROPERTY(double flowCalibrationMultiplier READ flowCalibrationMultiplier WRITE setFlowCalibrationMultiplier NOTIFY flowCalibrationMultiplierChanged)
    Q_PROPERTY(bool autoFlowCalibration READ autoFlowCalibration WRITE setAutoFlowCalibration NOTIFY autoFlowCalibrationChanged)
    Q_PROPERTY(int perProfileFlowCalVersion READ perProfileFlowCalVersion NOTIFY perProfileFlowCalibrationChanged)

    // SAW (Stop-at-Weight) learning
    Q_PROPERTY(double sawLearnedLag READ sawLearnedLag NOTIFY sawLearnedLagChanged)

public:
    explicit SettingsCalibration(Settings* owner, QObject* parent = nullptr);

    // Flow calibration
    double flowCalibrationMultiplier() const;
    void setFlowCalibrationMultiplier(double multiplier);
    bool autoFlowCalibration() const;
    void setAutoFlowCalibration(bool enabled);
    double profileFlowCalibration(const QString& profileFilename) const;
    bool setProfileFlowCalibration(const QString& profileFilename, double multiplier);
    Q_INVOKABLE void clearProfileFlowCalibration(const QString& profileFilename);
    Q_INVOKABLE double effectiveFlowCalibration(const QString& profileFilename) const;
    Q_INVOKABLE bool hasProfileFlowCalibration(const QString& profileFilename) const;
    QJsonObject allProfileFlowCalibrations() const;
    int perProfileFlowCalVersion() const { return m_perProfileFlowCalVersion; }

    // Auto flow calibration batch accumulator: stores pending ideal values per profile
    // until a full batch (5 shots) is collected, then the median is used to update C.
    QVector<double> flowCalPendingIdeals(const QString& profileFilename) const;
    void appendFlowCalPendingIdeal(const QString& profileFilename, double ideal);
    void clearFlowCalPendingIdeals(const QString& profileFilename);

    // Reset all per-profile flow calibrations to empty (used by one-shot migrations).
    void resetAllProfileFlowCalibrations();

    // SAW (Stop-at-Weight) learning
    double sawLearnedLag() const;  // Average lag for display in QML (calculated from drip/flow)
    double getExpectedDrip(double currentFlowRate) const;  // Predicts drip based on flow and history

    // Per-(profile, scale) variant of sawLearnedLag — falls back to global bootstrap /
    // per-scale data when the pair has not yet graduated. Pass empty profile for the
    // legacy global-pool path. Empty scaleType = resolve it (see class comment).
    Q_INVOKABLE double sawLearnedLagFor(const QString& profileFilename,
                                        const QString& scaleType = QString()) const;
    double getExpectedDripFor(const QString& profileFilename, const QString& scaleType,
                              double currentFlowRate) const;
    QList<QPair<double, double>> sawLearningEntriesFor(const QString& profileFilename,
                                                       const QString& scaleType,
                                                       int maxEntries) const;

    // Reports which model the read path uses for (profile, scale).
    Q_INVOKABLE QString sawModelSource(const QString& profileFilename,
                                       QString scaleType = QString()) const;

    // Learning is the one path that must NOT resolve: main.cpp latches the key at
    // shot start and passes it here ~40 s later, so that a scale swapped mid-shot
    // still trains the pool that made the prediction. Resolving live here would
    // reintroduce exactly that bug, so scaleType stays required.
    void addSawLearningPoint(double drip, double flowRate, QString scaleType, double overshoot,
                             const QString& profileFilename = QString());
    Q_INVOKABLE void resetSawLearning();
    Q_INVOKABLE void resetSawLearningForProfile(const QString& profileFilename, const QString& scaleType);

    // Export/import the whole SAW learning state (all four saw/* keys) for
    // device transfer / backup — SAW learning is scale+profile specific and
    // portable across devices, unlike the machine-specific flow calibration.
    // Empty object = nothing learned.
    QJsonObject sawLearningExport() const;
    void sawLearningImport(const QJsonObject& o);

    // Per-pair committed history (storage helpers; mostly for tests + bootstrap recompute).
    QJsonArray perProfileSawHistory(const QString& profileFilename,
                                    const QString& scaleType = QString()) const;
    QJsonObject allPerProfileSawHistory() const;

    // Per-pair pending batch accumulator (5 entries before committing the batch median).
    QJsonArray sawPendingBatch(const QString& profileFilename,
                               const QString& scaleType = QString()) const;

    // Global bootstrap lag for new (profile, scale) pairs without graduated history.
    double globalSawBootstrapLag(const QString& scaleType) const;
    void setGlobalSawBootstrapLag(const QString& scaleType, double lag);

    // Per-scale BLE sensor lag (seconds). Used as first-shot SAW default before learning kicks in.
    static double sensorLag(const QString& scaleType);

    // SAW convergence detection helper
    bool isSawConverged(QString scaleType) const;

    // Drop all in-memory caches so the next read pulls from QSettings. Called
    // by Settings::factoryReset() after clearing the underlying store.
    void invalidateCache();

    // One-time migration: rewrite SAW storage (global pool, per-pair history +
    // batch, global bootstrap) keyed on legacy display-name scale types to
    // canonical type-ids, merging colliding buckets without data loss. Invoked
    // once from Settings init under the scale/typeIdsMigrated flag.
    void migrateScaleTypeIds();

    // Returns SAW learning entries filtered by scale type (most recent first).
    // Used by WeightProcessor to snapshot learning data at shot start.
    QList<QPair<double, double>> sawLearningEntries(QString scaleType, int maxEntries) const;

    // Reports the type-id of the scale currently wired into the shot path, or an
    // empty string when none is connected. Installed once by MachineState, which is
    // the only object that tracks the serving scale.
    //
    // A PULL provider rather than a pushed value on purpose: the serving scale
    // changes on device events at a dozen call sites and its connected state changes
    // independently of that, so anything pushed would need re-pushing from both and
    // would go stale the first time one was missed. A closure reading live state
    // cannot be stale. It returns raw device identity only — the canonical-vocabulary
    // test and the saved-scale fallback are POLICY and stay here, so there is exactly
    // one implementation of the SAW key rule.
    //
    // The provider must not outlive its captured object; MachineState clears it in
    // its destructor.
    void setServingScaleTypeProvider(std::function<QString()> provider);

    // The SAW pool key for right now: the serving scale when it is real (connected
    // AND in the canonical ScaleTypeIds vocabulary), otherwise the saved primary,
    // normalized. FlowScale reports "flow" and is permanently isConnected(), so the
    // vocabulary check is what stops every scale-less shot opening a "flow" pool and
    // making sensorLag() warn about an unknown type on every cycle. Public because
    // MachineState::activeScaleType() forwards here for the QML property.
    QString currentScaleType() const;

signals:
    void flowCalibrationMultiplierChanged();
    void autoFlowCalibrationChanged();
    void perProfileFlowCalibrationChanged();
    void sawLearnedLagChanged();

    // Emitted by resetSawLearning() so Settings can forward to SettingsBrew
    // (hot-water SAW offset reset) via a connect-based wire — sub-objects do
    // not call into other domains directly.
    void sawLearningResetRequested();

private:
    void ensureSawCacheLoaded() const;
    void savePerProfileFlowCalMap(const QJsonObject& map);

    // INVARIANT: all writes route through savePerProfileSawHistoryMap() /
    // savePerProfileSawBatchMap() to keep the cache and QSettings in sync.
    QJsonObject loadPerProfileSawHistoryMap() const;
    void savePerProfileSawHistoryMap(const QJsonObject& map);
    QJsonObject loadPerProfileSawBatchMap() const;
    void savePerProfileSawBatchMap(const QJsonObject& map);
    static QString sawPairKey(const QString& profileFilename, const QString& scaleType);
    void addSawPerPairEntry(double drip, double flowRate, const QString& scaleType,
                            double overshoot, const QString& profileFilename);
    void recomputeGlobalSawBootstrap(const QString& scaleType);

    // Every optional-scaleType entry point funnels through here: an explicit key is
    // normalized and used as given, an empty one resolves. One helper so "empty means
    // resolve" cannot be implemented three subtly different ways.
    QString resolveScaleKey(const QString& explicitKey) const;

    Settings* m_owner = nullptr;  // Non-owning; used ONLY for currentScaleType() lookup.
    std::function<QString()> m_servingScaleType;  // See setServingScaleTypeProvider().
    mutable AppSettings m_settings;

    // SAW learning history cache (avoids re-parsing JSON from QSettings on every weight sample)
    mutable QJsonArray m_sawHistoryCache;
    mutable bool m_sawHistoryCacheDirty = true;
    mutable int m_sawConvergedCache = -1;  // -1 = unknown, 0 = no, 1 = yes
    mutable QString m_sawConvergedScaleType;

    int m_perProfileFlowCalVersion = 0;  // Bumped on per-profile calibration changes to trigger QML rebind
    mutable QJsonObject m_perProfileFlowCalCache;
    mutable bool m_perProfileFlowCalCacheValid = false;

    mutable QJsonObject m_perProfileSawHistoryCache;
    mutable bool m_perProfileSawHistoryCacheValid = false;
    mutable QJsonObject m_perProfileSawBatchCache;
    mutable bool m_perProfileSawBatchCacheValid = false;
};
