#include "ai/portaltailanalyzer.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

struct PortalSegment {
    QVector<PortalSample> samples;

    double startTime() const { return samples.first().time; }
    double endTime() const { return samples.last().time; }
};

struct MassKnot {
    double timeSec = 0.0;
    double massG = 0.0;
    std::optional<double> ecRaw;
    double cumulativeIntegral = 0.0;
};

bool isFinitePortalSample(const PortalSample& s)
{
    return std::isfinite(s.time) && std::isfinite(s.ecRaw) && std::isfinite(s.temperatureC);
}

bool isFiniteWeightPoint(const QPointF& pt)
{
    return std::isfinite(pt.x()) && std::isfinite(pt.y());
}

// Splits valid, time-sorted PORTAL samples into contiguous segments wherever
// `breakBefore` is flagged (after index 0) or adjacent samples are separated
// by more than `kGapSplitSeconds`.
QVector<PortalSegment> buildPortalSegments(const QVector<PortalSample>& rawSamples,
                                           int* outValidCount,
                                           int* outGapCount,
                                           double* outPeakRawEc,
                                           double* outPeakTimeSec,
                                           double* outMeanTempC,
                                           double* outMaxTempC = nullptr)
{
    QVector<PortalSample> cleaned;
    cleaned.reserve(rawSamples.size());
    double sumTempC = 0.0;
    double maxTempC = 0.0;
    for (const auto& s : rawSamples) {
        if (isFinitePortalSample(s)) {
            cleaned.append(s);
            sumTempC += s.temperatureC;
            if (s.temperatureC > maxTempC)
                maxTempC = s.temperatureC;
        }
    }

    std::stable_sort(cleaned.begin(), cleaned.end(), [](const PortalSample& a, const PortalSample& b) {
        return a.time < b.time;
    });

    if (outValidCount)
        *outValidCount = static_cast<int>(cleaned.size());
    if (outGapCount)
        *outGapCount = 0;
    if (outPeakRawEc)
        *outPeakRawEc = 0.0;
    if (outPeakTimeSec)
        *outPeakTimeSec = 0.0;
    if (outMeanTempC)
        *outMeanTempC = cleaned.isEmpty() ? 0.0 : (sumTempC / static_cast<double>(cleaned.size()));
    if (outMaxTempC)
        *outMaxTempC = maxTempC;

    QVector<PortalSegment> segments;
    if (cleaned.isEmpty())
        return segments;

    double peakEc = cleaned.first().ecRaw;
    double peakTime = cleaned.first().time;
    int gaps = 0;

    PortalSegment current;
    current.samples.append(cleaned.first());

    for (qsizetype i = 1; i < cleaned.size(); ++i) {
        const PortalSample& prev = cleaned[i - 1];
        const PortalSample& cur = cleaned[i];
        if (cur.ecRaw > peakEc) {
            peakEc = cur.ecRaw;
            peakTime = cur.time;
        }

        const double dt = cur.time - prev.time;
        if (cur.breakBefore || dt > PortalTailAnalyzer::kGapSplitSeconds) {
            ++gaps;
            segments.append(std::move(current));
            current = PortalSegment{};
        }
        current.samples.append(cur);
    }
    if (!current.samples.isEmpty())
        segments.append(std::move(current));

    if (outGapCount)
        *outGapCount = gaps;
    if (outPeakRawEc)
        *outPeakRawEc = peakEc;
    if (outPeakTimeSec)
        *outPeakTimeSec = peakTime;

    return segments;
}

// Evaluates EC at query time `tQuery` across contiguous PORTAL segments.
// - Inside a segment: linear interpolation (or exact value for 1-point segment).
// - Within `kBoundaryClampSeconds` before the first segment or after the last
//   segment: clamps to the nearest boundary sample (handles minor SAW settling
//   overhang where the final scale packet arrives tens of milliseconds after
//   the last BLE notification from PORTAL).
// - Inside an internal gap between segments, or outside `kBoundaryClampSeconds`:
//   returns std::nullopt so the mass step is counted as uncovered.
std::optional<double> evaluatePortalEcAtTime(const QVector<PortalSegment>& segments, double tQuery)
{
    if (segments.isEmpty() || !std::isfinite(tQuery))
        return std::nullopt;

    const PortalSegment& firstSeg = segments.first();
    if (tQuery < firstSeg.startTime()) {
        if (firstSeg.startTime() - tQuery <= PortalTailAnalyzer::kBoundaryClampSeconds)
            return firstSeg.samples.first().ecRaw;
        return std::nullopt;
    }

    const PortalSegment& lastSeg = segments.last();
    if (tQuery > lastSeg.endTime()) {
        if (tQuery - lastSeg.endTime() <= PortalTailAnalyzer::kBoundaryClampSeconds)
            return lastSeg.samples.last().ecRaw;
        return std::nullopt;
    }

    for (const auto& seg : segments) {
        if (tQuery < seg.startTime() || tQuery > seg.endTime())
            continue;

        const auto& pts = seg.samples;
        if (pts.size() == 1)
            return pts.first().ecRaw;

        auto it = std::lower_bound(
            pts.begin(), pts.end(), tQuery,
            [](const PortalSample& s, double t) { return s.time < t; });

        if (it == pts.begin())
            return it->ecRaw;
        if (it == pts.end())
            return pts.last().ecRaw;

        const PortalSample& right = *it;
        const PortalSample& left = *(it - 1);
        const double dt = right.time - left.time;
        if (dt <= 1e-9)
            return right.ecRaw;

        const double alpha = (tQuery - left.time) / dt;
        return left.ecRaw + alpha * (right.ecRaw - left.ecRaw);
    }

    // tQuery fell strictly between two segments (an internal BLE gap > kGapSplitSeconds).
    return std::nullopt;
}

double interpolateMassAtIntegral(const QVector<MassKnot>& uniqueKnots, double targetIntegral)
{
    if (uniqueKnots.isEmpty())
        return 0.0;
    if (targetIntegral <= uniqueKnots.first().cumulativeIntegral)
        return uniqueKnots.first().massG;
    if (targetIntegral >= uniqueKnots.last().cumulativeIntegral)
        return uniqueKnots.last().massG;

    auto it = std::lower_bound(
        uniqueKnots.begin(), uniqueKnots.end(), targetIntegral,
        [](const MassKnot& k, double val) { return k.cumulativeIntegral < val; });

    if (it == uniqueKnots.begin())
        return it->massG;
    if (it == uniqueKnots.end())
        return uniqueKnots.last().massG;

    const MassKnot& right = *it;
    const MassKnot& left = *(it - 1);
    const double di = right.cumulativeIntegral - left.cumulativeIntegral;
    if (di <= 1e-9)
        return right.massG;

    const double alpha = (targetIntegral - left.cumulativeIntegral) / di;
    return left.massG + alpha * (right.massG - left.massG);
}

double interpolateIntegralAtMass(const QVector<MassKnot>& uniqueKnots, double targetMass)
{
    if (uniqueKnots.isEmpty())
        return 0.0;
    if (targetMass <= uniqueKnots.first().massG)
        return uniqueKnots.first().cumulativeIntegral;
    if (targetMass >= uniqueKnots.last().massG)
        return uniqueKnots.last().cumulativeIntegral;

    auto it = std::lower_bound(
        uniqueKnots.begin(), uniqueKnots.end(), targetMass,
        [](const MassKnot& k, double m) { return k.massG < m; });

    if (it == uniqueKnots.begin())
        return it->cumulativeIntegral;
    if (it == uniqueKnots.end())
        return uniqueKnots.last().cumulativeIntegral;

    const MassKnot& right = *it;
    const MassKnot& left = *(it - 1);
    const double dm = right.massG - left.massG;
    if (dm <= 1e-9)
        return right.cumulativeIntegral;

    const double alpha = (targetMass - left.massG) / dm;
    return left.cumulativeIntegral + alpha * (right.cumulativeIntegral - left.cumulativeIntegral);
}

std::optional<double> interpolateEcAtMass(const QVector<MassKnot>& uniqueKnots, double targetMass)
{
    if (uniqueKnots.isEmpty())
        return std::nullopt;
    if (targetMass <= uniqueKnots.first().massG)
        return uniqueKnots.first().ecRaw;
    if (targetMass >= uniqueKnots.last().massG)
        return uniqueKnots.last().ecRaw;

    auto it = std::lower_bound(
        uniqueKnots.begin(), uniqueKnots.end(), targetMass,
        [](const MassKnot& k, double m) { return k.massG < m; });

    if (it == uniqueKnots.begin())
        return it->ecRaw;
    if (it == uniqueKnots.end())
        return uniqueKnots.last().ecRaw;

    const MassKnot& right = *it;
    const MassKnot& left = *(it - 1);
    if (!left.ecRaw.has_value() || !right.ecRaw.has_value())
        return std::nullopt;

    const double dm = right.massG - left.massG;
    if (dm <= 1e-9)
        return right.ecRaw;

    const double alpha = (targetMass - left.massG) / dm;
    return *left.ecRaw + alpha * (*right.ecRaw - *left.ecRaw);
}

