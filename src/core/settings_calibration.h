#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>
#include <QPair>
#include <QJsonArray>
#include <QJsonObject>

class Settings;

// Auto flow calibration + SAW (stop-at-weight) learning. Split from Settings as
// part of the Tier 3 domain decomposition (issue #860). Holds a non-owning
// pointer to Settings so sawLearnedLag() / getExpectedDrip() can read the
// current scaleType() without a public-API change. The owner pointer is used
// ONLY for that single lookup — no other Settings surface is reached through
// it.
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
    // legacy global-pool path.
    Q_INVOKABLE double sawLearnedLagFor(const QString& profileFilename, const QString& scaleType) const;
    double getExpectedDripFor(const QString& profileFilename, const QString& scaleType, double currentFlowRate) const;
    QList<QPair<double, double>> sawLearningEntriesFor(const QString& profileFilename, const QString& scaleType, int maxEntries) const;

    // Reports which model the read path uses for (profile, scale).
    Q_INVOKABLE QString sawModelSource(const QString& profileFilename, const QString& scaleType) const;

    void addSawLearningPoint(double drip, double flowRate, const QString& scaleType, double overshoot,
                             const QString& profileFilename = QString());
    Q_INVOKABLE void resetSawLearning();
    Q_INVOKABLE void resetSawLearningForProfile(const QString& profileFilename, const QString& scaleType);

    // Per-pair committed history (storage helpers; mostly for tests + bootstrap recompute).
    QJsonArray perProfileSawHistory(const QString& profileFilename, const QString& scaleType) const;
    QJsonObject allPerProfileSawHistory() const;

    // Per-pair pending batch accumulator (5 entries before committing the batch median).
    QJsonArray sawPendingBatch(const QString& profileFilename, const QString& scaleType) const;

    // Global bootstrap lag for new (profile, scale) pairs without graduated history.
    double globalSawBootstrapLag(const QString& scaleType) const;
    void setGlobalSawBootstrapLag(const QString& scaleType, double lag);

    // Per-scale BLE sensor lag (seconds). Used as first-shot SAW default before learning kicks in.
    static double sensorLag(const QString& scaleType);

    // SAW convergence detection helper
    bool isSawConverged(const QString& scaleType) const;

    // Drop all in-memory caches so the next read pulls from QSettings. Called
    // by Settings::factoryReset() after clearing the underlying store.
    void invalidateCache();

    // Returns SAW learning entries filtered by scale type (most recent first).
    // Used by WeightProcessor to snapshot learning data at shot start.
    QList<QPair<double, double>> sawLearningEntries(const QString& scaleType, int maxEntries) const;

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

    QString currentScaleType() const;

    Settings* m_owner = nullptr;  // Non-owning; used ONLY for currentScaleType() lookup.
    mutable QSettings m_settings;

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
