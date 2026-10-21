#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include "appsettings.h"

// Per-(grinderModel, grinderBurrs) deliberate UGS calibration records
// (OpenSpec add-grinder-ugs-calibration, Phase 2). A record holds a fine
// anchor and a coarse anchor — each {setting, ugs, rpm} — and the
// conversionKey = Δsetting/ΔUGS computed from the two. buildGrinderCalibrationBlock
// prefers this measured key over its mined within-batch key.
//
// This domain is DELIBERATELY ignorant of GrinderAliases/ShotSummarizer: it
// stores and validates numeric records, it does not parse dial strings or
// resolve UGS. The capture surface (a main-thread Q_INVOKABLE on AIManager)
// does that resolution and hands this class the numbers.
class SettingsGrinder : public QObject {
    Q_OBJECT

public:
    explicit SettingsGrinder(QObject* parent = nullptr);

    // Minimum anchor span: two anchors must be ≥ K UGS apart (sign-agnostic).
    // The whole point of a deliberate calibration is a WIDE span, so this is
    // deliberately larger than the mined-path floor (kCalibMinPairSpanUgs 0.75).
    static constexpr double kMinAnchorSpanUgs = 2.0;

    // The calibration record for (model, burrs), re-validated on read
    // (QSettings JSON is user-editable). Empty map when absent or invalid.
    // Keys: conversionKey, ts, fineAnchor{setting,ugs,rpm}, coarseAnchor{...}.
    Q_INVOKABLE QVariantMap calibrationFor(const QString& model, const QString& burrs) const;

    // Record ONE anchor (`which` = "fine" | "coarse"). When BOTH anchors then
    // exist, computes and persists conversionKey = (coarseSetting - fineSetting)
    // / (coarseUgs - fineUgs), REJECTING |coarseUgs - fineUgs| < kMinAnchorSpanUgs.
    // Returns true when the anchor was stored. A rejected span leaves the pair
    // without a key (the record is not publishable until re-anchored wider).
    Q_INVOKABLE bool setAnchor(const QString& model, const QString& burrs,
                               const QString& which,
                               double setting, double ugs, double rpm);

    Q_INVOKABLE void clearCalibration(const QString& model, const QString& burrs);

    // Pure selection + revalidation over a plain snapshot of all records. The
    // background-thread consumer (buildGrinderCalibrationBlock) snapshots the
    // store on the main thread and calls this off-thread — SettingsGrinder
    // never touches QSettings from the worker. `calibrationFor` reads the store
    // into a snapshot and calls this, so write and read share ONE validator.
    static QVariantMap calibrationFromSnapshot(const QVariantMap& snapshot,
                                               const QString& model, const QString& burrs);

    // The whole store as a plain map (record-key -> record QVariantMap), read on
    // the main thread and handed to a worker. Bounded: one entry per calibrated
    // (model, burrs).
    QVariantMap snapshotAll() const;

    // Normalized storage key for (model, burrs). Static + public so the capture
    // surface and the consumer agree with the store. Model canonicalized via
    // GrinderAliases alias lookup (else trimmed raw); burrs trimmed; empty↔empty.
    static QString recordKey(const QString& model, const QString& burrs);

signals:
    void calibrationChanged(const QString& model, const QString& burrs);

private:
    mutable AppSettings m_settings;
};