// Detects significant mid-shot V-dips (drop followed by recovery) in EC(m)
// after the main active-pour peak (m >= kActivePeakMinMassG).
QVector<PortalTailAnalyzer::InstabilityDip> detectInstabilityDips(
    const QVector<MassKnot>& uniqueKnots,
    double mainPeakMassG)
{
    QVector<PortalTailAnalyzer::InstabilityDip> dips;
    if (uniqueKnots.size() < 3)
        return dips;

    qsizetype startIdx = 0;
    while (startIdx < uniqueKnots.size()
           && uniqueKnots[startIdx].massG < std::max(PortalTailAnalyzer::kActivePeakMinMassG, mainPeakMassG)) {
        ++startIdx;
    }
    if (startIdx >= uniqueKnots.size() - 2)
        return dips;

    qsizetype i = startIdx;
    while (i + 2 < uniqueKnots.size()) {
        if (!uniqueKnots[i].ecRaw.has_value()) {
            ++i;
            continue;
        }

        // Advance to local pre-dip high
        while (i + 1 < uniqueKnots.size()
               && uniqueKnots[i + 1].ecRaw.has_value()
               && *uniqueKnots[i + 1].ecRaw >= *uniqueKnots[i].ecRaw) {
            ++i;
        }
        const qsizetype highIdx = i;
        const double highEc = *uniqueKnots[highIdx].ecRaw;

        // Walk down to local valley while looking for a recovery of at least kMinInstabilityProminenceRawEc
        qsizetype valleyIdx = highIdx;
        double valleyEc = highEc;
        qsizetype recoveryIdx = -1;
        double recoveryEc = valleyEc;

        for (qsizetype j = highIdx + 1; j < uniqueKnots.size(); ++j) {
            if (!uniqueKnots[j].ecRaw.has_value())
                break;
            const double curEc = *uniqueKnots[j].ecRaw;
            if (curEc < valleyEc) {
                valleyEc = curEc;
                valleyIdx = j;
                recoveryIdx = -1;
                recoveryEc = valleyEc;
            } else if (curEc - valleyEc >= PortalTailAnalyzer::kMinInstabilityProminenceRawEc) {
                if (curEc > recoveryEc) {
                    recoveryEc = curEc;
                    recoveryIdx = j;
                }
                // Stop extending recovery once EC begins falling again after recovery
                if (j + 1 < uniqueKnots.size()
                    && uniqueKnots[j + 1].ecRaw.has_value()
                    && *uniqueKnots[j + 1].ecRaw < curEc) {
                    break;
                }
            }
        }

        const double drop = highEc - valleyEc;
        const double recovery = recoveryEc - valleyEc;
        const double prominence = std::min(drop, recovery);
        if (recoveryIdx > valleyIdx && prominence >= PortalTailAnalyzer::kMinInstabilityProminenceRawEc) {
            PortalTailAnalyzer::InstabilityDip dip;
            dip.startMassG = uniqueKnots[highIdx].massG;
            dip.startTimeSec = uniqueKnots[highIdx].timeSec;
            dip.valleyMassG = uniqueKnots[valleyIdx].massG;
            dip.valleyTimeSec = uniqueKnots[valleyIdx].timeSec;
            dip.recoveryMassG = uniqueKnots[recoveryIdx].massG;
            dip.recoveryTimeSec = uniqueKnots[recoveryIdx].timeSec;
            dip.dropRawEc = drop;
            dip.recoveryRawEc = recovery;
            dip.prominenceRawEc = prominence;
            dips.append(dip);
            i = recoveryIdx;
        } else {
            ++i;
        }
    }

    return dips;
}

} // namespace

QVariantMap PortalTailAnalyzer::InstabilityDip::toVariantMap() const
{
    QVariantMap m;
    m["startMassG"] = startMassG;
    m["startTimeSec"] = startTimeSec;
    m["valleyMassG"] = valleyMassG;
    m["valleyTimeSec"] = valleyTimeSec;
    m["recoveryMassG"] = recoveryMassG;
    m["recoveryTimeSec"] = recoveryTimeSec;
    m["dropRawEc"] = dropRawEc;
    m["recoveryRawEc"] = recoveryRawEc;
    m["prominenceRawEc"] = prominenceRawEc;
    return m;
}

QVariantMap PortalTailAnalyzer::Result::toVariantMap() const
{
    QVariantMap m;
    m["valid"] = valid;
    m["status"] = PortalTailAnalyzer::statusToString(status);
    m["weightSource"] = PortalTailAnalyzer::weightSourceToString(weightSource);
    m["mountingMode"] = PortalTailAnalyzer::mountingModeToString(mountingMode);
    m["calibrationMode"] = calibrationMode;
    m["lagOffsetSec"] = lagOffsetSec;

    m["portalSampleCount"] = portalSampleCount;
    m["weightSampleCount"] = weightSampleCount;
    m["gapCount"] = gapCount;
    m["totalMassG"] = totalMassG;
    m["coveredMassG"] = coveredMassG;
    m["coverageRatio"] = coverageRatio;

    m["peakRawEc"] = peakRawEc;
    m["peakTimeSec"] = peakTimeSec;
    m["mainPeakRawEc"] = mainPeakRawEc;
    m["mainPeakMassG"] = mainPeakMassG;
    m["finalRawEc"] = finalRawEc;
    m["meanTempC"] = meanTempC;

    m["integralRawEcG"] = integralRawEcG;
    m["activeIntegralRawEcG"] = activeIntegralRawEcG;
    m["settlingIntegralRawEcG"] = settlingIntegralRawEcG;
    m["meanRawEcPerG"] = meanRawEcPerG;
    m["doseNormalizedIntegral"] = doseNormalizedIntegral;

    m["M2"] = tailMean2g;
    m["M5"] = tailMean5g;
    m["T5"] = tailSlope5gPerG;
    m["R5"] = tailRatio5g;
    m["recentContributionShare5g"] = recentContributionShare5g;

    m["instabilityDipCount"] = instabilityDipCount;
    m["maxDipProminenceRawEc"] = maxDipProminenceRawEc;
    QVariantList dipsList;
    dipsList.reserve(instabilityDips.size());
    for (const auto& dip : instabilityDips)
        dipsList.append(dip.toVariantMap());
    m["instabilityDips"] = dipsList;

    m["maxTempC"] = maxTempC;
    m["firstDropTimeSec"] = firstDropTimeSec;
    m["timeTo2gSec"] = timeTo2gSec;
    m["onsetFlowRateGPerS"] = onsetFlowRateGPerS;
    m["ecWettingLagSec"] = ecWettingLagSec;
    m["onsetChokeDetected"] = onsetChokeDetected;
    m["thermalDriftWarning"] = thermalDriftWarning;
    m["estimateReliable"] = estimateReliable;
    m["uncertaintyReasons"] = uncertaintyReasons;
    m["uncertaintySummary"] = uncertaintySummary;
    m["uncertaintyNoteText"] = uncertaintyNoteText;

    m["massAt85PctIntegralG"] = massAt85PctIntegralG;
    m["massAt90PctIntegralG"] = massAt90PctIntegralG;
    m["post1to2MassG"] = post1to2MassG;
    m["post1to2ContributionShare"] = post1to2ContributionShare;
    m["suggestedCutMassG"] = suggestedCutMassG;
    m["suggestedCutRatio"] = suggestedCutRatio;
    m["shotExhaustedInTail"] = shotExhaustedInTail;
    m["shotUnderExtractedInTail"] = shotUnderExtractedInTail;
    m["estimatedRemainingSolidsPct"] = estimatedRemainingSolidsPct;
    m["bestHistoricalWeightG"] = bestHistoricalWeightG;
    m["bestHistoricalRatio"] = bestHistoricalRatio;
    m["bestHistoricalEnjoyment"] = bestHistoricalEnjoyment;
    m["completionSummary"] = completionSummary;
    m["ratioAdviceText"] = ratioAdviceText;

    m["calibrated"] = calibrated;
    if (calibrated) {
        m["calibrationScope"] = calibrationScope;
        m["calibrationShotCount"] = calibrationShotCount;
        m["calibrationSlopeK"] = calibrationSlopeK;
        m["calibrationAffineValid"] = calibrationAffineValid;
        if (calibrationAffineValid) {
            m["calibrationAffineA"] = calibrationAffineA;
            m["calibrationAffineB"] = calibrationAffineB;
        }
        m["calibrationMaeTdsPct"] = calibrationMaeTdsPct;
        m["calibrationRmseTdsPct"] = calibrationRmseTdsPct;
        m["calibrationLoocvMaeTdsPct"] = calibrationLoocvMaeTdsPct;
        m["calibrationLoocvRmseTdsPct"] = calibrationLoocvRmseTdsPct;
        m["calibrationTempCorrR"] = calibrationTempCorrR;
        m["calibrationTempSlopePctPerC"] = calibrationTempSlopePctPerC;
        m["estimatedTdsPct"] = estimatedTdsPct;
        m["estimatedEyPct"] = estimatedEyPct;
        m["hasValidationReading"] = hasValidationReading;
        if (hasValidationReading) {
            m["validationResidualTdsPct"] = validationResidualTdsPct;
            m["validationResidualEyPct"] = validationResidualEyPct;
        }
    }

    return m;
}

