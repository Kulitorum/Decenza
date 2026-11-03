#pragma once
#ifndef DECENZA_PORTALTAILANALYZER_H
#define DECENZA_PORTALTAILANALYZER_H

#include "models/portalsample.h"

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Mass-space analysis and empirical calibration for Belka PORTAL electrical
// conductivity (EC) curves paired with physical scale cumulative cup weight.
//
// Raw EC and its mass integral are device units, never TDS % or EY %. Only a
// coffee-specific refractometer fit may supply an estimated TDS/EY, and only
// a physical scale curve can be integrated (virtual FlowScale weight is
// rejected with Status::RequiresPhysicalScale).
class PortalTailAnalyzer {
public:
    static constexpr double kGapSplitSeconds = 0.5;
    static constexpr double kBoundaryClampSeconds = 0.5;
    static constexpr double kTailWindowG = 5.0; // Tail features need this much observed mass.
    static constexpr double kMinCoverageRatio = 0.85;
    static constexpr double kCupMassToleranceG = 0.1; // One step of Shot Review's cup-weight input.
    static constexpr double kActivePeakMinMassG = 2.0;
    static constexpr double kMinInstabilityProminenceRawEc = 0.8;
    static constexpr qsizetype kMaxReferenceShots = 20;

    enum class WeightSource { Unknown, PhysicalScale, FlowScaleVirtual };

    enum class MountingMode {
        Unknown,
        OnScale,      // PORTAL rests on the cup/stand on the tared scale
        Independent   // PORTAL is held independently above the cup/scale
    };

    enum class Status {
        Valid,
        NoPortalData,
        MissingWeightProvenance,
        RequiresPhysicalScale,
        InsufficientWeightData,
        InsufficientCoverage
    };

    enum class CalibrationModel { None, Proportional, Affine };
    enum class CalibrationScope { None, Bag, Bean };
    enum class RatioAdvice { TasteFirst, Uncertain, TryLonger, TryShorter };

    struct InstabilityDip {
        double startMassG = 0.0;
        double startTimeSec = 0.0;
        double valleyMassG = 0.0;
        double valleyTimeSec = 0.0;
        double recoveryMassG = 0.0;
        double recoveryTimeSec = 0.0;
        double dropRawEc = 0.0;
        double recoveryRawEc = 0.0;
        double prominenceRawEc = 0.0;

        QVariantMap toVariantMap() const;
    };

    struct Options {
        WeightSource weightSource = WeightSource::Unknown;
        MountingMode mountingMode = MountingMode::Unknown;
        double doseWeightG = 0.0;
        double finalWeightG = 0.0; // Metadata ratio; observed coverage still uses the scale curve.
        double activeStopTimeSec = 0.0; // >0 splits active pour vs post-stop SAW settling
    };

    // The four values a shot row persists, zero unless the analysis is valid.
    struct PersistedSummary {
        double meanRawEcPerG = 0.0;
        double integralRawEcG = 0.0;
        double meanTempC = 0.0;
        bool measurementReliable = false;
    };

    struct Result {
        Status status = Status::NoPortalData;
        WeightSource weightSource = WeightSource::Unknown;
        MountingMode mountingMode = MountingMode::Unknown;

        // Sample & coverage diagnostics
        qsizetype portalSampleCount = 0;
        qsizetype weightSampleCount = 0;
        qsizetype gapCount = 0;
        double totalMassG = 0.0;
        double coveredMassG = 0.0;
        double coverageRatio = 0.0;

        // Peak & terminal EC (raw-EC) + sensor temperature (C)
        double peakRawEc = 0.0;
        double peakTimeSec = 0.0;
        double mainPeakRawEc = 0.0; // Peak EC at cumulative mass >= kActivePeakMinMassG
        double mainPeakMassG = 0.0;
        double finalRawEc = 0.0;
        double meanTempC = 0.0; // Time-sample mean; persisted and displayed only.
        double massWeightedTempC = 0.0;
        std::optional<double> massWeightedAuxiliaryTemperatureRaw;

        // Mass-space integrals (raw-EC * g and normalized relative proxies)
        double integralRawEcG = 0.0;
        double activeIntegralRawEcG = 0.0;
        double settlingIntegralRawEcG = 0.0;
        double meanRawEcPerG = 0.0;
        double doseNormalizedIntegral = 0.0;

        // Tail features over the final 2 g and 5 g of cumulative cup mass
        double tailMean2g = 0.0;                // M2 (raw-EC)
        double tailMean5g = 0.0;                // M5 (raw-EC)
        double tailSlope5gPerG = 0.0;           // T5 (raw-EC / g)
        double tailRatio5g = 0.0;               // R5 = M5 / mainPeakRawEc
        double recentContributionShare5g = 0.0; // int_{M-5}^M EC dm / int_0^M EC dm

