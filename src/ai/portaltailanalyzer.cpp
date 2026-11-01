#include "ai/portaltailanalyzer.h"

#include <QDateTime>

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
    double cumulativeCoveredMass = 0.0;
    qsizetype segmentIndex = -1;
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
    bool interrupted = false;
    for (auto s : rawSamples) {
        if (isFinitePortalSample(s) && s.ecRaw >= 0.0
            && (cleaned.isEmpty() || s.time > cleaned.last().time)) {
            s.breakBefore = s.breakBefore || interrupted;
            interrupted = false;
            cleaned.append(s);
            sumTempC += s.temperatureC;
            if (s.temperatureC > maxTempC)
                maxTempC = s.temperatureC;
        } else interrupted = true;
    }

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
std::optional<double> evaluatePortalEcAtTime(const QVector<PortalSegment>& segments, double tQuery,
                                            qsizetype* segmentIndex)
{
    *segmentIndex = -1;
    if (segments.isEmpty() || !std::isfinite(tQuery))
        return std::nullopt;

    const PortalSegment& firstSeg = segments.first();
    if (tQuery < firstSeg.startTime()) {
        if (firstSeg.startTime() - tQuery <= PortalTailAnalyzer::kBoundaryClampSeconds) {
            *segmentIndex = 0;
            return firstSeg.samples.first().ecRaw;
        }
        return std::nullopt;
    }

    const PortalSegment& lastSeg = segments.last();
    if (tQuery > lastSeg.endTime()) {
        if (tQuery - lastSeg.endTime() <= PortalTailAnalyzer::kBoundaryClampSeconds) {
            *segmentIndex = segments.size() - 1;
            return lastSeg.samples.last().ecRaw;
        }
        return std::nullopt;
    }

    for (qsizetype index = 0; index < segments.size(); ++index) {
        const auto& seg = segments[index];
        if (tQuery < seg.startTime() || tQuery > seg.endTime())
            continue;

        *segmentIndex = index;

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

    const double dm = right.massG - left.massG;
    if (!left.ecRaw || !right.ecRaw || left.segmentIndex != right.segmentIndex)
        return left.massG;
    const double areaPerG = (targetIntegral - left.cumulativeIntegral) / dm;
    const double deltaEc = *right.ecRaw - *left.ecRaw;
    const double endEc = std::sqrt(std::max(0.0, *left.ecRaw * *left.ecRaw + 2.0 * deltaEc * areaPerG));
    const double alpha = (*left.ecRaw + endEc) > 0.0
        ? 2.0 * areaPerG / (*left.ecRaw + endEc) : 0.0;
    return left.massG + std::clamp(alpha, 0.0, 1.0) * dm;
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
    if (!left.ecRaw || !right.ecRaw || left.segmentIndex != right.segmentIndex)
        return left.cumulativeIntegral;
    const double ecAtMass = *left.ecRaw + alpha * (*right.ecRaw - *left.ecRaw);
    return left.cumulativeIntegral + 0.5 * (*left.ecRaw + ecAtMass) * alpha * dm;
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
    if (!left.ecRaw.has_value() || !right.ecRaw.has_value()
        || left.segmentIndex != right.segmentIndex)
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
               && uniqueKnots[i + 1].segmentIndex == uniqueKnots[i].segmentIndex
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
            if (!uniqueKnots[j].ecRaw.has_value()
                || uniqueKnots[j].segmentIndex != uniqueKnots[highIdx].segmentIndex)
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
    m["thermalDriftWarning"] = thermalDriftWarning;
    m["estimateReliable"] = estimateReliable;
    m["calibrationEligible"] = calibrationEligible;
    m["uncertaintyReasons"] = uncertaintyReasons;

    m["massAt85PctIntegralG"] = massAt85PctIntegralG;
    m["massAt90PctIntegralG"] = massAt90PctIntegralG;
    m["post1to2MassG"] = post1to2MassG;
    m["post1to2ContributionShare"] = post1to2ContributionShare;
    m["ratioAdvice"] = ratioAdvice;
    m["lowTailConcentration"] = lowTailConcentration;
    m["possibleUnderExtraction"] = possibleUnderExtraction;
    m["bestHistoricalWeightG"] = bestHistoricalWeightG;
    m["bestHistoricalRatio"] = bestHistoricalRatio;
    m["bestHistoricalEnjoyment"] = bestHistoricalEnjoyment;

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

    QVector<QPointF> weightClean;
    weightClean.reserve(cumulativeWeight.size());
    double runningMaxMass = 0.0;
    for (const auto& pt : cumulativeWeight) {
        if (!isFiniteWeightPoint(pt)
            || (!weightClean.isEmpty() && pt.x() < weightClean.last().x())
            || pt.y() < runningMaxMass - 2.0) {
            out.status = Status::InsufficientWeightData;
            return out;
        }
        if (!weightClean.isEmpty() && pt.x() == weightClean.last().x()) {
            if (pt.y() != weightClean.last().y()) {
                out.status = Status::InsufficientWeightData;
                return out;
            }
            continue;
        }
        runningMaxMass = std::max(runningMaxMass, pt.y());
        weightClean.append(QPointF(pt.x(), runningMaxMass));
    }

    out.weightSampleCount = static_cast<int>(weightClean.size());
    if (weightClean.size() < 2) {
        out.status = Status::InsufficientWeightData;
        return out;
    }

    const double firstTime = weightClean.first().x();
    QVector<MassKnot> knots;
    knots.reserve(weightClean.size() + portalSamples.size() + 1);
    for (const auto& pt : weightClean) {
        MassKnot knot;
        knot.timeSec = pt.x();
        knot.massG = pt.y();
        knots.append(knot);
    }

    // Keep scale plateaus and every EC change; sparse scale packets must not
    // erase the chemistry observed between them.
    const auto addTime = [&](double time) {
        if (!std::isfinite(time) || time <= firstTime || time >= weightClean.last().x()) return;
        const auto right = std::lower_bound(weightClean.cbegin(), weightClean.cend(), time,
            [](const QPointF& pt, double t) { return pt.x() < t; });
        const auto left = right - 1;
        const double alpha = (time - left->x()) / (right->x() - left->x());
        MassKnot knot;
        knot.timeSec = time;
        knot.massG = left->y() + alpha * (right->y() - left->y());
        knots.append(knot);
    };
    for (const auto& segment : segments)
        for (const auto& sample : segment.samples)
            addTime(sample.time - options.lagOffsetSec);
    addTime(options.activeStopTimeSec);
    std::stable_sort(knots.begin(), knots.end(), [](const MassKnot& a, const MassKnot& b) {
        return a.timeSec < b.timeSec;
    });
    for (auto& knot : knots)
        knot.ecRaw = evaluatePortalEcAtTime(segments, knot.timeSec + options.lagOffsetSec,
                                          &knot.segmentIndex);

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
        if (dm > 0.0 && knots[i - 1].ecRaw.has_value() && knots[i].ecRaw.has_value()
            && knots[i - 1].segmentIndex == knots[i].segmentIndex) {
            const double ecMid = 0.5 * (*knots[i - 1].ecRaw + *knots[i].ecRaw);
            const double stepIntegral = ecMid * dm;
            totalIntegral += stepIntegral;
            coveredMass += dm;
            if (!hasActiveStop || knots[i].timeSec <= options.activeStopTimeSec)
                activeIntegral += stepIntegral;
        }
        knots[i].cumulativeIntegral = totalIntegral;
        knots[i].cumulativeCoveredMass = coveredMass;
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

    double tailCoveredMass = 0.0;
    const double tailStart = out.totalMassG - 5.0;
    for (qsizetype i = 1; i < knots.size(); ++i) {
        if (knots[i].cumulativeCoveredMass > knots[i - 1].cumulativeCoveredMass)
            tailCoveredMass += std::max(0.0, knots[i].massG - std::max(tailStart, knots[i - 1].massG));
    }
    if (out.coverageRatio < kMinCoverageRatio || tailCoveredMass < 5.0 - 1e-6) {
        out.status = Status::InsufficientCoverage;
        out.estimateReliable = false;
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
    const double brewRatio = (options.doseWeightG > 0.0 && std::isfinite(options.doseWeightG))
                             ? (out.totalMassG / options.doseWeightG)
                             : 0.0;
    out.thermalDriftWarning = (out.maxTempC >= 68.0) || (brewRatio > 2.55 && out.maxTempC >= 64.0);

    if (out.instabilityDipCount > 0)
        out.uncertaintyReasons.append(QStringLiteral("mid_shot_dip"));
    if (out.thermalDriftWarning)
        out.uncertaintyReasons.append(QStringLiteral("thermal_drift"));

    if (out.gapCount > 0 || out.coverageRatio < 0.999999)
        out.uncertaintyReasons.append(QStringLiteral("incomplete_coverage"));
    if (out.mountingMode == MountingMode::Unknown)
        out.uncertaintyReasons.append(QStringLiteral("unknown_mounting"));
    out.estimateReliable = out.uncertaintyReasons.isEmpty();
    out.calibrationEligible = out.estimateReliable;

    // --- Shot completion / cut-point metrics ---
    if (iEnd > 0.0) {
        out.massAt85PctIntegralG = interpolateMassAtIntegral(uniqueKnots, 0.85 * iEnd);
        out.massAt90PctIntegralG = interpolateMassAtIntegral(uniqueKnots, 0.90 * iEnd);
    }

    const bool hasValidDose = (options.doseWeightG > 0.0 && std::isfinite(options.doseWeightG));
    const double m1to2 = hasValidDose ? (2.0 * options.doseWeightG) : 0.0;
    if (hasValidDose && mEnd > m1to2 + 1.0 && iEnd > 0.0) {
        out.post1to2MassG = mEnd - m1to2;
        const double iAt1to2 = interpolateIntegralAtMass(uniqueKnots, m1to2);
        out.post1to2ContributionShare = std::clamp((iEnd - iAt1to2) / iEnd, 0.0, 1.0);
    }

    out.lowTailConcentration = out.estimateReliable && out.instabilityDipCount == 0
        && out.tailRatio5g > 0.0 && out.tailRatio5g <= 0.16 && out.tailSlope5gPerG <= 0.0;
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

PortalTailAnalyzer::WeightSource PortalTailAnalyzer::weightSourceFromString(const QString& source)
{
    if (source == QLatin1String("physical_scale")) return WeightSource::PhysicalScale;
    if (source == QLatin1String("flow_scale_virtual")) return WeightSource::FlowScaleVirtual;
    return WeightSource::Unknown;
}

PortalTailAnalyzer::MountingMode PortalTailAnalyzer::mountingModeFromString(const QString& mode)
{
    if (mode == QLatin1String("onScale")) return MountingMode::OnScale;
    if (mode == QLatin1String("independent")) return MountingMode::Independent;
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
    m["excludedShotCount"] = excludedShotCount;
    m["referenceShots"] = referenceShots;
    m["minMeanRawEc"] = minMeanRawEc;
    m["maxMeanRawEc"] = maxMeanRawEc;
    m["minRatio"] = minRatio;
    m["maxRatio"] = maxRatio;
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
    qint64 excludeShotId,
    MountingMode targetMountingMode)
{
    CalibrationFit fit;

    QVector<CalibrationPoint> validAll;
    validAll.reserve(points.size());
    for (const auto& p : points) {
        if (p.estimateReliable && p.mountingMode == targetMountingMode
            && targetMountingMode != MountingMode::Unknown
            && std::isfinite(p.meanRawEcPerG) && p.meanRawEcPerG > 0.1
            && std::isfinite(p.measuredTdsPct) && p.measuredTdsPct > 0.1) {
            validAll.append(p);
        }
    }

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

    if (scope == QLatin1String("none"))
        scope = targetBagId > 0 ? QStringLiteral("bag") : QStringLiteral("bean");
    fit.scope = scope;
    for (const auto& p : points) {
        const bool sameCoffee = scope == QLatin1String("bag") ? p.bagId == targetBagId
            : !targetBeanBrand.trimmed().isEmpty() && !targetBeanType.trimmed().isEmpty()
                && p.beanBrand.trimmed().compare(targetBeanBrand.trimmed(), Qt::CaseInsensitive) == 0
                && p.beanType.trimmed().compare(targetBeanType.trimmed(), Qt::CaseInsensitive) == 0;
        if (!sameCoffee || (excludeShotId > 0 && p.shotId == excludeShotId) || p.measuredTdsPct <= 0.0)
            continue;
        const bool included = std::any_of(selected.cbegin(), selected.cend(),
            [&p](const CalibrationPoint& reference) { return reference.shotId == p.shotId; });
        const QString reason = included ? QString()
            : !std::isfinite(p.meanRawEcPerG) || p.meanRawEcPerG <= 0.1
                ? QStringLiteral("missing_ec")
                : p.mountingMode != targetMountingMode || targetMountingMode == MountingMode::Unknown
                    ? QStringLiteral("mounting_mismatch") : QStringLiteral("measurement_warning");
        if (!included) ++fit.excludedShotCount;
        fit.referenceShots.append(QVariantMap{
            {QStringLiteral("shotId"), p.shotId},
            {QStringLiteral("dateTime"), QDateTime::fromSecsSinceEpoch(p.timestamp).toString(Qt::ISODate)},
            {QStringLiteral("ratio"), p.doseWeightG > 0.0 ? p.finalWeightG / p.doseWeightG : 0.0},
            {QStringLiteral("measuredTdsPct"), p.measuredTdsPct},
            {QStringLiteral("meanRawEc"), p.meanRawEcPerG},
            {QStringLiteral("individualK"), p.meanRawEcPerG > 0.0 ? p.measuredTdsPct / p.meanRawEcPerG : 0.0},
            {QStringLiteral("included"), included},
            {QStringLiteral("exclusionReason"), reason}});
    }
    if (selected.isEmpty())
        return fit;

    fit.minMeanRawEc = fit.maxMeanRawEc = selected.first().meanRawEcPerG;
    for (const auto& p : selected) {
        fit.minMeanRawEc = std::min(fit.minMeanRawEc, p.meanRawEcPerG);
        fit.maxMeanRawEc = std::max(fit.maxMeanRawEc, p.meanRawEcPerG);
        const double ratio = p.doseWeightG > 0.0 ? p.finalWeightG / p.doseWeightG : 0.0;
        if (!std::isfinite(ratio) || ratio <= 0.0)
            continue;
        fit.minRatio = fit.minRatio > 0.0 ? std::min(fit.minRatio, ratio) : ratio;
        fit.maxRatio = std::max(fit.maxRatio, ratio);
    }

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
    if (fit.shotCount < 5) {
        result->estimateReliable = false;
        result->uncertaintyReasons.append(QStringLiteral("preliminary_calibration"));
    }

    const double ratio = doseWeightG > 0.0 ? finalWeightG / doseWeightG : 0.0;
    if (fit.shotCount >= 2 && ((fit.minMeanRawEc > 0.0 && (result->meanRawEcPerG < fit.minMeanRawEc - 1e-6
                                              || result->meanRawEcPerG > fit.maxMeanRawEc + 1e-6))
        || (fit.minRatio > 0.0 && ratio > 0.0 && (ratio < fit.minRatio - 1e-6 || ratio > fit.maxRatio + 1e-6)))) {
        result->estimateReliable = false;
        result->uncertaintyReasons.append(QStringLiteral("outside_calibration_range"));
    }

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
                                             const QString& currentNotes,
                                             qint64 targetEquipmentId,
                                             const QString& targetProfileName)
{
    if (!result || !result->valid)
        return;

    result->possibleUnderExtraction = false;
    result->ratioAdvice = QStringLiteral("taste_first");

    const CalibrationPoint* bestShot = nullptr;
    for (const auto& pt : historyPoints) {
        if (pt.shotId == currentShotId || pt.enjoyment < 75 || pt.enjoyment > 100
            || pt.finalWeightG <= 5.0 || pt.doseWeightG <= 5.0
            || targetEquipmentId <= 0 || pt.equipmentId != targetEquipmentId
            || targetProfileName.isEmpty() || pt.profileName != targetProfileName
            || std::abs(pt.doseWeightG - doseWeightG) > 0.5)
            continue;
        const bool sameCoffee = targetBagId > 0 ? pt.bagId == targetBagId
            : (!targetBeanBrand.trimmed().isEmpty() && !targetBeanType.trimmed().isEmpty()
               && pt.beanBrand.trimmed().compare(targetBeanBrand.trimmed(), Qt::CaseInsensitive) == 0
               && pt.beanType.trimmed().compare(targetBeanType.trimmed(), Qt::CaseInsensitive) == 0);
        if (sameCoffee && (!bestShot || pt.enjoyment > bestShot->enjoyment))
            bestShot = &pt;
    }

    result->bestHistoricalWeightG = bestShot ? bestShot->finalWeightG : 0.0;
    result->bestHistoricalRatio = bestShot ? bestShot->finalWeightG / bestShot->doseWeightG : 0.0;
    result->bestHistoricalEnjoyment = bestShot ? bestShot->enjoyment : 0;

    // Only explicit feedback on this cup informs its taste interpretation.
    Q_UNUSED(currentEnjoyment);
    Q_UNUSED(currentNotes);
    const bool sour = currentTasteBalance.compare(QLatin1String("sour"), Qt::CaseInsensitive) == 0;
    const bool bitterOrThin = currentTasteBalance.compare(QLatin1String("bitter"), Qt::CaseInsensitive) == 0
        || currentTasteBody.compare(QLatin1String("thin"), Qt::CaseInsensitive) == 0;
    if (!result->estimateReliable) {
        result->ratioAdvice = QStringLiteral("uncertain");
        return;
    }
    if (sour && result->tailRatio5g >= 0.22) {
        result->possibleUnderExtraction = true;
        result->ratioAdvice = QStringLiteral("try_longer");
    } else if (bitterOrThin && result->lowTailConcentration) {
        result->ratioAdvice = QStringLiteral("try_shorter");
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