QVariantMap PortalTailAnalyzer::LagSensitivityPoint::toVariantMap() const
{
    QVariantMap m;
    m["lagOffsetSec"] = lagOffsetSec;
    m["valid"] = valid;
    m["integralRawEcG"] = integralRawEcG;
    m["meanRawEcPerG"] = meanRawEcPerG;
    m["M2"] = tailMean2g;
    m["M5"] = tailMean5g;
    m["T5"] = tailSlope5gPerG;
    m["R5"] = tailRatio5g;
    m["recentContributionShare5g"] = recentContributionShare5g;
    return m;
}

PortalTailAnalyzer::Result PortalTailAnalyzer::analyze(
    const QVector<PortalSample>& portalSamples,
    const QVector<QPointF>& cumulativeWeight,
    const Options& options)
{
    Result out;
    out.weightSource = options.weightSource;
    out.mountingMode = options.mountingMode;
    out.lagOffsetSec = options.lagOffsetSec;

    int validPortalCount = 0;
    int gapCount = 0;
    double peakRawEc = 0.0;
    double peakTimeSec = 0.0;
    double meanTempC = 0.0;
    double maxTempC = 0.0;
    const QVector<PortalSegment> segments = buildPortalSegments(
        portalSamples, &validPortalCount, &gapCount, &peakRawEc, &peakTimeSec, &meanTempC, &maxTempC);

    out.portalSampleCount = validPortalCount;
    out.gapCount = gapCount;
    out.peakRawEc = peakRawEc;
    out.peakTimeSec = peakTimeSec;
    out.meanTempC = meanTempC;
    out.maxTempC = maxTempC;

    if (validPortalCount < 2 || segments.isEmpty()) {
        out.status = Status::NoPortalData;
        return out;
    }

    if (options.weightSource != WeightSource::PhysicalScale) {
        out.status = Status::RequiresPhysicalScale;
        return out;
    }

    // Filter and sort weight points by time
    QVector<QPointF> weightClean;
    weightClean.reserve(cumulativeWeight.size());
    for (const auto& pt : cumulativeWeight) {
        if (isFiniteWeightPoint(pt))
            weightClean.append(pt);
    }
    std::stable_sort(weightClean.begin(), weightClean.end(), [](const QPointF& a, const QPointF& b) {
        return a.x() < b.x();
    });

    out.weightSampleCount = static_cast<int>(weightClean.size());
    if (weightClean.isEmpty()) {
        out.status = Status::InsufficientWeightData;
        return out;
    }

    // Build monotone non-decreasing cumulative mass knots m(t) = max_{u <= t} w(u).
    // ShotDataModel::addWeightSample() ignores weight < 0.1 g and starts
    // cumulativeWeightData() at the first sample >= 0.1 g (e.g. 0.2 g at t = 5.472 s).
    // Prepend a synthetic (t_first, 0.0) anchor when the first recorded mass > 0
    // so the initial drip [0 -> w_0] is integrated consistently.
    QVector<MassKnot> knots;
    knots.reserve(weightClean.size() + 1);

    const double firstTime = weightClean.first().x();
    const double firstMass = std::max(0.0, weightClean.first().y());
    if (firstMass > 0.0) {
        MassKnot zeroKnot;
        zeroKnot.timeSec = firstTime;
        zeroKnot.massG = 0.0;
        zeroKnot.ecRaw = evaluatePortalEcAtTime(segments, firstTime + options.lagOffsetSec);
        knots.append(zeroKnot);
    }

    double runningMaxMass = 0.0;
    for (const auto& pt : weightClean) {
        const double clampedW = std::max(0.0, pt.y());
        if (knots.isEmpty() || clampedW > runningMaxMass) {
            runningMaxMass = clampedW;
            MassKnot k;
            k.timeSec = pt.x();
            k.massG = runningMaxMass;
            k.ecRaw = evaluatePortalEcAtTime(segments, pt.x() + options.lagOffsetSec);
            knots.append(k);
        }
    }

    out.totalMassG = runningMaxMass;
    if (out.totalMassG < kMinTailMassG || knots.size() < 2) {
        out.status = Status::InsufficientWeightData;
        return out;
    }

    // Trapezoidal integration in cumulative cup-mass space: int EC(m) dm
    double totalIntegral = 0.0;
    double activeIntegral = 0.0;
    double coveredMass = 0.0;
    const bool hasActiveStop = options.activeStopTimeSec > 0.0 && std::isfinite(options.activeStopTimeSec);

    knots[0].cumulativeIntegral = 0.0;
    for (qsizetype i = 1; i < knots.size(); ++i) {
        const double dm = knots[i].massG - knots[i - 1].massG;
        if (dm > 0.0 && knots[i - 1].ecRaw.has_value() && knots[i].ecRaw.has_value()) {
            const double ecMid = 0.5 * (*knots[i - 1].ecRaw + *knots[i].ecRaw);
            const double stepIntegral = ecMid * dm;
            totalIntegral += stepIntegral;
            coveredMass += dm;
            if (!hasActiveStop || knots[i].timeSec <= options.activeStopTimeSec)
                activeIntegral += stepIntegral;
        }
        knots[i].cumulativeIntegral = totalIntegral;
    }

    out.coveredMassG = coveredMass;
    out.coverageRatio = out.totalMassG > 0.0 ? (coveredMass / out.totalMassG) : 0.0;
    out.integralRawEcG = totalIntegral;
    out.activeIntegralRawEcG = hasActiveStop ? activeIntegral : totalIntegral;
    out.settlingIntegralRawEcG = std::max(0.0, out.integralRawEcG - out.activeIntegralRawEcG);

    if (coveredMass > 0.0)
        out.meanRawEcPerG = totalIntegral / coveredMass;
    if (options.doseWeightG > 0.0 && std::isfinite(options.doseWeightG))
        out.doseNormalizedIntegral = totalIntegral / options.doseWeightG;

    const QVector<MassKnot>& uniqueKnots = knots;

    if (uniqueKnots.last().ecRaw.has_value())
        out.finalRawEc = *uniqueKnots.last().ecRaw;

    // Determine main peak EC during active pour (cumulative mass >= kActivePeakMinMassG)
    double mainPeakEc = 0.0;
    double mainPeakMass = 0.0;
    for (const auto& k : uniqueKnots) {
        if (k.massG >= kActivePeakMinMassG && k.ecRaw.has_value() && *k.ecRaw > mainPeakEc) {
            mainPeakEc = *k.ecRaw;
            mainPeakMass = k.massG;
        }
    }
    if (mainPeakEc <= 0.0) {
        mainPeakEc = out.peakRawEc;
        mainPeakMass = 0.0;
    }
    out.mainPeakRawEc = mainPeakEc;
    out.mainPeakMassG = mainPeakMass;

    // Detect mid-shot V-dip instability events
    out.instabilityDips = detectInstabilityDips(uniqueKnots, mainPeakMass);
    out.instabilityDipCount = static_cast<int>(out.instabilityDips.size());
    for (const auto& dip : out.instabilityDips) {
        out.maxDipProminenceRawEc = std::max(out.maxDipProminenceRawEc, dip.prominenceRawEc);
    }

    if (out.coverageRatio < kMinCoverageRatio) {
        out.status = Status::InsufficientCoverage;
        return out;
    }

    // Tail window features over [M_end - 2 g, M_end] and [M_end - 5 g, M_end]
    const double mEnd = out.totalMassG;
    const double iEnd = uniqueKnots.last().cumulativeIntegral;
    const double iMinus2 = interpolateIntegralAtMass(uniqueKnots, mEnd - 2.0);
    const double iMinus5 = interpolateIntegralAtMass(uniqueKnots, mEnd - 5.0);

    out.tailMean2g = (iEnd - iMinus2) / 2.0;
    out.tailMean5g = (iEnd - iMinus5) / 5.0;
    if (out.mainPeakRawEc > 0.0)
        out.tailRatio5g = out.tailMean5g / out.mainPeakRawEc;
    if (iEnd > 0.0)
        out.recentContributionShare5g = (iEnd - iMinus5) / iEnd;

    // Linear least-squares slope T5 = dEC/dm over uniform 51-point mass grid in [M_end - 5, M_end]
    constexpr int kGridPoints = 51;
    const double mStart5 = mEnd - 5.0;
    double sumM = 0.0;
    double sumEc = 0.0;
    double sumMM = 0.0;
    double sumMEc = 0.0;
    int validGridCount = 0;

    for (int idx = 0; idx < kGridPoints; ++idx) {
        const double m = mStart5 + (5.0 * static_cast<double>(idx)) / static_cast<double>(kGridPoints - 1);
        const std::optional<double> ecAtM = interpolateEcAtMass(uniqueKnots, m);
        if (!ecAtM.has_value())
            continue;
        const double dm = m - mStart5;
        sumM += dm;
        sumEc += *ecAtM;
        sumMM += dm * dm;
        sumMEc += dm * (*ecAtM);
        ++validGridCount;
    }

    if (validGridCount >= 2) {
        const double n = static_cast<double>(validGridCount);
        const double denom = n * sumMM - sumM * sumM;
        if (std::abs(denom) > 1e-9)
            out.tailSlope5gPerG = (n * sumMEc - sumM * sumEc) / denom;
    }

    // Evaluate early-shot onset choke and thermal drift safeguards
    out.firstDropTimeSec = firstTime;
    if (out.totalMassG >= 2.0) {
        for (qsizetype i = 1; i < uniqueKnots.size(); ++i) {
            if (uniqueKnots[i].massG >= 2.0) {
                const double m0 = uniqueKnots[i - 1].massG;
                const double m1 = uniqueKnots[i].massG;
                const double t0 = uniqueKnots[i - 1].timeSec;
                const double t1 = uniqueKnots[i].timeSec;
                const double dm = m1 - m0;
                const double frac = dm > 1e-9 ? ((2.0 - m0) / dm) : 1.0;
                out.timeTo2gSec = t0 + std::clamp(frac, 0.0, 1.0) * (t1 - t0);
                break;
            }
        }
        const double dtOnset = out.timeTo2gSec - out.firstDropTimeSec;
        const double dmOnset = std::max(0.1, 2.0 - firstMass);
        if (dtOnset > 1e-6)
            out.onsetFlowRateGPerS = dmOnset / dtOnset;
    }

    for (const auto& seg : segments) {
        for (const auto& s : seg.samples) {
            if (s.time >= out.firstDropTimeSec - 0.5 && s.ecRaw >= 2.0) {
                out.ecWettingLagSec = std::max(0.0, s.time - out.firstDropTimeSec);
                break;
            }
        }
        if (out.ecWettingLagSec > 0.0)
            break;
    }

    if (out.totalMassG >= 10.0 && out.timeTo2gSec > out.firstDropTimeSec) {
        const bool slowDrippingOnset = (out.onsetFlowRateGPerS > 0.0 && out.onsetFlowRateGPerS < 0.85)
                                       && (out.timeTo2gSec >= 11.0 || out.ecWettingLagSec >= 1.2);
        const bool delayedElectrodeWetting = (out.ecWettingLagSec >= 1.8 && out.firstDropTimeSec >= 8.0);
        out.onsetChokeDetected = slowDrippingOnset || delayedElectrodeWetting;
    }

    const double brewRatio = (options.doseWeightG > 0.0 && std::isfinite(options.doseWeightG))
                             ? (out.totalMassG / options.doseWeightG)
                             : 0.0;
    out.thermalDriftWarning = (out.maxTempC >= 68.0) || (brewRatio > 2.55 && out.maxTempC >= 64.0);

    if (out.onsetChokeDetected)
        out.uncertaintyReasons.append(QStringLiteral("onset_choke"));
    if (out.instabilityDipCount > 0)
        out.uncertaintyReasons.append(QStringLiteral("mid_shot_dip"));
    if (out.thermalDriftWarning)
        out.uncertaintyReasons.append(QStringLiteral("thermal_drift"));

    out.estimateReliable = out.uncertaintyReasons.isEmpty() && out.gapCount == 0 && out.coverageRatio >= 0.90;

    if (out.onsetChokeDetected) {
        out.uncertaintySummary = QStringLiteral(
            "Opstarts-choke (%1 g/s frem til %2 s) — tæt crema/dråbeflow i starten gør EC-baseret ~TDS% og ~EY% usikker")
            .arg(QString::number(out.onsetFlowRateGPerS, 'f', 2),
                 QString::number(out.timeTo2gSec, 'f', 1));
        out.uncertaintyNoteText = QStringLiteral(
            "[Belka Portal] Advarsel: Opstarts-choke (%1 g/s frem til %2 s) — tæt crema/dråbeflow i starten giver forhøjet usikkerhed på EC-beregnet TDS/EY.")
            .arg(QString::number(out.onsetFlowRateGPerS, 'f', 2),
                 QString::number(out.timeTo2gSec, 'f', 1));
    } else if (out.instabilityDipCount > 0) {
        out.uncertaintySummary = QStringLiteral(
            "Midt-shot EC-dyk (%1×) — stråle-ustabilitet/kanalisering gør EC-baseret ~TDS% og ~EY% usikker")
            .arg(out.instabilityDipCount);
        out.uncertaintyNoteText = QStringLiteral(
            "[Belka Portal] Advarsel: Midt-shot EC-dyk (%1×) — forhøjet usikkerhed på EC-beregnet TDS/EY.")
            .arg(out.instabilityDipCount);
    } else if (out.thermalDriftWarning) {
        out.uncertaintySummary = QStringLiteral(
            "Høj sluttemperatur (%1 °C / ratio > 1:2.5) — opvarmning i halen løfter rå EC og øger usikkerheden på ~TDS% / ~EY%")
            .arg(QString::number(out.maxTempC, 'f', 1));
        out.uncertaintyNoteText = QStringLiteral(
            "[Belka Portal] Advarsel: Høj sluttemperatur (%1 °C / ratio > 1:2.5) — opvarmning i halen giver forhøjet usikkerhed på EC-beregnet TDS/EY.")
            .arg(QString::number(out.maxTempC, 'f', 1));
    }

    // --- Shot completion / cut-point metrics ---
    if (iEnd > 0.0) {
        out.massAt85PctIntegralG = interpolateMassAtIntegral(uniqueKnots, 0.85 * iEnd);
        out.massAt90PctIntegralG = interpolateMassAtIntegral(uniqueKnots, 0.90 * iEnd);
    }

    const double hasValidDose = (options.doseWeightG > 0.0 && std::isfinite(options.doseWeightG));
    const double m1to2 = hasValidDose ? (2.0 * options.doseWeightG) : 0.0;
    if (hasValidDose && mEnd > m1to2 + 1.0 && iEnd > 0.0) {
        out.post1to2MassG = mEnd - m1to2;
        const double iAt1to2 = interpolateIntegralAtMass(uniqueKnots, m1to2);
        out.post1to2ContributionShare = std::clamp((iEnd - iAt1to2) / iEnd, 0.0, 1.0);
    }

    // Scan mass curve after main peak to find when EC drops below the exhaustion
    // threshold (max(2.35, 16% of mainPeakRawEc)) or marginal 5 g share <= 4.2%.
    const double minScanMass = std::max(out.mainPeakMassG + 5.0,
                                        hasValidDose ? (1.5 * options.doseWeightG) : 18.0);
    const double ecCutThreshold = std::max(2.35, 0.16 * out.mainPeakRawEc);
    double detectedCutMass = mEnd;
    bool cutTriggered = false;
    if (mEnd >= minScanMass) {
        for (double m = minScanMass; m <= mEnd + 1e-6; m += 0.5) {
            const double mClamped = std::min(m, mEnd);
            const std::optional<double> ecAtM = interpolateEcAtMass(uniqueKnots, mClamped);
            const double iAtM = interpolateIntegralAtMass(uniqueKnots, mClamped);
            const double iMinus5AtM = interpolateIntegralAtMass(uniqueKnots, std::max(0.0, mClamped - 5.0));
            const double share5AtM = (iAtM > 1e-6 && mClamped >= 5.0)
                ? ((iAtM - iMinus5AtM) / iAtM) : 1.0;
            const bool lowEc = ecAtM.has_value() && (*ecAtM <= ecCutThreshold);
            const bool lowShare = (mClamped >= 22.0 && share5AtM <= 0.042);
            if (lowEc || lowShare) {
                detectedCutMass = mClamped;
                cutTriggered = true;
                break;
            }
        }
    }
    // When the shot stopped BEFORE reaching the exhaustion threshold, extrapolate
    // how much additional cup weight and extractable solids remain before EC(m)
    // would drop to ecCutThreshold.
    if (!cutTriggered) {
        const double endEc = std::max(out.finalRawEc, out.tailMean2g);
        if (endEc > ecCutThreshold && out.tailMean5g > 0.1) {
            const double decayLambda = std::clamp(
                std::max(0.06, -out.tailSlope5gPerG) / std::max(2.5, out.tailMean5g),
                0.05, 0.16);
            const double deltaMRem = std::clamp(std::log(endEc / ecCutThreshold) / decayLambda, 0.0, 12.0);
            detectedCutMass = mEnd + deltaMRem;
            const double deltaIRem = (endEc - ecCutThreshold) / decayLambda;
            if (iEnd > 0.0 && deltaIRem > 0.0)
                out.estimatedRemainingSolidsPct = (deltaIRem / (iEnd + deltaIRem)) * 100.0;
        }
    }

    out.suggestedCutMassG = detectedCutMass;
    if (hasValidDose)
        out.suggestedCutRatio = out.suggestedCutMassG / options.doseWeightG;
    out.shotExhaustedInTail = (cutTriggered && (mEnd - out.suggestedCutMassG >= 3.0))
                              || (out.recentContributionShare5g > 0.0 && out.recentContributionShare5g <= 0.040);

    // Intrinsic under-extraction signal: tail still carries high EC relative to peak
    // (R5 >= 0.22 and 5g share >= 5.8%) or >= 6.8% of total solids in the last 5 g
    // with substantial remaining extractable solids (>= 4.0% estimated remaining).
    if (!out.shotExhaustedInTail && !out.thermalDriftWarning) {
        const bool highTailConcentration = (out.tailRatio5g >= 0.22 && out.recentContributionShare5g >= 0.058);
        const bool highMarginalTailShare = (out.recentContributionShare5g >= 0.068 && out.estimatedRemainingSolidsPct >= 4.0);
        const bool earlyStopBeforeCut = ((out.suggestedCutMassG - mEnd) >= 3.0 && out.estimatedRemainingSolidsPct >= 5.5);
        out.shotUnderExtractedInTail = highTailConcentration || highMarginalTailShare || earlyStopBeforeCut;
    }

    if (out.post1to2MassG >= 2.0 && hasValidDose) {
        const double pre1to2SharePct = (1.0 - out.post1to2ContributionShare) * 100.0;
        const double post1to2SharePct = out.post1to2ContributionShare * 100.0;
        out.completionSummary = QStringLiteral(
            "%1% af udtrækket lå før 1:2 (%2 g) · Sidste %3 g bidrog kun med %4% (90% nået ved %5 g)")
            .arg(QString::number(pre1to2SharePct, 'f', 1),
                 QString::number(m1to2, 'f', 1),
                 QString::number(out.post1to2MassG, 'f', 1),
                 QString::number(post1to2SharePct, 'f', 1),
                 QString::number(out.massAt90PctIntegralG, 'f', 1));
    } else if (out.massAt90PctIntegralG > 0.0) {
        const QString ratio90Str = hasValidDose
            ? QStringLiteral(" (1:%1)").arg(QString::number(out.massAt90PctIntegralG / options.doseWeightG, 'f', 2))
            : QString();
        out.completionSummary = QStringLiteral(
            "90% af udtrækket nået ved %1 g%2 · Sidste 5 g gav %3% af totalen")
            .arg(QString::number(out.massAt90PctIntegralG, 'f', 1),
                 ratio90Str,
                 QString::number(out.recentContributionShare5g * 100.0, 'f', 1));
    }

    if (out.shotExhaustedInTail) {
        const QString cutRatioStr = (out.suggestedCutRatio > 0.0)
            ? QStringLiteral(" (1:%1)").arg(QString::number(out.suggestedCutRatio, 'f', 2))
            : QString();
        out.ratioAdviceText = QStringLiteral(
            "Anbefaling: Shottet var reelt færdigt omkring %1 g%2 — de sidste %3 g tilføjede kun %4% pr. 5 g (tynd hale). Overvej at forkorte ratio mod ~%1 g.")
            .arg(QString::number(out.suggestedCutMassG, 'f', 1),
                 cutRatioStr,
                 QString::number(mEnd - out.suggestedCutMassG, 'f', 1),
                 QString::number(out.recentContributionShare5g * 100.0, 'f', 1));
    } else if (out.shotUnderExtractedInTail) {
        const double targetLongerG = std::max(mEnd + 3.0, out.suggestedCutMassG);
        const QString targetRatioStr = hasValidDose
            ? QStringLiteral(" (1:%1)").arg(QString::number(targetLongerG / options.doseWeightG, 'f', 2))
            : QString();
        out.ratioAdviceText = QStringLiteral(
            "Tegn på potentiel underekstraktion: Slutfasen havde stadig høj koncentration (M5 %1, R5 %2%, sidste 5 g = %3% af udtrækket, ~%4% ekstra solids tilbage). Hvis shottet smager syrligt eller kort, kan du forlænge ratio mod ~%5 g%6.")
            .arg(QString::number(out.tailMean5g, 'f', 2),
                 QString::number(out.tailRatio5g * 100.0, 'f', 1),
                 QString::number(out.recentContributionShare5g * 100.0, 'f', 1),
                 QString::number(std::max(3.5, out.estimatedRemainingSolidsPct), 'f', 1),
                 QString::number(targetLongerG, 'f', 1),
                 targetRatioStr);
    } else if (out.recentContributionShare5g > 0.0) {
        const QString stopRatioStr = (brewRatio > 0.0)
            ? QStringLiteral(" (1:%1)").arg(QString::number(brewRatio, 'f', 2))
            : QString();
        out.ratioAdviceText = QStringLiteral(
            "Stopvægt (%1 g%2) passer godt til udtrækskurven (sidste 5 g bidrog med %3% af solids).")
            .arg(QString::number(mEnd, 'f', 1),
                 stopRatioStr,
                 QString::number(out.recentContributionShare5g * 100.0, 'f', 1));
    }

    out.valid = true;
    out.status = Status::Valid;
    return out;
}