        // Mid-shot instability / V-dip events after main peak
        qsizetype instabilityDipCount = 0;
        double maxDipProminenceRawEc = 0.0;
        QVector<InstabilityDip> instabilityDips;

        double maxTempC = 0.0;
        bool thermalDriftWarning = false;
        // Fixed by analyze(): the measurement itself is clean enough to train a
        // calibration and to support ratio advice. Calibration never changes it.
        bool measurementReliable = false;
        QStringList uncertaintyReasons; // Measurement caveats, fixed by analyze().
        QStringList calibrationReasons; // Prediction caveats, replaced by each applyCalibration().

        // Shot completion / cut-point & historical ratio advisor
        double massAt85PctIntegralG = 0.0;
        double massAt90PctIntegralG = 0.0;
        double post1to2MassG = 0.0;
        double post1to2ContributionShare = 0.0;
        bool lowTailConcentration = false;
        double bestHistoricalWeightG = 0.0;
        double bestHistoricalRatio = 0.0;
        int bestHistoricalEnjoyment = 0;
        RatioAdvice ratioAdvice = RatioAdvice::TasteFirst;

        // Empirical refractometer calibration (Atago PAL / drink_tds paired shots)
        bool calibrated = false;
        bool predictionAvailable = false;
        bool calibrationHasCrossValidation = false;
        CalibrationModel calibrationModel = CalibrationModel::None;
        CalibrationScope calibrationScope = CalibrationScope::None;
        qsizetype calibrationShotCount = 0;
        double calibrationSlopeK = 0.0; // Proportional model: TDS% = k * meanRawEcPerG
        double calibrationAffineA = 0.0; // Affine model: TDS% = a * meanRawEcPerG + b
        double calibrationAffineB = 0.0;
        double calibrationLoocvMaeTdsPct = 0.0;
        double estimatedTdsPct = 0.0;
        double estimatedEyPct = 0.0;
        bool hasValidationReading = false;
        double selfReferenceK = 0.0;           // This shot's own measured TDS / meanRawEcPerG
        double validationResidualTdsPct = 0.0; // estimatedTdsPct - measuredTdsPct

        bool valid() const { return status == Status::Valid; }
        // No measurement or calibration caveat applies.
        bool estimateReliable() const
        {
            return valid() && uncertaintyReasons.isEmpty() && calibrationReasons.isEmpty();
        }
        QString calibrationMode() const;
        PersistedSummary persistedSummary() const;
        QVariantMap toVariantMap() const;
    };

    struct CalibrationPoint {
        qint64 shotId = 0;
        qint64 timestamp = 0;
        double doseWeightG = 0.0;
        double finalWeightG = 0.0;
        double meanRawEcPerG = 0.0;
        double measuredTdsPct = 0.0;
        qint64 bagId = -1;
        QString beanBrand;
        QString beanType;
        int enjoyment = 0;
        bool measurementReliable = false;
        MountingMode mountingMode = MountingMode::Unknown;
        qint64 equipmentId = 0;
        QString profileName;
    };

    struct Prediction {
        bool available = false; // A reliable measurement, a supported model, inside the measured range.
        double tdsPct = 0.0;
        double eyPct = 0.0;
        QStringList uncertaintyReasons;
    };

    struct CalibrationFit {
        bool calibrated = false;
        bool hasCrossValidation = false;
        bool predictionSupported = false;
        double baselineLoocvRmseTdsPct = 0.0;
        CalibrationModel activeModel = CalibrationModel::None;
        CalibrationScope scope = CalibrationScope::None;
        qsizetype shotCount = 0;
        qsizetype excludedShotCount = 0;
        QVariantList referenceShots; // At most kMaxReferenceShots, most relevant first.
        double minMeanRawEc = 0.0;
        double maxMeanRawEc = 0.0;
        double minRatio = 0.0;
        double maxRatio = 0.0;

        // Model 1: Proportional (TDS% = k * meanRawEcPerG); errors are leave-one-out.
        double proportionalK = 0.0;
        double proportionalLoocvMaeTdsPct = 0.0;
        double proportionalLoocvRmseTdsPct = 0.0;

        // Model 2: Affine with intercept (TDS% = a * meanRawEcPerG + b)
        bool affineValid = false;
        bool affineHasCrossValidation = false;
        double affineSlopeA = 0.0;
        double affineInterceptB = 0.0;
        double affineLoocvMaeTdsPct = 0.0;
        double affineLoocvRmseTdsPct = 0.0;

