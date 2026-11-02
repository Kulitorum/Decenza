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
    double temperatureC = 0.0;
    std::optional<double> auxiliaryTemperatureRaw;
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
                                           qsizetype* outValidCount,
                                           qsizetype* outGapCount,
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
        if (!cleaned.isEmpty() && isFinitePortalSample(s) && s.ecRaw >= 0.0
            && s.time == cleaned.last().time) {
            interrupted = interrupted || s.breakBefore;
            continue;
        }
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
        *outValidCount = cleaned.size();
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
    qsizetype gaps = 0;

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

// Evaluates sensor fields at query time `tQuery` across contiguous PORTAL segments.
// - Inside a segment: linear interpolation (or exact value for 1-point segment).
// - Within `kBoundaryClampSeconds` before the first segment or after the last
//   segment: clamps to the nearest boundary sample (handles minor SAW settling
//   overhang where the final scale packet arrives tens of milliseconds after
//   the last BLE notification from PORTAL).
// - Inside an internal gap between segments, or outside `kBoundaryClampSeconds`:
//   returns std::nullopt so the mass step is counted as uncovered.
std::optional<PortalSample> evaluatePortalAtTime(const QVector<PortalSegment>& segments, double tQuery,
                                            qsizetype* segmentIndex)
{
    *segmentIndex = -1;
    if (segments.isEmpty() || !std::isfinite(tQuery))
        return std::nullopt;

    const PortalSegment& firstSeg = segments.first();
    if (tQuery < firstSeg.startTime()) {
        if (firstSeg.startTime() - tQuery <= PortalTailAnalyzer::kBoundaryClampSeconds) {
            *segmentIndex = 0;
            return firstSeg.samples.first();
        }
        return std::nullopt;
    }

    const PortalSegment& lastSeg = segments.last();
    if (tQuery > lastSeg.endTime()) {
        if (tQuery - lastSeg.endTime() <= PortalTailAnalyzer::kBoundaryClampSeconds) {
            *segmentIndex = segments.size() - 1;
            return lastSeg.samples.last();
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
            return pts.first();

        auto it = std::lower_bound(
            pts.begin(), pts.end(), tQuery,
            [](const PortalSample& s, double t) { return s.time < t; });

        if (it == pts.begin() || (it != pts.end() && it->time == tQuery))
            return *it;
        if (it == pts.end())
            return pts.last();

        const PortalSample& right = *it;
        const PortalSample& left = *(it - 1);
        const double dt = right.time - left.time;
        if (dt <= 1e-9)
            return right;

        const double alpha = (tQuery - left.time) / dt;
        PortalSample value{tQuery, left.ecRaw + alpha * (right.ecRaw - left.ecRaw),
            left.temperatureC + alpha * (right.temperatureC - left.temperatureC), false, std::nullopt};
        if (left.auxiliaryTemperatureRaw && right.auxiliaryTemperatureRaw)
            value.auxiliaryTemperatureRaw = *left.auxiliaryTemperatureRaw
                + alpha * (*right.auxiliaryTemperatureRaw - *left.auxiliaryTemperatureRaw);
        return value;
    }

    // tQuery fell between two segments.
    return std::nullopt;
}

double interpolateMassAtIntegral(const QVector<MassKnot>& knots, double targetIntegral)
{
    if (knots.isEmpty())
        return 0.0;
    if (targetIntegral <= knots.first().cumulativeIntegral)
        return knots.first().massG;
    if (targetIntegral >= knots.last().cumulativeIntegral)
        return knots.last().massG;

    auto it = std::lower_bound(
        knots.begin(), knots.end(), targetIntegral,
        [](const MassKnot& k, double val) { return k.cumulativeIntegral < val; });

    if (it == knots.begin())
        return it->massG;
    if (it == knots.end())
        return knots.last().massG;

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

double interpolateIntegralAtMass(const QVector<MassKnot>& knots, double targetMass)
{
    if (knots.isEmpty())
        return 0.0;
    if (targetMass <= knots.first().massG)
        return knots.first().cumulativeIntegral;
    if (targetMass >= knots.last().massG)
        return knots.last().cumulativeIntegral;

    auto it = std::lower_bound(
        knots.begin(), knots.end(), targetMass,
        [](const MassKnot& k, double m) { return k.massG < m; });

    if (it == knots.begin())
        return it->cumulativeIntegral;
    if (it == knots.end())
        return knots.last().cumulativeIntegral;

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

std::optional<double> interpolateEcAtMass(const QVector<MassKnot>& knots, double targetMass)
{
    if (knots.isEmpty())
        return std::nullopt;
    if (targetMass <= knots.first().massG)
        return knots.first().ecRaw;
    if (targetMass >= knots.last().massG)
        return knots.last().ecRaw;

    auto it = std::lower_bound(
        knots.begin(), knots.end(), targetMass,
        [](const MassKnot& k, double m) { return k.massG < m; });

    if (it == knots.begin())
        return it->ecRaw;
    if (it == knots.end())
        return knots.last().ecRaw;

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
    const QVector<MassKnot>& knots,
    double mainPeakMassG)
{
    QVector<PortalTailAnalyzer::InstabilityDip> dips;
    if (knots.size() < 3)
        return dips;

    qsizetype startIdx = 0;
    while (startIdx < knots.size()
           && knots[startIdx].massG < std::max(PortalTailAnalyzer::kActivePeakMinMassG, mainPeakMassG)) {
        ++startIdx;
    }
    if (startIdx >= knots.size() - 2)
        return dips;

    qsizetype i = startIdx;
    while (i + 2 < knots.size()) {
        if (!knots[i].ecRaw.has_value()) {
            ++i;
            continue;
        }

        // Advance to local pre-dip high
        while (i + 1 < knots.size()
               && knots[i + 1].ecRaw.has_value()
               && knots[i + 1].segmentIndex == knots[i].segmentIndex
               && *knots[i + 1].ecRaw >= *knots[i].ecRaw) {
            ++i;
        }
        const qsizetype highIdx = i;
        const double highEc = *knots[highIdx].ecRaw;

        // Walk down to local valley while looking for a recovery of at least kMinInstabilityProminenceRawEc
        qsizetype valleyIdx = highIdx;
        double valleyEc = highEc;
        qsizetype recoveryIdx = -1;
        double recoveryEc = valleyEc;

        for (qsizetype j = highIdx + 1; j < knots.size(); ++j) {
            if (!knots[j].ecRaw.has_value()
                || knots[j].segmentIndex != knots[highIdx].segmentIndex)
                break;
            const double curEc = *knots[j].ecRaw;
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
                if (j + 1 < knots.size()
                    && knots[j + 1].ecRaw.has_value()
                    && *knots[j + 1].ecRaw < curEc) {
                    break;
                }
            }
        }

        const double drop = highEc - valleyEc;
        const double recovery = recoveryEc - valleyEc;
        const double prominence = std::min(drop, recovery);
        if (recoveryIdx > valleyIdx && prominence >= PortalTailAnalyzer::kMinInstabilityProminenceRawEc) {
            PortalTailAnalyzer::InstabilityDip dip;
            dip.startMassG = knots[highIdx].massG;
            dip.startTimeSec = knots[highIdx].timeSec;
            dip.valleyMassG = knots[valleyIdx].massG;
            dip.valleyTimeSec = knots[valleyIdx].timeSec;
            dip.recoveryMassG = knots[recoveryIdx].massG;
            dip.recoveryTimeSec = knots[recoveryIdx].timeSec;
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



// A drop larger than this is a scale reset or tare, not settling noise.
constexpr double kMaxScaleRegressionG = 2.0;
// Bounds the affine intercept to a plausible TDS offset.
constexpr double kMaxAffineInterceptTdsPct = 5.0;

bool positiveFinite(double value)
{
    return std::isfinite(value) && value > 0.0;
}

// Rejects reversed clocks, non-finite points and scale resets; otherwise
// returns the curve made monotonic, one point per timestamp.
std::optional<QVector<QPointF>> cleanWeightCurve(const QVector<QPointF>& raw)
{
    QVector<QPointF> clean;
    clean.reserve(raw.size());
    double runningMaxMass = 0.0;
    for (const auto& pt : raw) {
        if (!isFiniteWeightPoint(pt)
            || (!clean.isEmpty() && pt.x() < clean.last().x())
            || pt.y() < runningMaxMass - kMaxScaleRegressionG)
            return std::nullopt;
        runningMaxMass = std::max(runningMaxMass, pt.y());
        // Snapshots sharing a millisecond collapse to the running maximum.
        if (!clean.isEmpty() && pt.x() == clean.last().x()) {
            clean.last().setY(runningMaxMass);
            continue;
        }
        clean.append(QPointF(pt.x(), runningMaxMass));
    }
    return clean;
}

// One knot per scale point plus one at every PORTAL sample and the active
// stop, so sparse scale packets do not erase the EC observed between them.
QVector<MassKnot> buildMassKnots(const QVector<QPointF>& weight,
                                 const QVector<PortalSegment>& segments,
                                 double activeStopTimeSec)
{
    QVector<MassKnot> knots;
    knots.reserve(weight.size());
    for (const auto& pt : weight) {
        MassKnot knot;
        knot.timeSec = pt.x();
        knot.massG = pt.y();
        knots.append(knot);
    }

    const double firstTime = weight.first().x();
    const double lastTime = weight.last().x();
    const auto addTime = [&](double time) {
        if (!std::isfinite(time) || time <= firstTime || time >= lastTime) return;
        const auto right = std::lower_bound(weight.cbegin(), weight.cend(), time,
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
            addTime(sample.time);
    addTime(activeStopTimeSec);
    std::stable_sort(knots.begin(), knots.end(), [](const MassKnot& a, const MassKnot& b) {
        return a.timeSec < b.timeSec;
    });

    for (auto& knot : knots) {
        const auto value = evaluatePortalAtTime(segments, knot.timeSec, &knot.segmentIndex);
        if (value) {
            knot.ecRaw = value->ecRaw;
            knot.temperatureC = value->temperatureC;
            knot.auxiliaryTemperatureRaw = value->auxiliaryTemperatureRaw;
        }
    }
    return knots;
}

struct MassIntegrals {
    double total = 0.0;
    double active = 0.0;
    double coveredMass = 0.0;
    double temperature = 0.0;
    double auxiliary = 0.0;
    double auxiliaryCoveredMass = 0.0;
};

// Trapezoidal integration of EC(m) dm over steps whose both ends lie in one
// PORTAL segment; fills each knot's cumulative integral and covered mass.
MassIntegrals integrateMassSpace(QVector<MassKnot>* knots, double activeStopTimeSec)
{
    MassIntegrals sums;
    const bool hasActiveStop = positiveFinite(activeStopTimeSec);
    auto& k = *knots;
    k[0].cumulativeIntegral = 0.0;
    for (qsizetype i = 1; i < k.size(); ++i) {
        const double dm = k[i].massG - k[i - 1].massG;
        if (dm > 0.0 && k[i - 1].ecRaw && k[i].ecRaw && k[i - 1].segmentIndex == k[i].segmentIndex) {
            const double stepIntegral = 0.5 * (*k[i - 1].ecRaw + *k[i].ecRaw) * dm;
            sums.total += stepIntegral;
            sums.coveredMass += dm;
            sums.temperature += 0.5 * (k[i - 1].temperatureC + k[i].temperatureC) * dm;
            if (k[i - 1].auxiliaryTemperatureRaw && k[i].auxiliaryTemperatureRaw) {
                sums.auxiliary += 0.5 * (*k[i - 1].auxiliaryTemperatureRaw + *k[i].auxiliaryTemperatureRaw) * dm;
                sums.auxiliaryCoveredMass += dm;
            }
            if (!hasActiveStop || k[i].timeSec <= activeStopTimeSec)
                sums.active += stepIntegral;
        }
        k[i].cumulativeIntegral = sums.total;
        k[i].cumulativeCoveredMass = sums.coveredMass;
    }
    return sums;
}

double tailCoveredMassG(const QVector<MassKnot>& knots, double totalMassG)
{
    double covered = 0.0;
    const double tailStart = totalMassG - PortalTailAnalyzer::kTailWindowG;
    for (qsizetype i = 1; i < knots.size(); ++i) {
        if (knots[i].cumulativeCoveredMass > knots[i - 1].cumulativeCoveredMass)
            covered += std::max(0.0, knots[i].massG - std::max(tailStart, knots[i - 1].massG));
    }
    return covered;
}

// M2/M5/R5/T5 over the last 2 g and kTailWindowG of cup mass.
void computeTailFeatures(const QVector<MassKnot>& knots, PortalTailAnalyzer::Result* out)
{
    constexpr double kWindowG = PortalTailAnalyzer::kTailWindowG;
    const double mEnd = out->totalMassG;
    const double iEnd = knots.last().cumulativeIntegral;
    const double iMinus2 = interpolateIntegralAtMass(knots, mEnd - 2.0);
    const double iMinus5 = interpolateIntegralAtMass(knots, mEnd - kWindowG);

    out->tailMean2g = (iEnd - iMinus2) / 2.0;
    out->tailMean5g = (iEnd - iMinus5) / kWindowG;
    if (out->mainPeakRawEc > 0.0)
        out->tailRatio5g = out->tailMean5g / out->mainPeakRawEc;
    if (iEnd > 0.0)
        out->recentContributionShare5g = (iEnd - iMinus5) / iEnd;

    // Least-squares slope over a uniform 51-point mass grid.
    constexpr int kGridPoints = 51;
    const double mStart = mEnd - kWindowG;
    double sumM = 0.0;
    double sumEc = 0.0;
    double sumMM = 0.0;
    double sumMEc = 0.0;
    int count = 0;
    for (int idx = 0; idx < kGridPoints; ++idx) {
        const double dm = kWindowG * static_cast<double>(idx) / static_cast<double>(kGridPoints - 1);
        const std::optional<double> ec = interpolateEcAtMass(knots, mStart + dm);
        if (!ec)
            continue;
        sumM += dm;
        sumEc += *ec;
        sumMM += dm * dm;
        sumMEc += dm * *ec;
        ++count;
    }
    if (count >= 2) {
        const double n = static_cast<double>(count);
        const double denom = n * sumMM - sumM * sumM;
        if (std::abs(denom) > 1e-9)
            out->tailSlope5gPerG = (n * sumMEc - sumM * sumEc) / denom;
    }
}

void assessMeasurement(const PortalTailAnalyzer::Options& options, PortalTailAnalyzer::Result* out)
{
    const double beverageWeightG = positiveFinite(options.finalWeightG) ? options.finalWeightG : out->totalMassG;
    const double brewRatio = positiveFinite(options.doseWeightG) ? beverageWeightG / options.doseWeightG : 0.0;
    // Conservative screening heuristics, not published validity limits (docs/PORTAL_EVIDENCE.md).
    out->thermalDriftWarning = out->maxTempC >= 68.0 || (brewRatio > 2.55 && out->maxTempC >= 64.0);

    if (out->instabilityDipCount > 0)
        out->uncertaintyReasons.append(QStringLiteral("mid_shot_dip"));
    if (out->thermalDriftWarning)
        out->uncertaintyReasons.append(QStringLiteral("thermal_drift"));
    if (out->gapCount > 0 || out->totalMassG - out->coveredMassG > PortalTailAnalyzer::kCupMassToleranceG + 1e-6)
        out->uncertaintyReasons.append(QStringLiteral("incomplete_coverage"));
    if (positiveFinite(options.finalWeightG)
        && std::abs(options.finalWeightG - out->totalMassG) > PortalTailAnalyzer::kCupMassToleranceG + 1e-6)
        out->uncertaintyReasons.append(QStringLiteral("cup_mass_mismatch"));
    if (out->mountingMode == PortalTailAnalyzer::MountingMode::Unknown)
        out->uncertaintyReasons.append(QStringLiteral("unknown_mounting"));
    out->measurementReliable = out->uncertaintyReasons.isEmpty();
}

using CalibrationPoint = PortalTailAnalyzer::CalibrationPoint;
using CalibrationScope = PortalTailAnalyzer::CalibrationScope;

// "Same coffee" for calibration and ratio advice: one bag, or one case-folded brand + bean name.
struct CoffeeTarget {
    qint64 bagId = -1;
    QString brandKey;
    QString typeKey;

    CoffeeTarget(qint64 bag, const QString& brand, const QString& type)
        : bagId(bag)
        , brandKey(PortalTailAnalyzer::coffeeNameKey(brand))
        , typeKey(PortalTailAnalyzer::coffeeNameKey(type))
    {}

    bool matches(const CalibrationPoint& p, CalibrationScope scope) const
    {
        if (scope == CalibrationScope::Bag)
            return bagId > 0 && p.bagId == bagId;
        return !brandKey.isEmpty() && !typeKey.isEmpty()
            && PortalTailAnalyzer::coffeeNameKey(p.beanBrand) == brandKey
            && PortalTailAnalyzer::coffeeNameKey(p.beanType) == typeKey;
    }
};

double brewRatio(const CalibrationPoint& p)
{
    return p.doseWeightG > 0.0 ? p.finalWeightG / p.doseWeightG : 0.0;
}

// One shot's TDS-per-EC factor, the single-reference proportional k.
double individualK(double measuredTdsPct, double meanRawEcPerG)
{
    return meanRawEcPerG > 0.0 ? measuredTdsPct / meanRawEcPerG : 0.0;
}

void appendReferenceRows(PortalTailAnalyzer::CalibrationFit* fit,
                         const QVector<CalibrationPoint>& points,
                         const QVector<CalibrationPoint>& selected,
                         const CoffeeTarget& target, qint64 excludeShotId,
                         PortalTailAnalyzer::MountingMode targetMountingMode)
{
    for (const auto& p : points) {
        if (!target.matches(p, fit->scope) || (excludeShotId > 0 && p.shotId == excludeShotId)
            || p.measuredTdsPct <= 0.0)
            continue;
        const bool included = std::any_of(selected.cbegin(), selected.cend(),
            [&p](const CalibrationPoint& reference) { return reference.shotId == p.shotId; });
        if (!included)
            ++fit->excludedShotCount;
        if (fit->referenceShots.size() >= PortalTailAnalyzer::kMaxReferenceShots)
            continue;
        const QString reason = included ? QString()
            : !std::isfinite(p.meanRawEcPerG) || p.meanRawEcPerG <= 0.1
                ? QStringLiteral("missing_ec")
                : p.mountingMode != targetMountingMode || targetMountingMode == PortalTailAnalyzer::MountingMode::Unknown
                    ? QStringLiteral("mounting_mismatch") : QStringLiteral("measurement_warning");
        fit->referenceShots.append(QVariantMap{
            {QStringLiteral("shotId"), p.shotId},
            {QStringLiteral("dateTime"), QDateTime::fromSecsSinceEpoch(p.timestamp).toString(Qt::ISODate)},
            {QStringLiteral("ratio"), brewRatio(p)},
            {QStringLiteral("measuredTdsPct"), p.measuredTdsPct},
            {QStringLiteral("meanRawEc"), p.meanRawEcPerG},
            {QStringLiteral("individualK"), individualK(p.measuredTdsPct, p.meanRawEcPerG)},
            {QStringLiteral("included"), included},
            {QStringLiteral("exclusionReason"), reason}});
    }
}

struct RegressionSums {
    double n = 0.0;
    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
};

// TDS% = k * EC, with exact leave-one-out errors and the mean-of-the-others baseline.
bool fitProportional(const QVector<CalibrationPoint>& selected, const RegressionSums& s,
                     PortalTailAnalyzer::CalibrationFit* fit)
{
    if (s.sxx <= 1e-12)
        return false;
    fit->proportionalK = s.sxy / s.sxx;
    if (!(fit->proportionalK > 0.0) || !std::isfinite(fit->proportionalK))
        return false;

    const qsizetype n = selected.size();
    if (n < 2)
        return true;
    double loocvAbs = 0.0;
    double loocvSq = 0.0;
    double baselineSq = 0.0;
    qsizetype loocvCount = 0;
    for (const auto& p : selected) {
        const double x = p.meanRawEcPerG;
        const double y = p.measuredTdsPct;
        const double baselineError = (s.sy - y) / (s.n - 1.0) - y;
        baselineSq += baselineError * baselineError;
        const double denom = s.sxx - x * x;
        if (denom > 1e-9) {
            const double loocvErr = (s.sxy - x * y) / denom * x - y;
            loocvAbs += std::abs(loocvErr);
            loocvSq += loocvErr * loocvErr;
            ++loocvCount;
        }
    }

    if (loocvCount > 0) {
        fit->proportionalLoocvMaeTdsPct = loocvAbs / static_cast<double>(loocvCount);
        fit->proportionalLoocvRmseTdsPct = std::sqrt(loocvSq / static_cast<double>(loocvCount));
    }
    fit->hasCrossValidation = loocvCount == n;
    if (fit->hasCrossValidation)
        fit->baselineLoocvRmseTdsPct = std::sqrt(baselineSq / s.n);
    return true;
}

// TDS% = a * EC + b; needs three points spanning 0.5 raw EC.
void fitAffine(const QVector<CalibrationPoint>& selected, const RegressionSums& s,
               PortalTailAnalyzer::CalibrationFit* fit)
{
    const qsizetype n = selected.size();
    const double denom = s.n * s.sxx - s.sx * s.sx;
    if (n < 3 || fit->maxMeanRawEc - fit->minMeanRawEc < 0.5 || std::abs(denom) <= 1e-9)
        return;
    const double a = (s.n * s.sxy - s.sx * s.sy) / denom;
    const double b = (s.sy - a * s.sx) / s.n;
    if (!std::isfinite(a) || !std::isfinite(b) || a <= 0.0)
        return;
    fit->affineValid = true;
    fit->affineSlopeA = a;
    fit->affineInterceptB = b;

    double loocvAbs = 0.0;
    double loocvSq = 0.0;
    qsizetype loocvCount = 0;
    for (const auto& p : selected) {
        const double x = p.meanRawEcPerG;
        const double y = p.measuredTdsPct;
        const double nSub = s.n - 1.0;
        const double sxSub = s.sx - x;
        const double sySub = s.sy - y;
        const double denomSub = nSub * (s.sxx - x * x) - sxSub * sxSub;
        if (std::abs(denomSub) <= 1e-9)
            continue;
        const double aSub = (nSub * (s.sxy - x * y) - sxSub * sySub) / denomSub;
        const double bSub = (sySub - aSub * sxSub) / nSub;
        const double loocvErr = aSub * x + bSub - y;
        if (!std::isfinite(loocvErr) || !std::isfinite(aSub) || aSub <= 0.0)
            continue;
        loocvAbs += std::abs(loocvErr);
        loocvSq += loocvErr * loocvErr;
        ++loocvCount;
    }

    fit->affineHasCrossValidation = loocvCount == n;
    if (fit->affineHasCrossValidation) {
        fit->affineLoocvMaeTdsPct = loocvAbs / s.n;
        fit->affineLoocvRmseTdsPct = std::sqrt(loocvSq / s.n);
    }

    // The intercept must earn its parameter: at least 5% lower held-out RMSE.
    if (n >= 4 && std::abs(b) < kMaxAffineInterceptTdsPct
        && fit->affineHasCrossValidation && fit->hasCrossValidation
        && fit->affineLoocvRmseTdsPct < fit->proportionalLoocvRmseTdsPct * 0.95)
        fit->activeModel = PortalTailAnalyzer::CalibrationModel::Affine;
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

QString PortalTailAnalyzer::Result::calibrationMode() const
{
    if (!calibrated)
        return QStringLiteral("relative_proxy_only");
    return calibrationModel == CalibrationModel::Affine ? QStringLiteral("refractometer_affine")
                                                        : QStringLiteral("refractometer_proportional");
}

PortalTailAnalyzer::PersistedSummary PortalTailAnalyzer::Result::persistedSummary() const
{
    if (!valid())
        return {};
    return {meanRawEcPerG, integralRawEcG, meanTempC, measurementReliable};
}

QVariantMap PortalTailAnalyzer::Result::toVariantMap() const
{
    QVariantMap m;
    m["valid"] = valid();
    m["status"] = PortalTailAnalyzer::statusToString(status);
    m["weightSource"] = PortalTailAnalyzer::weightSourceToString(weightSource);
    m["mountingMode"] = PortalTailAnalyzer::mountingModeToString(mountingMode);
    m["calibrationMode"] = calibrationMode();

    m["portalSampleCount"] = portalSampleCount;
    m["weightSampleCount"] = weightSampleCount;
    m["gapCount"] = gapCount;
    m["totalMassG"] = totalMassG;
    m["coveredMassG"] = coveredMassG;
    m["coverageRatio"] = coverageRatio;
    m["peakRawEc"] = peakRawEc;
    m["peakTimeSec"] = peakTimeSec;
    m["meanTempC"] = meanTempC;
    m["maxTempC"] = maxTempC;
    m["temperatureCompensation"] = QStringLiteral("unverified_device_output");
    m["measurementReliable"] = measurementReliable;
    m["estimateReliable"] = estimateReliable();
    m["uncertaintyReasons"] = uncertaintyReasons + calibrationReasons;
    if (!valid())
        return m;

    m["mainPeakRawEc"] = mainPeakRawEc;
    m["mainPeakMassG"] = mainPeakMassG;
    m["finalRawEc"] = finalRawEc;
    m["massWeightedTempC"] = massWeightedTempC;
    if (massWeightedAuxiliaryTemperatureRaw)
        m["massWeightedAuxiliaryTemperatureRaw"] = *massWeightedAuxiliaryTemperatureRaw;

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
    m["thermalDriftWarning"] = thermalDriftWarning;

    m["massAt85PctIntegralG"] = massAt85PctIntegralG;
    m["massAt90PctIntegralG"] = massAt90PctIntegralG;
    m["post1to2MassG"] = post1to2MassG;
    m["post1to2ContributionShare"] = post1to2ContributionShare;
    m["ratioAdvice"] = PortalTailAnalyzer::ratioAdviceToString(ratioAdvice);
    m["lowTailConcentration"] = lowTailConcentration;
    m["bestHistoricalWeightG"] = bestHistoricalWeightG;
    m["bestHistoricalRatio"] = bestHistoricalRatio;
    m["bestHistoricalEnjoyment"] = bestHistoricalEnjoyment;

    if (selfReferenceK > 0.0)
        m["selfReferenceK"] = selfReferenceK;
    m["calibrated"] = calibrated;
    m["predictionAvailable"] = predictionAvailable;
    if (!calibrated)
        return m;
    m["calibrationHasCrossValidation"] = calibrationHasCrossValidation;
    m["calibrationScope"] = PortalTailAnalyzer::calibrationScopeToString(calibrationScope);
    m["calibrationShotCount"] = calibrationShotCount;
    m["calibrationSlopeK"] = calibrationSlopeK;
    if (calibrationModel == CalibrationModel::Affine) {
        m["calibrationAffineA"] = calibrationAffineA;
        m["calibrationAffineB"] = calibrationAffineB;
    }
    if (calibrationHasCrossValidation)
        m["calibrationLoocvMaeTdsPct"] = calibrationLoocvMaeTdsPct;
    m["hasValidationReading"] = hasValidationReading;
    if (predictionAvailable) {
        m["estimatedTdsPct"] = estimatedTdsPct;
        m["estimatedEyPct"] = estimatedEyPct;
        if (hasValidationReading)
            m["validationResidualTdsPct"] = validationResidualTdsPct;
    }
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

    const QVector<PortalSegment> segments = buildPortalSegments(
        portalSamples, &out.portalSampleCount, &out.gapCount, &out.peakRawEc, &out.peakTimeSec,
        &out.meanTempC, &out.maxTempC);
    if (out.portalSampleCount < 2 || segments.isEmpty()) {
        out.status = Status::NoPortalData;
        return out;
    }

    if (options.weightSource != WeightSource::PhysicalScale) {
        out.status = options.weightSource == WeightSource::Unknown
            ? Status::MissingWeightProvenance : Status::RequiresPhysicalScale;
        return out;
    }

    const auto weight = cleanWeightCurve(cumulativeWeight);
    out.weightSampleCount = weight ? weight->size() : 0;
    if (!weight || weight->size() < 2) {
        out.status = Status::InsufficientWeightData;
        return out;
    }
    out.totalMassG = weight->last().y();
    if (out.totalMassG < kTailWindowG) {
        out.status = Status::InsufficientWeightData;
        return out;
    }

    QVector<MassKnot> knots = buildMassKnots(*weight, segments, options.activeStopTimeSec);
    const MassIntegrals sums = integrateMassSpace(&knots, options.activeStopTimeSec);
    if (sums.coveredMass > 0.0) {
        out.meanRawEcPerG = sums.total / sums.coveredMass;
        out.massWeightedTempC = sums.temperature / sums.coveredMass;
        if (std::abs(sums.auxiliaryCoveredMass - sums.coveredMass) < 1e-6)
            out.massWeightedAuxiliaryTemperatureRaw = sums.auxiliary / sums.coveredMass;
    }
    out.coveredMassG = sums.coveredMass;
    out.coverageRatio = sums.coveredMass / out.totalMassG;
    out.integralRawEcG = sums.total;
    out.activeIntegralRawEcG = sums.active;
    out.settlingIntegralRawEcG = std::max(0.0, sums.total - sums.active);
    if (positiveFinite(options.doseWeightG))
        out.doseNormalizedIntegral = sums.total / options.doseWeightG;
    if (knots.last().ecRaw)
        out.finalRawEc = *knots.last().ecRaw;

    for (const auto& k : knots) {
        if (k.massG >= kActivePeakMinMassG && k.ecRaw && *k.ecRaw > out.mainPeakRawEc) {
            out.mainPeakRawEc = *k.ecRaw;
            out.mainPeakMassG = k.massG;
        }
    }
    if (out.mainPeakRawEc <= 0.0) {
        out.mainPeakRawEc = out.peakRawEc;
        out.mainPeakMassG = 0.0;
    }

    out.instabilityDips = detectInstabilityDips(knots, out.mainPeakMassG);
    out.instabilityDipCount = out.instabilityDips.size();
    for (const auto& dip : out.instabilityDips)
        out.maxDipProminenceRawEc = std::max(out.maxDipProminenceRawEc, dip.prominenceRawEc);

    if (out.coverageRatio < kMinCoverageRatio
        || tailCoveredMassG(knots, out.totalMassG) < kTailWindowG - 1e-6) {
        out.status = Status::InsufficientCoverage;
        return out;
    }

    computeTailFeatures(knots, &out);
    assessMeasurement(options, &out);

    const double iEnd = knots.last().cumulativeIntegral;
    if (iEnd > 0.0) {
        out.massAt85PctIntegralG = interpolateMassAtIntegral(knots, 0.85 * iEnd);
        out.massAt90PctIntegralG = interpolateMassAtIntegral(knots, 0.90 * iEnd);
    }
    const double m1to2 = positiveFinite(options.doseWeightG) ? 2.0 * options.doseWeightG : 0.0;
    if (m1to2 > 0.0 && out.totalMassG > m1to2 + 1.0 && iEnd > 0.0) {
        out.post1to2MassG = out.totalMassG - m1to2;
        const double iAt1to2 = interpolateIntegralAtMass(knots, m1to2);
        out.post1to2ContributionShare = std::clamp((iEnd - iAt1to2) / iEnd, 0.0, 1.0);
    }

    out.lowTailConcentration = out.measurementReliable && out.instabilityDipCount == 0
        && out.tailRatio5g > 0.0 && out.tailRatio5g <= 0.16 && out.tailSlope5gPerG <= 0.0;
    out.status = Status::Valid;
    return out;
}

PortalTailAnalyzer::Result PortalTailAnalyzer::analyzeShot(
    const QVector<PortalSample>& portalSamples, const QVector<QPointF>& cumulativeWeight,
    const QString& weightSource, const QString& mountingMode,
    double doseWeightG, double finalWeightG, const QVector<QPointF>& pressure)
{
    Options options;
    options.weightSource = weightSourceFromString(weightSource);
    options.mountingMode = mountingModeFromString(mountingMode);
    options.doseWeightG = doseWeightG;
    options.finalWeightG = finalWeightG;
    options.activeStopTimeSec = pressure.isEmpty() ? 0.0 : pressure.last().x();
    return analyze(portalSamples, cumulativeWeight, options);
}

PortalTailAnalyzer::Prediction PortalTailAnalyzer::CalibrationFit::predict(
    double meanRawEcPerG, double finalWeightG, double doseWeightG, bool measurementReliable) const
{
    Prediction prediction;
    if (!calibrated)
        return prediction;
    if (shotCount < 5)
        prediction.uncertaintyReasons.append(QStringLiteral("preliminary_calibration"));
    if (!hasCrossValidation)
        prediction.uncertaintyReasons.append(QStringLiteral("unvalidated_calibration"));
    else if (!predictionSupported)
        prediction.uncertaintyReasons.append(QStringLiteral("inconsistent_calibration"));
    const double ratio = doseWeightG > 0.0 ? finalWeightG / doseWeightG : 0.0;
    const bool outsideEc = minMeanRawEc > 0.0
        && (meanRawEcPerG < minMeanRawEc - 1e-6 || meanRawEcPerG > maxMeanRawEc + 1e-6);
    const bool outsideRatio = minRatio > 0.0 && ratio > 0.0
        && (ratio < minRatio - 1e-6 || ratio > maxRatio + 1e-6);
    const bool outsideRange = shotCount >= 2 && (outsideEc || outsideRatio);
    if (outsideRange)
        prediction.uncertaintyReasons.append(QStringLiteral("outside_calibration_range"));

    prediction.available = measurementReliable && predictionSupported && !outsideRange
        && std::isfinite(meanRawEcPerG) && meanRawEcPerG > 0.0;
    if (prediction.available) {
        prediction.tdsPct = std::max(0.0, affineActive() ? affineSlopeA * meanRawEcPerG + affineInterceptB
                                                         : proportionalK * meanRawEcPerG);
        prediction.eyPct = extractionYieldPct(prediction.tdsPct, finalWeightG, doseWeightG);
    }
    return prediction;
}

double PortalTailAnalyzer::extractionYieldPct(double tdsPct, double finalWeightG, double doseWeightG)
{
    if (!std::isfinite(tdsPct) || !std::isfinite(finalWeightG) || !std::isfinite(doseWeightG)
        || tdsPct <= 0.0 || finalWeightG <= 0.0 || doseWeightG <= 0.0) {
        return 0.0;
    }
    return (tdsPct * finalWeightG) / doseWeightG;
}

QVariantMap PortalTailAnalyzer::CalibrationFit::toVariantMap() const
{
    QVariantMap m;
    m["calibrated"] = calibrated;
    m["hasCrossValidation"] = hasCrossValidation;
    m["predictionSupported"] = predictionSupported;
    m["activeModel"] = calibrationModelToString(activeModel);
    m["scope"] = calibrationScopeToString(scope);
    m["shotCount"] = shotCount;
    m["excludedShotCount"] = excludedShotCount;
    m["referenceShots"] = referenceShots;
    m["minRatio"] = minRatio;
    m["maxRatio"] = maxRatio;
    m["proportionalK"] = proportionalK;
    if (hasCrossValidation)
        m["activeLoocvMaeTdsPct"] = activeLoocvMaeTdsPct();
    if (affineActive()) {
        m["affineSlopeA"] = affineSlopeA;
        m["affineInterceptB"] = affineInterceptB;
    }
    return m;
}

QString PortalTailAnalyzer::coffeeNameKey(const QString& name)
{
    return name.trimmed().toCaseFolded();
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
    const CoffeeTarget target(targetBagId, targetBeanBrand, targetBeanType);
    const auto selectFor = [&](CalibrationScope scope) {
        QVector<CalibrationPoint> out;
        for (const auto& p : points) {
            if (p.measurementReliable && targetMountingMode != MountingMode::Unknown
                && p.mountingMode == targetMountingMode
                && std::isfinite(p.meanRawEcPerG) && p.meanRawEcPerG > 0.1
                && std::isfinite(p.measuredTdsPct) && p.measuredTdsPct > 0.1
                && !(excludeShotId > 0 && p.shotId == excludeShotId)
                && target.matches(p, scope))
                out.append(p);
        }
        return out;
    };

    // A bag's own references win; otherwise the same brand + bean across bags.
    QVector<CalibrationPoint> selected;
    if (targetBagId > 0) {
        selected = selectFor(CalibrationScope::Bag);
        if (!selected.isEmpty())
            fit.scope = CalibrationScope::Bag;
    }
    if (selected.isEmpty()) {
        selected = selectFor(CalibrationScope::Bean);
        if (!selected.isEmpty())
            fit.scope = CalibrationScope::Bean;
    }
    if (fit.scope == CalibrationScope::None)
        fit.scope = targetBagId > 0 ? CalibrationScope::Bag : CalibrationScope::Bean;
    appendReferenceRows(&fit, points, selected, target, excludeShotId, targetMountingMode);
    if (selected.isEmpty())
        return fit;

    RegressionSums sums;
    fit.minMeanRawEc = fit.maxMeanRawEc = selected.first().meanRawEcPerG;
    for (const auto& p : selected) {
        const double x = p.meanRawEcPerG;
        const double y = p.measuredTdsPct;
        sums.n += 1.0;
        sums.sx += x;
        sums.sy += y;
        sums.sxx += x * x;
        sums.sxy += x * y;
        fit.minMeanRawEc = std::min(fit.minMeanRawEc, x);
        fit.maxMeanRawEc = std::max(fit.maxMeanRawEc, x);
        const double ratio = brewRatio(p);
        if (!std::isfinite(ratio) || ratio <= 0.0)
            continue;
        fit.minRatio = fit.minRatio > 0.0 ? std::min(fit.minRatio, ratio) : ratio;
        fit.maxRatio = std::max(fit.maxRatio, ratio);
    }
    fit.shotCount = selected.size();

    fit.calibrated = fitProportional(selected, sums, &fit);
    if (!fit.calibrated)
        return fit;
    fit.activeModel = CalibrationModel::Proportional;
    fitAffine(selected, sums, &fit);

    // A model must beat predicting each reference from the others' mean TDS.
    // This is a model-selection policy, not a physical EC-to-TDS validity threshold.
    const double activeRmse = fit.activeLoocvRmseTdsPct();
    fit.predictionSupported = fit.hasCrossValidation && sums.n * sums.sxy - sums.sx * sums.sy > 1e-9
        && std::isfinite(activeRmse) && activeRmse < fit.baselineLoocvRmseTdsPct;
    return fit;
}

void PortalTailAnalyzer::applyCalibration(Result* result,
                                          const CalibrationFit& fit,
                                          double finalWeightG,
                                          double doseWeightG,
                                          double measuredTdsPct)
{
    if (!result) return;
    // Everything below is derived from `fit`, so a second call fully replaces the first.
    result->calibrationReasons.clear();
    result->calibrated = result->predictionAvailable = result->calibrationHasCrossValidation = false;
    result->calibrationModel = CalibrationModel::None;
    result->calibrationScope = CalibrationScope::None;
    result->calibrationShotCount = 0;
    result->calibrationSlopeK = result->calibrationAffineA = result->calibrationAffineB = 0.0;
    result->calibrationLoocvMaeTdsPct = result->estimatedTdsPct = result->estimatedEyPct = 0.0;
    result->selfReferenceK = result->validationResidualTdsPct = 0.0;
    result->hasValidationReading = std::isfinite(measuredTdsPct) && measuredTdsPct > 0.1;
    if (!result->valid()) return;
    result->selfReferenceK = result->hasValidationReading ? individualK(measuredTdsPct, result->meanRawEcPerG) : 0.0;
    if (!fit.calibrated) return;

    const double effectiveWeightG = positiveFinite(finalWeightG) ? finalWeightG : result->totalMassG;
    const Prediction prediction = fit.predict(result->meanRawEcPerG, effectiveWeightG, doseWeightG,
                                              result->measurementReliable);
    result->calibrationReasons = prediction.uncertaintyReasons;
    result->calibrated = true;
    result->calibrationHasCrossValidation = fit.hasCrossValidation;
    result->calibrationModel = fit.activeModel;
    result->calibrationScope = fit.scope;
    result->calibrationShotCount = fit.shotCount;
    result->calibrationSlopeK = fit.proportionalK;
    result->calibrationAffineA = fit.affineSlopeA;
    result->calibrationAffineB = fit.affineInterceptB;
    result->calibrationLoocvMaeTdsPct = fit.activeLoocvMaeTdsPct();
    if (!prediction.available)
        return;
    result->predictionAvailable = true;
    result->estimatedTdsPct = prediction.tdsPct;
    result->estimatedEyPct = prediction.eyPct;
    if (result->hasValidationReading)
        result->validationResidualTdsPct = result->estimatedTdsPct - measuredTdsPct;
}

void PortalTailAnalyzer::evaluateRatioAdvice(Result* result,
                                             const QVector<CalibrationPoint>& historyPoints,
                                             qint64 currentShotId,
                                             qint64 targetBagId,
                                             const QString& targetBeanBrand,
                                             const QString& targetBeanType,
                                             double doseWeightG,
                                             const QString& currentTasteBalance,
                                             const QString& currentTasteBody,
                                             qint64 targetEquipmentId,
                                             const QString& targetProfileName)
{
    if (!result || !result->valid())
        return;

    result->ratioAdvice = RatioAdvice::TasteFirst;

    const CoffeeTarget target(targetBagId, targetBeanBrand, targetBeanType);
    const CalibrationScope scope = targetBagId > 0 ? CalibrationScope::Bag : CalibrationScope::Bean;
    const CalibrationPoint* bestShot = nullptr;
    for (const auto& pt : historyPoints) {
        if (pt.shotId == currentShotId || pt.enjoyment < 75 || pt.enjoyment > 100
            || pt.finalWeightG <= 5.0 || pt.doseWeightG <= 5.0
            || targetEquipmentId <= 0 || pt.equipmentId != targetEquipmentId
            || targetProfileName.isEmpty() || pt.profileName != targetProfileName
            || std::abs(pt.doseWeightG - doseWeightG) > 0.5)
            continue;
        if (target.matches(pt, scope) && (!bestShot || pt.enjoyment > bestShot->enjoyment))
            bestShot = &pt;
    }

    result->bestHistoricalWeightG = bestShot ? bestShot->finalWeightG : 0.0;
    result->bestHistoricalRatio = bestShot ? brewRatio(*bestShot) : 0.0;
    result->bestHistoricalEnjoyment = bestShot ? bestShot->enjoyment : 0;

    if (!result->measurementReliable) {
        result->ratioAdvice = RatioAdvice::Uncertain;
        return;
    }
    // Only explicit feedback on this cup informs its taste interpretation.
    const bool sour = currentTasteBalance.compare(QLatin1String("sour"), Qt::CaseInsensitive) == 0;
    const bool bitterOrThin = currentTasteBalance.compare(QLatin1String("bitter"), Qt::CaseInsensitive) == 0
        || currentTasteBody.compare(QLatin1String("thin"), Qt::CaseInsensitive) == 0;
    if (sour && result->tailRatio5g >= 0.22)
        result->ratioAdvice = RatioAdvice::TryLonger;
    else if (bitterOrThin && result->lowTailConcentration)
        result->ratioAdvice = RatioAdvice::TryShorter;
}

QString PortalTailAnalyzer::statusToString(Status status)
{
    switch (status) {
    case Status::Valid:
        return QStringLiteral("valid");
    case Status::NoPortalData:
        return QStringLiteral("no_portal_data");
    case Status::MissingWeightProvenance:
        return QStringLiteral("missing_weight_provenance");
    case Status::RequiresPhysicalScale:
        return QStringLiteral("requires_physical_scale");
    case Status::InsufficientWeightData:
        return QStringLiteral("insufficient_weight_data");
    case Status::InsufficientCoverage:
        return QStringLiteral("insufficient_coverage");
    }
    return QStringLiteral("unknown");
}

QString PortalTailAnalyzer::calibrationModelToString(CalibrationModel model)
{
    switch (model) {
    case CalibrationModel::Proportional:
        return QStringLiteral("proportional");
    case CalibrationModel::Affine:
        return QStringLiteral("affine");
    case CalibrationModel::None:
        return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

QString PortalTailAnalyzer::calibrationScopeToString(CalibrationScope scope)
{
    switch (scope) {
    case CalibrationScope::Bag:
        return QStringLiteral("bag");
    case CalibrationScope::Bean:
        return QStringLiteral("bean");
    case CalibrationScope::None:
        return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

QString PortalTailAnalyzer::ratioAdviceToString(RatioAdvice advice)
{
    switch (advice) {
    case RatioAdvice::TasteFirst:
        return QStringLiteral("taste_first");
    case RatioAdvice::Uncertain:
        return QStringLiteral("uncertain");
    case RatioAdvice::TryLonger:
        return QStringLiteral("try_longer");
    case RatioAdvice::TryShorter:
        return QStringLiteral("try_shorter");
    }
    return QStringLiteral("taste_first");
}