QVector<PortalTailAnalyzer::LagSensitivityPoint> PortalTailAnalyzer::analyzeLagSensitivity(
    const QVector<PortalSample>& portalSamples,
    const QVector<QPointF>& cumulativeWeight,
    const Options& baseOptions,
    const QVector<double>& lagOffsetsSec)
{
    QVector<LagSensitivityPoint> table;
    table.reserve(lagOffsetsSec.size());
    for (double tau : lagOffsetsSec) {
        Options opts = baseOptions;
        opts.lagOffsetSec = tau;
        const Result r = analyze(portalSamples, cumulativeWeight, opts);

        LagSensitivityPoint pt;
        pt.lagOffsetSec = tau;
        pt.valid = r.valid;
        pt.integralRawEcG = r.integralRawEcG;
        pt.meanRawEcPerG = r.meanRawEcPerG;
        pt.tailMean2g = r.tailMean2g;
        pt.tailMean5g = r.tailMean5g;
        pt.tailSlope5gPerG = r.tailSlope5gPerG;
        pt.tailRatio5g = r.tailRatio5g;
        pt.recentContributionShare5g = r.recentContributionShare5g;
        table.append(pt);
    }
    return table;
}

QVector<double> PortalTailAnalyzer::defaultLagOffsetsSec()
{
    return { -0.5, -0.3, -0.2, -0.1, 0.0, 0.1, 0.2, 0.3, 0.5 };
}