        bool affineActive() const { return activeModel == CalibrationModel::Affine; }
        double activeLoocvMaeTdsPct() const { return affineActive() ? affineLoocvMaeTdsPct : proportionalLoocvMaeTdsPct; }
        double activeLoocvRmseTdsPct() const { return affineActive() ? affineLoocvRmseTdsPct : proportionalLoocvRmseTdsPct; }

        // The single prediction gate shared by Shot Review, History and the AI payload.
        Prediction predict(double meanRawEcPerG, double finalWeightG, double doseWeightG,
                           bool measurementReliable) const;
        QVariantMap toVariantMap() const;
    };

    static Result analyze(const QVector<PortalSample>& portalSamples,
                          const QVector<QPointF>& cumulativeWeight,
                          const Options& options);

    // analyze() with Options decoded from a stored shot; pressure's last sample marks the active stop.
    static Result analyzeShot(const QVector<PortalSample>& portalSamples,
                              const QVector<QPointF>& cumulativeWeight,
                              const QString& weightSource, const QString& mountingMode,
                              double doseWeightG, double finalWeightG,
                              const QVector<QPointF>& pressure);

    static CalibrationFit fitCalibration(const QVector<CalibrationPoint>& points,
                                         qint64 targetBagId = -1,
                                         const QString& targetBeanBrand = QString(),
                                         const QString& targetBeanType = QString(),
                                         qint64 excludeShotId = 0,
                                         MountingMode targetMountingMode = MountingMode::Unknown);

    static void applyCalibration(Result* result,
                                 const CalibrationFit& fit,
                                 double finalWeightG,
                                 double doseWeightG,
                                 double measuredTdsPct = 0.0);

    static double extractionYieldPct(double tdsPct, double finalWeightG, double doseWeightG);

    static void evaluateRatioAdvice(Result* result,
                                    const QVector<CalibrationPoint>& historyPoints,
                                    qint64 currentShotId,
                                    qint64 targetBagId,
                                    const QString& targetBeanBrand,
                                    const QString& targetBeanType,
                                    double doseWeightG,
                                    const QString& currentTasteBalance = QString(),
                                    const QString& currentTasteBody = QString(),
                                    qint64 targetEquipmentId = 0,
                                    const QString& targetProfileName = QString());

    // Case-folded so SQL scope resolution, fitting and the History cache agree.
    static QString coffeeNameKey(const QString& name);

    // A physical-scale curve with a recorded mounting: the only shots a calibration may learn from.
    static bool hasReferenceProvenance(const QString& weightSource, const QString& mountingMode)
    {
        return weightSourceFromString(weightSource) == WeightSource::PhysicalScale
            && mountingModeFromString(mountingMode) != MountingMode::Unknown;
    }

    static WeightSource weightSourceFromString(const QString& source)
    {
        if (source == QLatin1String("physical_scale")) return WeightSource::PhysicalScale;
        if (source == QLatin1String("flow_scale_virtual")) return WeightSource::FlowScaleVirtual;
        return WeightSource::Unknown;
    }
    static MountingMode mountingModeFromString(const QString& mode,
                                               Qt::CaseSensitivity casing = Qt::CaseSensitive)
    {
        if (mode.compare(QLatin1String("onScale"), casing) == 0) return MountingMode::OnScale;
        if (mode.compare(QLatin1String("independent"), casing) == 0) return MountingMode::Independent;
        return MountingMode::Unknown;
    }

    static QString statusToString(Status status);
    static QString weightSourceToString(WeightSource source)
    {
        switch (source) {
        case WeightSource::PhysicalScale:
            return QStringLiteral("physical_scale");
        case WeightSource::FlowScaleVirtual:
            return QStringLiteral("flow_scale_virtual");
        case WeightSource::Unknown:
            return QStringLiteral("unknown");
        }
        return QStringLiteral("unknown");
    }
    static QString mountingModeToString(MountingMode mode)
    {
        switch (mode) {
        case MountingMode::OnScale:
            return QStringLiteral("onScale");
        case MountingMode::Independent:
            return QStringLiteral("independent");
        case MountingMode::Unknown:
            return QStringLiteral("unknown");
        }
        return QStringLiteral("unknown");
    }
    static QString calibrationModelToString(CalibrationModel model);
    static QString calibrationScopeToString(CalibrationScope scope);
    static QString ratioAdviceToString(RatioAdvice advice);
};

#endif // DECENZA_PORTALTAILANALYZER_H
