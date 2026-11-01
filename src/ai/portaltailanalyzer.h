#pragma once
#ifndef DECENZA_PORTALTAILANALYZER_H
#define DECENZA_PORTALTAILANALYZER_H

#include "models/portalsample.h"

#include <QPointF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Pure, deterministic mass-space analysis for Belka PORTAL electrical
// conductivity (EC) curves paired with physical scale cumulative cup weight.
//
// Scientific & physical guardrails:
//   - Belka PORTAL reports raw electrical conductivity (`ecRaw`) in device
//     units, not optical refractive index (TDS %). Without a bean- and
//     water-specific calibration against a refractometer, `ecRaw` and its
//     mass integral must NEVER be presented as physical TDS % or physical
//     Extraction Yield %. Integrals remain raw-EC * g even when a separate
//     empirical refractometer model supplies an estimated TDS/EY.
//   - Mass-space integration requires a physical scale (`WeightSource::PhysicalScale`).
//     Virtual FlowScale curves (derived from DE1 pump flow) are explicitly
//     rejected (`Status::RequiresPhysicalScale`).
class PortalTailAnalyzer {
public:
    static constexpr double kGapSplitSeconds = 0.5;
    static constexpr double kBoundaryClampSeconds = 0.5;
    static constexpr double kMinTailMassG = 5.0;
    static constexpr double kMinCoverageRatio = 0.85;
    static constexpr double kActivePeakMinMassG = 2.0;
    static constexpr double kMinInstabilityProminenceRawEc = 0.8;

    enum class WeightSource {
        Unknown,
        PhysicalScale,
        FlowScaleVirtual
    };

    enum class MountingMode {
        Unknown,
        OnScale,      // PORTAL rests on the cup/stand on the tared scale
        Independent   // PORTAL is held independently above the cup/scale
    };