PortalTailAnalyzer::WeightSource PortalTailAnalyzer::inferWeightSource(
    const QString& debugLog,
    const QVector<QPointF>& cumulativeWeight)
{
    // Detect virtual FlowScale markers in the shot debug log. Note that
    // "disabled FlowScale" and "FlowScale Compare:" appear when a physical
    // scale IS active, so match only markers that indicate FlowScale served
    // the shot or no physical scale was present.
    if (debugLog.contains(QLatin1String("scale=flow"), Qt::CaseInsensitive)
        || debugLog.contains(QLatin1String("scale=none"), Qt::CaseInsensitive)
        || debugLog.contains(QLatin1String("No physical scale"), Qt::CaseInsensitive)
        || debugLog.contains(QLatin1String("switched to FlowScale"), Qt::CaseInsensitive)
        || debugLog.contains(QLatin1String("falling back to FlowScale"), Qt::CaseInsensitive)
        || debugLog.contains(QLatin1String("Scale: Flow Scale"), Qt::CaseInsensitive)) {
        return WeightSource::FlowScaleVirtual;
    }

    if (!cumulativeWeight.isEmpty())
        return WeightSource::PhysicalScale;

    return WeightSource::Unknown;
}

PortalTailAnalyzer::MountingMode PortalTailAnalyzer::inferMountingMode(const QString& debugLog)
{
    if (debugLog.contains(QLatin1String("mountingMode=independent"), Qt::CaseInsensitive))
        return MountingMode::Independent;
    if (debugLog.contains(QLatin1String("mountingMode=onScale"), Qt::CaseInsensitive))
        return MountingMode::OnScale;
    return MountingMode::Unknown;
}

double PortalTailAnalyzer::CalibrationFit::predictTdsPct(double meanRawEcPerG) const
{
    if (!calibrated || !std::isfinite(meanRawEcPerG) || meanRawEcPerG <= 0.0)
        return 0.0;
    double pred = 0.0;
    if (activeModel == QLatin1String("affine") && affineValid)
        pred = affineSlopeA * meanRawEcPerG + affineInterceptB;
    else
        pred = proportionalK * meanRawEcPerG;
    return std::max(0.0, pred);
}

double PortalTailAnalyzer::CalibrationFit::predictEyPct(double meanRawEcPerG,
                                                        double finalWeightG,
                                                        double doseWeightG) const
{
    if (!calibrated || !std::isfinite(finalWeightG) || !std::isfinite(doseWeightG)
        || finalWeightG <= 0.0 || doseWeightG <= 0.0) {
        return 0.0;
    }
    const double tds = predictTdsPct(meanRawEcPerG);
    return (tds * finalWeightG) / doseWeightG;
}

QVariantMap PortalTailAnalyzer::CalibrationFit::toVariantMap() const
{
    QVariantMap m;
    m["calibrated"] = calibrated;
    m["activeModel"] = activeModel;
    m["scope"] = scope;
    m["shotCount"] = shotCount;
    m["proportionalK"] = proportionalK;
    m["proportionalMaeTdsPct"] = proportionalMaeTdsPct;
    m["proportionalRmseTdsPct"] = proportionalRmseTdsPct;
    m["proportionalBiasTdsPct"] = proportionalBiasTdsPct;
    m["proportionalLoocvMaeTdsPct"] = proportionalLoocvMaeTdsPct;
    m["proportionalLoocvRmseTdsPct"] = proportionalLoocvRmseTdsPct;
    m["affineValid"] = affineValid;
    if (affineValid) {
        m["affineSlopeA"] = affineSlopeA;
        m["affineInterceptB"] = affineInterceptB;
        m["affineMaeTdsPct"] = affineMaeTdsPct;
        m["affineRmseTdsPct"] = affineRmseTdsPct;
        m["affineBiasTdsPct"] = affineBiasTdsPct;
        m["affineLoocvMaeTdsPct"] = affineLoocvMaeTdsPct;
        m["affineLoocvRmseTdsPct"] = affineLoocvRmseTdsPct;
    }
    m["tempCorrelationR"] = tempCorrelationR;
    m["tempSlopePctPerC"] = tempSlopePctPerC;
    return m;
}

PortalTailAnalyzer::CalibrationFit PortalTailAnalyzer::fitCalibration(
    const QVector<CalibrationPoint>& points,
    qint64 targetBagId,
    const QString& targetBeanBrand,
    const QString& targetBeanType,
    qint64 excludeShotId)
{
    CalibrationFit fit;

    QVector<CalibrationPoint> validAll;
    validAll.reserve(points.size());
    for (const auto& p : points) {
        if (std::isfinite(p.meanRawEcPerG) && p.meanRawEcPerG > 0.1
            && std::isfinite(p.measuredTdsPct) && p.measuredTdsPct > 0.1) {
            validAll.append(p);
        }
    }
    if (validAll.isEmpty())
        return fit;

    QVector<CalibrationPoint> pool;
    pool.reserve(validAll.size());
    for (const auto& p : validAll) {
        if (excludeShotId > 0 && p.shotId == excludeShotId)
            continue;
        pool.append(p);
    }

    QVector<CalibrationPoint> selected;
    QString scope = QStringLiteral("none");

    if (targetBagId > 0) {
        for (const auto& p : pool) {
            if (p.bagId == targetBagId)
                selected.append(p);
        }
        if (!selected.isEmpty())
            scope = QStringLiteral("bag");
    }

    if (selected.isEmpty() && !targetBeanBrand.trimmed().isEmpty() && !targetBeanType.trimmed().isEmpty()) {
        for (const auto& p : pool) {
            if (p.beanBrand.trimmed().compare(targetBeanBrand.trimmed(), Qt::CaseInsensitive) == 0
                && p.beanType.trimmed().compare(targetBeanType.trimmed(), Qt::CaseInsensitive) == 0) {
                selected.append(p);
            }
        }
        if (!selected.isEmpty())
            scope = QStringLiteral("bean");
    }

    if (selected.isEmpty() && !pool.isEmpty()) {
        selected = pool;
        scope = QStringLiteral("global");
    }

    // If only the current shot itself has a paired refractometer reading, allow
    // a 1-point self-anchor (explicitly labeled scope = "self").
    if (selected.isEmpty() && excludeShotId > 0) {
        for (const auto& p : validAll) {
            if (p.shotId == excludeShotId) {
                selected.append(p);
                scope = QStringLiteral("self");
                break;
            }
        }
    }

    if (selected.isEmpty())
        return fit;

    const int n = static_cast<int>(selected.size());
    fit.shotCount = n;
    fit.scope = scope;

    double sxx = 0.0;
    double sxy = 0.0;
    double sx = 0.0;
    double sy = 0.0;
    double minX = selected.first().meanRawEcPerG;
    double maxX = selected.first().meanRawEcPerG;

    for (const auto& p : selected) {
        const double x = p.meanRawEcPerG;
        const double y = p.measuredTdsPct;
        sxx += x * x;
        sxy += x * y;
        sx += x;
        sy += y;
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
    }

    if (sxx <= 1e-12)
        return fit;

    // 1. Proportional model: y_hat = k * x
    fit.proportionalK = sxy / sxx;
    fit.calibrated = fit.proportionalK > 0.0 && std::isfinite(fit.proportionalK);
    if (!fit.calibrated)
        return fit;

    fit.activeModel = QStringLiteral("proportional");

    double propSumAbs = 0.0;
    double propSumSq = 0.0;
    double propSumErr = 0.0;
    double propLoocvAbs = 0.0;
    double propLoocvSq = 0.0;
    int propLoocvCount = 0;

    for (const auto& p : selected) {
        const double x = p.meanRawEcPerG;
        const double y = p.measuredTdsPct;
        const double err = (fit.proportionalK * x) - y;
        propSumAbs += std::abs(err);
        propSumSq += err * err;
        propSumErr += err;

        if (n >= 2) {
            const double denom = sxx - x * x;
            if (denom > 1e-9) {
                const double kLeaveOut = (sxy - x * y) / denom;
                const double loocvErr = (kLeaveOut * x) - y;
                propLoocvAbs += std::abs(loocvErr);
                propLoocvSq += loocvErr * loocvErr;
                ++propLoocvCount;
            }
        }
    }

    fit.proportionalMaeTdsPct = propSumAbs / static_cast<double>(n);
    fit.proportionalRmseTdsPct = std::sqrt(propSumSq / static_cast<double>(n));
    fit.proportionalBiasTdsPct = propSumErr / static_cast<double>(n);
    if (propLoocvCount > 0) {
        fit.proportionalLoocvMaeTdsPct = propLoocvAbs / static_cast<double>(propLoocvCount);
        fit.proportionalLoocvRmseTdsPct = std::sqrt(propLoocvSq / static_cast<double>(propLoocvCount));
    }

    // 2. Affine model: y_hat = a * x + b (when n >= 3 and EC range >= 0.5 raw-EC)
    const double nd = static_cast<double>(n);
    const double affineDenom = nd * sxx - sx * sx;
    if (n >= 3 && (maxX - minX) >= 0.5 && std::abs(affineDenom) > 1e-9) {
        const double a = (nd * sxy - sx * sy) / affineDenom;
        const double b = (sy - a * sx) / nd;
        if (std::isfinite(a) && std::isfinite(b) && a > 0.0) {
            fit.affineValid = true;
            fit.affineSlopeA = a;
            fit.affineInterceptB = b;

            double affSumAbs = 0.0;
            double affSumSq = 0.0;
            double affSumErr = 0.0;
            double affLoocvAbs = 0.0;
            double affLoocvSq = 0.0;
            int affLoocvCount = 0;

            for (int i = 0; i < n; ++i) {
                const double x = selected[i].meanRawEcPerG;
                const double y = selected[i].measuredTdsPct;
                const double err = (a * x + b) - y;
                affSumAbs += std::abs(err);
                affSumSq += err * err;
                affSumErr += err;

                // Leave-One-Out for affine regression
                const double nSub = static_cast<double>(n - 1);
                const double sxSub = sx - x;
                const double sySub = sy - y;
                const double sxxSub = sxx - x * x;
                const double sxySub = sxy - x * y;
                const double denomSub = nSub * sxxSub - sxSub * sxSub;
                if (std::abs(denomSub) > 1e-9) {
                    const double aSub = (nSub * sxySub - sxSub * sySub) / denomSub;
                    const double bSub = (sySub - aSub * sxSub) / nSub;
                    const double loocvErr = (aSub * x + bSub) - y;
                    affLoocvAbs += std::abs(loocvErr);
                    affLoocvSq += loocvErr * loocvErr;
                    ++affLoocvCount;
                }
            }

            fit.affineMaeTdsPct = affSumAbs / nd;
            fit.affineRmseTdsPct = std::sqrt(affSumSq / nd);
            fit.affineBiasTdsPct = affSumErr / nd;
            if (affLoocvCount > 0) {
                fit.affineLoocvMaeTdsPct = affLoocvAbs / static_cast<double>(affLoocvCount);
                fit.affineLoocvRmseTdsPct = std::sqrt(affLoocvSq / static_cast<double>(affLoocvCount));
            }

            // Select affine model if n >= 4, intercept is physically bounded, and LOOCV RMSE improves by >= 5%
            if (n >= 4 && std::abs(b) < 5.0 && affLoocvCount == n && propLoocvCount == n
                && fit.affineLoocvRmseTdsPct < fit.proportionalLoocvRmseTdsPct * 0.95) {
                fit.activeModel = QStringLiteral("affine");
            }
        }
    }

    // 3. Empirical temperature dependence check across paired shots
    if (n >= 3) {
        double sumT = 0.0;
        double sumR = 0.0;
        double sumTT = 0.0;
        double sumRR = 0.0;
        double sumTR = 0.0;
        double minT = 1e9;
        double maxT = -1e9;
        int tempCount = 0;

        for (const auto& p : selected) {
            if (!std::isfinite(p.meanTempC) || p.meanTempC <= 5.0)
                continue;
            const double predProp = fit.proportionalK * p.meanRawEcPerG;
            if (predProp <= 0.1)
                continue;
            // Relative conductivity excess (%) vs refractometer TDS
            const double relPct = ((predProp - p.measuredTdsPct) / p.measuredTdsPct) * 100.0;
            const double t = p.meanTempC;
            sumT += t;
            sumR += relPct;
            sumTT += t * t;
            sumRR += relPct * relPct;
            sumTR += t * relPct;
            minT = std::min(minT, t);
            maxT = std::max(maxT, t);
            ++tempCount;
        }

        if (tempCount >= 3 && (maxT - minT) >= 0.5) {
            const double nt = static_cast<double>(tempCount);
            const double varT = nt * sumTT - sumT * sumT;
            const double varR = nt * sumRR - sumR * sumR;
            const double covTR = nt * sumTR - sumT * sumR;
            if (varT > 1e-9) {
                fit.tempSlopePctPerC = covTR / varT;
                if (varR > 1e-9)
                    fit.tempCorrelationR = covTR / std::sqrt(varT * varR);
            }
        }
    }

    return fit;
}