    enum class Status {
        Valid,
        NoPortalData,
        RequiresPhysicalScale,
        InsufficientWeightData,
        InsufficientCoverage
    };

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
        double activeStopTimeSec = 0.0; // >0 splits active pour vs post-stop SAW settling
        double lagOffsetSec = 0.0;      // Evaluates EC(t + lagOffsetSec) against m(t)
    };

    struct Result {
        bool valid = false;
        Status status = Status::NoPortalData;
        WeightSource weightSource = WeightSource::Unknown;
        MountingMode mountingMode = MountingMode::Unknown;
        QString calibrationMode = QStringLiteral("relative_proxy_only");
        double lagOffsetSec = 0.0;

        // Sample & coverage diagnostics
        int portalSampleCount = 0;
        int weightSampleCount = 0;
        int gapCount = 0;
        double totalMassG = 0.0;
        double coveredMassG = 0.0;
        double coverageRatio = 0.0;

        // Peak & terminal EC (raw-EC) + mean sensor temperature (C)
        double peakRawEc = 0.0;
        double peakTimeSec = 0.0;
        double mainPeakRawEc = 0.0; // Peak EC at cumulative mass >= kActivePeakMinMassG
        double mainPeakMassG = 0.0;
        double finalRawEc = 0.0;
        double meanTempC = 0.0;

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
        int instabilityDipCount = 0;
        double maxDipProminenceRawEc = 0.0;
        QVector<InstabilityDip> instabilityDips;

        // Safeguards & reliability diagnostics for EC -> TDS/EY estimation
        double maxTempC = 0.0;
        double firstDropTimeSec = 0.0;
        double timeTo2gSec = 0.0;
        double onsetFlowRateGPerS = 0.0;
        double ecWettingLagSec = 0.0;
        bool onsetChokeDetected = false;
        bool thermalDriftWarning = false;
        bool estimateReliable = false;
        bool calibrationEligible = false;
        QStringList uncertaintyReasons;
        QString uncertaintySummary;
        QString uncertaintyNoteText;

        // Shot completion / cut-point & historical ratio advisor
        double massAt85PctIntegralG = 0.0;
        double massAt90PctIntegralG = 0.0;
        double post1to2MassG = 0.0;
        double post1to2ContributionShare = 0.0;
        bool lowTailConcentration = false;
        bool possibleUnderExtraction = false;
        double bestHistoricalWeightG = 0.0;
        double bestHistoricalRatio = 0.0;
        int bestHistoricalEnjoyment = 0;
        QString completionSummary;
        QString ratioAdviceText;

        // Empirical refractometer calibration (Atago PAL / drink_tds paired shots)
        bool calibrated = false;
        QString calibrationScope;       // "bag" | "bean" | ""
        int calibrationShotCount = 0;
        double calibrationSlopeK = 0.0; // Proportional model: TDS% = k * meanRawEcPerG
        bool calibrationAffineValid = false;
        double calibrationAffineA = 0.0; // Affine model: TDS% = a * meanRawEcPerG + b
        double calibrationAffineB = 0.0;
        double calibrationMaeTdsPct = 0.0;
        double calibrationRmseTdsPct = 0.0;
        double calibrationLoocvMaeTdsPct = 0.0;
        double calibrationLoocvRmseTdsPct = 0.0;
        double calibrationTempCorrR = 0.0;
        double calibrationTempSlopePctPerC = 0.0;
        double estimatedTdsPct = 0.0;
        double estimatedEyPct = 0.0;
        bool hasValidationReading = false;
        double validationResidualTdsPct = 0.0; // estimatedTdsPct - measuredTdsPct
        double validationResidualEyPct = 0.0;  // estimatedEyPct - measuredEyPct

        QVariantMap toVariantMap() const;
    };

    struct CalibrationPoint {
        qint64 shotId = 0;
        qint64 timestamp = 0;
        double doseWeightG = 0.0;
        double finalWeightG = 0.0;
        double meanRawEcPerG = 0.0;
        double integralRawEcG = 0.0;
        double meanTempC = 0.0;
        double measuredTdsPct = 0.0;
        double measuredEyPct = 0.0;
        qint64 bagId = -1;
        QString beanBrand;
        QString beanType;
        int enjoyment = 0;
        QString tasteBalance;
        QString tasteBody;
        QString espressoNotes;
        bool estimateReliable = false;
        MountingMode mountingMode = MountingMode::Unknown;
        qint64 equipmentId = 0;
        QString profileName;
    };

    struct CalibrationFit {
        bool calibrated = false;
        QString activeModel = QStringLiteral("none"); // "proportional" | "affine" | "none"
        QString scope = QStringLiteral("none");       // "bag" | "bean" | "none"
        int shotCount = 0;
        int excludedShotCount = 0;
        QVariantList referenceShots;
        double minMeanRawEc = 0.0;
        double maxMeanRawEc = 0.0;
        double minRatio = 0.0;
        double maxRatio = 0.0;

        // Model 1: Proportional (TDS% = k * meanRawEcPerG)
        double proportionalK = 0.0;
        double proportionalMaeTdsPct = 0.0;
        double proportionalRmseTdsPct = 0.0;
        double proportionalBiasTdsPct = 0.0;
        double proportionalLoocvMaeTdsPct = 0.0;
        double proportionalLoocvRmseTdsPct = 0.0;

        // Model 2: Affine with intercept (TDS% = a * meanRawEcPerG + b)
        bool affineValid = false;
        double affineSlopeA = 0.0;
        double affineInterceptB = 0.0;
        double affineMaeTdsPct = 0.0;
        double affineRmseTdsPct = 0.0;
        double affineBiasTdsPct = 0.0;
        double affineLoocvMaeTdsPct = 0.0;
        double affineLoocvRmseTdsPct = 0.0;

        // Step 3: Empirical temperature dependence diagnostics across paired shots
        double tempCorrelationR = 0.0;
        double tempSlopePctPerC = 0.0;

        double predictTdsPct(double meanRawEcPerG) const;
        double predictEyPct(double meanRawEcPerG, double finalWeightG, double doseWeightG) const;
        QVariantMap toVariantMap() const;
    };

    struct LagSensitivityPoint {
        double lagOffsetSec = 0.0;
        bool valid = false;
        double integralRawEcG = 0.0;
        double meanRawEcPerG = 0.0;
        double tailMean2g = 0.0;
        double tailMean5g = 0.0;
        double tailSlope5gPerG = 0.0;
        double tailRatio5g = 0.0;
        double recentContributionShare5g = 0.0;

        QVariantMap toVariantMap() const;
    };

    static Result analyze(const QVector<PortalSample>& portalSamples,
                          const QVector<QPointF>& cumulativeWeight,
                          const Options& options);

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
                                 double measuredTdsPct = 0.0,
                                 double measuredEyPct = 0.0);

    static void evaluateRatioAdvice(Result* result,
                                    const QVector<CalibrationPoint>& historyPoints,
                                    qint64 currentShotId,
                                    qint64 targetBagId,
                                    const QString& targetBeanBrand,
                                    const QString& targetBeanType,
                                    double doseWeightG,
                                    int currentEnjoyment = 0,
                                    const QString& currentTasteBalance = QString(),
                                    const QString& currentTasteBody = QString(),
                                    const QString& currentNotes = QString(),
                                    qint64 targetEquipmentId = 0,
                                    const QString& targetProfileName = QString());

    static QVector<LagSensitivityPoint> analyzeLagSensitivity(
        const QVector<PortalSample>& portalSamples,
        const QVector<QPointF>& cumulativeWeight,
        const Options& baseOptions,
        const QVector<double>& lagOffsetsSec = defaultLagOffsetsSec());

    static QVector<double> defaultLagOffsetsSec();

    static WeightSource inferWeightSource(const QString& debugLog,
                                          const QVector<QPointF>& cumulativeWeight);
    static MountingMode inferMountingMode(const QString& debugLog);

    static QString statusToString(Status status);
    static QString weightSourceToString(WeightSource source);
    static QString mountingModeToString(MountingMode mode);
};

#endif // DECENZA_PORTALTAILANALYZER_H