void PortalTailAnalyzer::applyCalibration(Result* result,
                                          const CalibrationFit& fit,
                                          double finalWeightG,
                                          double doseWeightG,
                                          double measuredTdsPct,
                                          double measuredEyPct)
{
    if (!result || !result->valid || !fit.calibrated)
        return;

    result->calibrated = true;
    result->calibrationMode = fit.activeModel == QLatin1String("affine")
        ? QStringLiteral("refractometer_affine")
        : QStringLiteral("refractometer_proportional");
    result->calibrationScope = fit.scope;
    result->calibrationShotCount = fit.shotCount;
    result->calibrationSlopeK = fit.proportionalK;
    result->calibrationAffineValid = fit.affineValid;
    result->calibrationAffineA = fit.affineSlopeA;
    result->calibrationAffineB = fit.affineInterceptB;
    result->calibrationMaeTdsPct = fit.activeModel == QLatin1String("affine")
        ? fit.affineMaeTdsPct : fit.proportionalMaeTdsPct;
    result->calibrationRmseTdsPct = fit.activeModel == QLatin1String("affine")
        ? fit.affineRmseTdsPct : fit.proportionalRmseTdsPct;
    result->calibrationLoocvMaeTdsPct = fit.activeModel == QLatin1String("affine")
        ? fit.affineLoocvMaeTdsPct : fit.proportionalLoocvMaeTdsPct;
    result->calibrationLoocvRmseTdsPct = fit.activeModel == QLatin1String("affine")
        ? fit.affineLoocvRmseTdsPct : fit.proportionalLoocvRmseTdsPct;
    result->calibrationTempCorrR = fit.tempCorrelationR;
    result->calibrationTempSlopePctPerC = fit.tempSlopePctPerC;

    const double effectiveWeightG = (std::isfinite(finalWeightG) && finalWeightG > 0.0)
        ? finalWeightG : result->totalMassG;
    result->estimatedTdsPct = fit.predictTdsPct(result->meanRawEcPerG);
    result->estimatedEyPct = fit.predictEyPct(result->meanRawEcPerG, effectiveWeightG, doseWeightG);

    if (std::isfinite(measuredTdsPct) && measuredTdsPct > 0.1) {
        result->hasValidationReading = true;
        result->validationResidualTdsPct = result->estimatedTdsPct - measuredTdsPct;
        const double actualEy = (std::isfinite(measuredEyPct) && measuredEyPct > 0.1)
            ? measuredEyPct
            : ((doseWeightG > 0.0 && effectiveWeightG > 0.0)
                   ? (measuredTdsPct * effectiveWeightG / doseWeightG)
                   : 0.0);
        if (actualEy > 0.0 && result->estimatedEyPct > 0.0)
            result->validationResidualEyPct = result->estimatedEyPct - actualEy;
    }
}

void PortalTailAnalyzer::evaluateRatioAdvice(Result* result,
                                             const QVector<CalibrationPoint>& historyPoints,
                                             qint64 currentShotId,
                                             qint64 targetBagId,
                                             const QString& targetBeanBrand,
                                             const QString& targetBeanType,
                                             double doseWeightG,
                                             int currentEnjoyment,
                                             const QString& currentTasteBalance,
                                             const QString& currentTasteBody,
                                             const QString& currentNotes)
{
    if (!result || !result->valid)
        return;

    const bool hasDose = (std::isfinite(doseWeightG) && doseWeightG > 0.0);
    const double currentRatio = hasDose ? (result->totalMassG / doseWeightG) : 0.0;

    // Filter historical shots for the same bag or same bean
    QVector<CalibrationPoint> sameBeanShots;
    for (const auto& pt : historyPoints) {
        if (pt.finalWeightG <= 5.0 || pt.doseWeightG <= 5.0)
            continue;
        const bool sameBag = (targetBagId > 0 && pt.bagId == targetBagId);
        const bool sameBean = (!targetBeanBrand.trimmed().isEmpty()
                               && !targetBeanType.trimmed().isEmpty()
                               && pt.beanBrand.trimmed().compare(targetBeanBrand.trimmed(), Qt::CaseInsensitive) == 0
                               && pt.beanType.trimmed().compare(targetBeanType.trimmed(), Qt::CaseInsensitive) == 0);
        if (sameBag || sameBean)
            sameBeanShots.append(pt);
    }

    // Find highest-enjoyment shot on this bean (enjoyment >= 75)
    const CalibrationPoint* bestShot = nullptr;
    for (const auto& pt : sameBeanShots) {
        if (pt.enjoyment >= 75) {
            if (!bestShot || pt.enjoyment > bestShot->enjoyment)
                bestShot = &pt;
        }
    }

    // Check taste cues from current shot or most recent rated shot on this bean
    QString effectiveBalance = currentTasteBalance.trimmed().toLower();
    QString effectiveBody = currentTasteBody.trimmed().toLower();
    QString effectiveNotes = currentNotes.toLower();
    int effectiveEnjoyment = currentEnjoyment;

    if (effectiveBalance.isEmpty() && effectiveEnjoyment <= 0) {
        for (const auto& pt : sameBeanShots) {
            if (currentShotId > 0 && pt.shotId == currentShotId)
                continue;
            if (!pt.tasteBalance.trimmed().isEmpty() || pt.enjoyment > 0) {
                effectiveBalance = pt.tasteBalance.trimmed().toLower();
                effectiveBody = pt.tasteBody.trimmed().toLower();
                if (effectiveNotes.isEmpty())
                    effectiveNotes = pt.espressoNotes.toLower();
                effectiveEnjoyment = pt.enjoyment;
                break;
            }
        }
    }

    const bool tastesBitterOrThin = (effectiveBalance == QLatin1String("bitter"))
                                    || (effectiveBody == QLatin1String("thin"))
                                    || effectiveNotes.contains(QLatin1String("bitter"))
                                    || effectiveNotes.contains(QLatin1String("tør"))
                                    || effectiveNotes.contains(QLatin1String("tynd"));
    const bool tastesSourOrUnder = (effectiveBalance == QLatin1String("sour"))
                                   || effectiveNotes.contains(QLatin1String("sur"))
                                   || effectiveNotes.contains(QLatin1String("syrlig"))
                                   || effectiveNotes.contains(QLatin1String("underekstraher"))
                                   || effectiveNotes.contains(QLatin1String("under-ekstraher"))
                                   || effectiveNotes.contains(QLatin1String("underextract"))
                                   || effectiveNotes.contains(QLatin1String("under-extract"))
                                   || effectiveNotes.contains(QLatin1String("skarp"))
                                   || effectiveNotes.contains(QLatin1String("grøn"))
                                   || effectiveNotes.contains(QLatin1String("salty"))
                                   || effectiveNotes.contains(QLatin1String("mangler sødme"));

    const double bestWeightG = bestShot ? bestShot->finalWeightG : 0.0;
    const double bestRatio = (bestShot && bestShot->doseWeightG > 0.0)
        ? (bestShot->finalWeightG / bestShot->doseWeightG) : 0.0;
    result->bestHistoricalWeightG = bestWeightG;
    result->bestHistoricalRatio = bestRatio;
    result->bestHistoricalEnjoyment = bestShot ? bestShot->enjoyment : 0;

    // Also check if this shot extracted significantly fewer total ions per gram of dose
    // than the best-rated shot on the same bean while stopping earlier
    const double bestDoseNormIntegral = (bestShot && bestShot->doseWeightG > 0.0 && bestShot->integralRawEcG > 0.0)
        ? (bestShot->integralRawEcG / bestShot->doseWeightG) : 0.0;
    const bool integralDeficitVsBest = (bestDoseNormIntegral > 0.0 && result->doseNormalizedIntegral > 0.0
                                        && result->doseNormalizedIntegral < 0.90 * bestDoseNormIntegral
                                        && result->totalMassG < bestWeightG - 1.5);
    if (integralDeficitVsBest && !result->shotExhaustedInTail && !result->thermalDriftWarning) {
        result->shotUnderExtractedInTail = true;
    }

    if (result->shotExhaustedInTail) {
        const QString cutRatioStr = (result->suggestedCutRatio > 0.0)
            ? QStringLiteral(" (1:%1)").arg(QString::number(result->suggestedCutRatio, 'f', 2))
            : QString();
        if (bestShot && bestRatio > 0.0) {
            result->ratioAdviceText = QStringLiteral(
                "Anbefaling: Shottet var reelt færdigt omkring %1 g%2 — de sidste %3 g gav kun %4% udtræk pr. 5 g (tynd hale). Dine bedst bedømte shots på denne bønne (%5/100) lå ved %6 g (1:%7); gå tilbage mod ~%6 g.")
                .arg(QString::number(result->suggestedCutMassG, 'f', 1),
                     cutRatioStr,
                     QString::number(result->totalMassG - result->suggestedCutMassG, 'f', 1),
                     QString::number(result->recentContributionShare5g * 100.0, 'f', 1),
                     QString::number(bestShot->enjoyment),
                     QString::number(bestWeightG, 'f', 1),
                     QString::number(bestRatio, 'f', 2));
        } else {
            result->ratioAdviceText = QStringLiteral(
                "Anbefaling: Shottet var reelt færdigt omkring %1 g%2 — de sidste %3 g tilføjede kun %4% pr. 5 g (tynd hale). Overvej at forkorte ratio mod ~%1 g.")
                .arg(QString::number(result->suggestedCutMassG, 'f', 1),
                     cutRatioStr,
                     QString::number(result->totalMassG - result->suggestedCutMassG, 'f', 1),
                     QString::number(result->recentContributionShare5g * 100.0, 'f', 1));
        }
    } else if (tastesSourOrUnder && !result->thermalDriftWarning) {
        if (result->recentContributionShare5g >= 0.044 || result->tailRatio5g >= 0.15) {
            result->shotUnderExtractedInTail = true;
            const double targetLongerG = (bestWeightG > result->totalMassG + 1.5)
                ? bestWeightG
                : std::max(result->totalMassG + 3.0, result->suggestedCutMassG);
            const QString longerRatioStr = hasDose
                ? QStringLiteral(" (1:%1)").arg(QString::number(targetLongerG / doseWeightG, 'f', 2))
                : QString();
            const double remPct = std::max(3.5, result->estimatedRemainingSolidsPct);
            result->ratioAdviceText = QStringLiteral(
                "Tegn på potentiel underekstraktion: Slutfasen havde stadig høj koncentration (M5 %1, R5 %2%, sidste 5 g = %3% af udtrækket, ~%4% ekstra solids tilbage i pucken) og smagen er markeret syrlig/underekstraheret. Prøv at forlænge ratio mod ~%5 g%6 eller mal lidt finere.")
                .arg(QString::number(result->tailMean5g, 'f', 2),
                     QString::number(result->tailRatio5g * 100.0, 'f', 1),
                     QString::number(result->recentContributionShare5g * 100.0, 'f', 1),
                     QString::number(remPct, 'f', 1),
                     QString::number(targetLongerG, 'f', 1),
                     longerRatioStr);
        } else {
            result->shotUnderExtractedInTail = true;
            result->ratioAdviceText = QStringLiteral(
                "Tegn på potentiel underekstraktion trods lav slut-EC (M5 %1): Syrlig/underekstraheret smag kombineret med lav udtrækshale tyder på ujævnt gennemløb (forbigåede zoner i pucken) — fokusér på puck-prep/kværn fremfor blot længere ratio.")
                .arg(QString::number(result->tailMean5g, 'f', 2));
        }
    } else if (result->shotUnderExtractedInTail) {
        const double targetLongerG = (bestWeightG > result->totalMassG + 1.5)
            ? bestWeightG
            : std::max(result->totalMassG + 3.0, result->suggestedCutMassG);
        const QString longerRatioStr = hasDose
            ? QStringLiteral(" (1:%1)").arg(QString::number(targetLongerG / doseWeightG, 'f', 2))
            : QString();
        const double remPct = std::max(4.0, result->estimatedRemainingSolidsPct);
        if (bestShot && bestWeightG > result->totalMassG + 1.5) {
            result->ratioAdviceText = QStringLiteral(
                "Tegn på potentiel underekstraktion: Slutfasen havde stadig høj koncentration (M5 %1, R5 %2%, sidste 5 g = %3% af udtrækket, ~%4% ekstra solids tilbage). Dine bedste shots på denne bønne (%5/100) lå ved %6 g (1:%7) — overvej at øge target weight mod ~%6 g.")
                .arg(QString::number(result->tailMean5g, 'f', 2),
                     QString::number(result->tailRatio5g * 100.0, 'f', 1),
                     QString::number(result->recentContributionShare5g * 100.0, 'f', 1),
                     QString::number(remPct, 'f', 1),
                     QString::number(bestShot->enjoyment),
                     QString::number(bestWeightG, 'f', 1),
                     QString::number(bestRatio, 'f', 2));
        } else {
            result->ratioAdviceText = QStringLiteral(
                "Tegn på potentiel underekstraktion: Slutfasen havde stadig høj koncentration (M5 %1, R5 %2%, sidste 5 g = %3% af udtrækket, ~%4% ekstra solids tilbage). Hvis shottet smager syrligt eller kort, kan du forlænge ratio mod ~%5 g%6.")
                .arg(QString::number(result->tailMean5g, 'f', 2),
                     QString::number(result->tailRatio5g * 100.0, 'f', 1),
                     QString::number(result->recentContributionShare5g * 100.0, 'f', 1),
                     QString::number(remPct, 'f', 1),
                     QString::number(targetLongerG, 'f', 1),
                     longerRatioStr);
        }
    } else if (tastesBitterOrThin && result->massAt90PctIntegralG > 0.0) {
        const double targetCutG = std::max(result->massAt90PctIntegralG, result->totalMassG - 3.5);
        const QString targetRatioStr = hasDose
            ? QStringLiteral(" (1:%1)").arg(QString::number(targetCutG / doseWeightG, 'f', 2))
            : QString();
        result->ratioAdviceText = QStringLiteral(
            "Anbefaling (bitter/tynd smag): 90% af kaffens solids var ude ved %1 g — prøv at stoppe shottet tidligere omkring %2 g%3 for at undgå bitre halestoffer.")
            .arg(QString::number(result->massAt90PctIntegralG, 'f', 1),
                 QString::number(targetCutG, 'f', 1),
                 targetRatioStr);
    } else if (bestShot && bestRatio > 0.0) {
        const QString stopRatioStr = (currentRatio > 0.0)
            ? QStringLiteral(" (1:%1)").arg(QString::number(currentRatio, 'f', 2))
            : QString();
        result->ratioAdviceText = QStringLiteral(
            "Anbefaling: Stopvægten (%1 g%2) rammer koncentrations-tærsklen godt (sidste 5 g = %3% udtræk) og matcher dit sweet-spot på denne bønne (%4/100 ved %5 g / 1:%6).")
            .arg(QString::number(result->totalMassG, 'f', 1),
                 stopRatioStr,
                 QString::number(result->recentContributionShare5g * 100.0, 'f', 1),
                 QString::number(bestShot->enjoyment),
                 QString::number(bestWeightG, 'f', 1),
                 QString::number(bestRatio, 'f', 2));
    }
}

QString PortalTailAnalyzer::statusToString(Status status)
{
    switch (status) {
    case Status::Valid:
        return QStringLiteral("valid");
    case Status::NoPortalData:
        return QStringLiteral("no_portal_data");
    case Status::RequiresPhysicalScale:
        return QStringLiteral("requires_physical_scale");
    case Status::InsufficientWeightData:
        return QStringLiteral("insufficient_weight_data");
    case Status::InsufficientCoverage:
        return QStringLiteral("insufficient_coverage");
    }
    return QStringLiteral("unknown");
}

QString PortalTailAnalyzer::weightSourceToString(WeightSource source)
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

QString PortalTailAnalyzer::mountingModeToString(MountingMode mode)
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
