// shot_eval — standalone CLI for evaluating Decenza's ShotAnalysis
// heuristics against real shot data. Links the production code directly
// (src/ai/shotanalysis.cpp, src/ai/conductance.cpp) so running this against
// a corpus of shots validates whatever the current algorithm is doing.
//
// Inputs: one or more visualizer.coffee-format JSON files (download via
//   curl https://visualizer.coffee/api/shots/<uuid>/download > shot.json
// and point this tool at the saved file). Glob patterns and directories
// are accepted.
//
// Phase-mode inference: visualizer payloads don't carry the per-frame
// isFlowMode flag Decenza uses internally, so we derive it at each sample
// from the published goal curves. A sample is "flow-mode" when the flow
// goal is active and the pressure goal is not (and vice versa). Runs of
// same-mode samples become HistoryPhaseMarker spans, identical in shape to
// what maincontroller.cpp records for live shots.
//
// Output: a table per shot showing baseline (unrestricted) vs mode-aware
// detector verdicts alongside counts, peaks, and the mask coverage the
// mode-aware windowing produces. Optional --json emits one JSON object
// per shot to stdout for diffing.

#include "ai/conductance.h"
#include "ai/shotanalysis.h"
#include "history/shothistorystorage.h"  // HistoryPhaseMarker

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace {

struct LoadedShot {
    QString path;
    QString id;
    QString profileTitle;
    QString beverageType;
    double durationSec = 0.0;
    int enjoyment = 0;
    double doseG = 0.0;
    double yieldG = 0.0;
    QString grinderSetting;

    QVector<QPointF> pressure;
    QVector<QPointF> flow;
    QVector<QPointF> pressureGoal;
    QVector<QPointF> flowGoal;

    QList<HistoryPhaseMarker> phases;
    QVector<QPointF> conductance;
    QVector<QPointF> conductanceDerivative;

    double pourStart = 0.0;
    double pourEnd = 0.0;
};

double toDouble(const QJsonValue& v)
{
    if (v.isDouble()) return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        double d = v.toString().toDouble(&ok);
        return ok ? d : 0.0;
    }
    return 0.0;
}

// Pair a scalar array with a timeframe (or synthesize a 10 Hz axis if the
// timeframe array is missing — visualizer exports sometimes omit it).
QVector<QPointF> buildSeries(const QJsonArray& values, const QJsonArray& timeframe,
                              double fallbackDt = 0.1)
{
    QVector<QPointF> out;
    out.reserve(values.size());
    const bool useTimeframe = timeframe.size() == values.size();
    for (qsizetype i = 0; i < values.size(); ++i) {
        const double t = useTimeframe ? toDouble(timeframe[i]) : i * fallbackDt;
        const double y = toDouble(values[i]);
        out.append(QPointF(t, y));
    }
    return out;
}

// Treat visualizer "no goal" sentinels (negative or tiny values) as absent.
bool hasActiveGoal(double v)
{
    return v >= ShotAnalysis::WINDOW_MIN_GOAL;
}

// Walk the two goal series and emit one HistoryPhaseMarker each time the
// inferred control mode changes. The marker times are the transition
// boundaries; each marker's isFlowMode describes the span that *starts* at
// that time and runs until the next marker (or shot end).
QList<HistoryPhaseMarker> inferPhasesFromGoals(const QVector<QPointF>& pressure,
                                                const QVector<QPointF>& pressureGoal,
                                                const QVector<QPointF>& flowGoal)
{
    QList<HistoryPhaseMarker> markers;
    const qsizetype n = pressure.size();
    if (n == 0) return markers;

    auto modeAt = [&](qsizetype i) -> int {
        // -1 = unknown / no active goal; 0 = pressure; 1 = flow.
        const double t = pressure[i].x();
        // Match by index when lengths align, else interpolate by time.
        double pg = 0.0, fg = 0.0;
        if (i < pressureGoal.size()) pg = pressureGoal[i].y();
        if (i < flowGoal.size()) fg = flowGoal[i].y();
        // Fall back to time-based lookup if series lengths diverge.
        if (pressureGoal.size() != n) {
            for (const auto& pt : pressureGoal) {
                if (pt.x() >= t) { pg = pt.y(); break; }
            }
        }
        if (flowGoal.size() != n) {
            for (const auto& pt : flowGoal) {
                if (pt.x() >= t) { fg = pt.y(); break; }
            }
        }
        const bool pActive = hasActiveGoal(pg);
        const bool fActive = hasActiveGoal(fg);
        if (fActive && !pActive) return 1;
        if (pActive && !fActive) return 0;
        return -1;
    };

    int currentMode = -1;
    int frameNumber = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const int m = modeAt(i);
        if (m < 0) continue;  // unknown — don't emit, keep previous
        if (m == currentMode) continue;
        // Emit a marker at this transition.
        HistoryPhaseMarker marker;
        marker.time = pressure[i].x();
        marker.label = (m == 1) ? QStringLiteral("FlowPhase") : QStringLiteral("PressurePhase");
        marker.frameNumber = frameNumber++;
        marker.isFlowMode = (m == 1);
        markers.append(marker);
        currentMode = m;
    }

    // Return an empty list when no mode ever resolved. Under the current
    // buildChannelingWindows contract, empty phases triggers the whole-pour
    // legacy fallback, which is what we want for visualizer shots without
    // populated goal curves. A synthesized pressure-mode marker would make
    // buildChannelingWindows probe pressureGoal, get NaN everywhere, and
    // return an empty window list — which the detector now treats as
    // "silence" rather than "unrestricted fallback".
    return markers;
}

// Pour-start proxy for visualizer shots which don't expose Decenza's phase
// markers directly. Scans for the first sample where P > 2.0 bar and
// F > 0.2 ml/s — the same heuristic ShotSummarizer falls back to.
double pourStartFromCurves(const QVector<QPointF>& pressure, const QVector<QPointF>& flow)
{
    const qsizetype n = std::min(pressure.size(), flow.size());
    for (qsizetype i = 0; i < n; ++i) {
        if (pressure[i].y() > 2.0 && flow[i].y() > 0.2) return pressure[i].x();
    }
    return pressure.isEmpty() ? 0.0 : pressure.first().x();
}

// Decenza's ShotHistoryExporter writes JSONs shaped like
//   { "elapsed": [...], "pressure": {"pressure": [...], "goal": [...]},
//     "flow": {"flow": [...], "goal": [...]}, "profile": {"steps": [...]}, ... }
// Very different from visualizer.coffee's flat "data.espresso_pressure" shape.
// Detect the format on the first key seen and dispatch to the matching
// loader so one tool invocation can mix a visualizer dump and an export
// directory.
bool looksLikeDecenzaFormat(const QJsonObject& root)
{
    return root.contains("elapsed") && root.value("pressure").isObject();
}

bool loadVisualizerFormat(const QJsonObject& root, LoadedShot& out, QString* errOut)
{
    out.id = root.value("id").toString();
    out.profileTitle = root.value("profile_title").toString();
    out.beverageType = root.value("beverage_type").toString();
    out.durationSec = toDouble(root.value("duration"));
    out.enjoyment = root.value("espresso_enjoyment").toInt();
    out.doseG = toDouble(root.value("bean_weight"));
    out.yieldG = toDouble(root.value("drink_weight"));
    out.grinderSetting = root.value("grinder_setting").toString();

    const QJsonObject data = root.value("data").toObject();
    const QJsonArray timeframe = data.value("timeframe").toArray();
    out.pressure = buildSeries(data.value("espresso_pressure").toArray(), timeframe);
    out.flow = buildSeries(data.value("espresso_flow").toArray(), timeframe);
    out.pressureGoal = buildSeries(data.value("espresso_pressure_goal").toArray(), timeframe);
    out.flowGoal = buildSeries(data.value("espresso_flow_goal").toArray(), timeframe);

    if (out.pressure.size() < 5) {
        if (errOut) *errOut = QStringLiteral("not enough samples");
        return false;
    }
    out.phases = inferPhasesFromGoals(out.pressure, out.pressureGoal, out.flowGoal);
    return true;
}

bool loadDecenzaFormat(const QJsonObject& root, LoadedShot& out, QString* errOut)
{
    const QJsonArray elapsed = root.value("elapsed").toArray();
    const QJsonObject pressureObj = root.value("pressure").toObject();
    const QJsonObject flowObj = root.value("flow").toObject();
    const QJsonObject profile = root.value("profile").toObject();
    const QJsonObject meta = root.value("meta").toObject();
    const QJsonObject totals = root.value("totals").toObject();

    out.profileTitle = profile.value("title").toString();
    out.beverageType = profile.value("beverage_type").toString();
    if (!elapsed.isEmpty()) out.durationSec = toDouble(elapsed.last());

    // Shot metadata lives under meta.shot / meta.bean / meta.grinder
    const QJsonObject metaShot = meta.value("shot").toObject();
    const QJsonObject metaBean = meta.value("bean").toObject();
    const QJsonObject metaGrinder = meta.value("grinder").toObject();
    out.id = metaShot.value("uuid").toString();
    if (out.id.isEmpty()) out.id = metaShot.value("id").toString();
    out.enjoyment = metaShot.value("enjoyment").toInt();
    out.doseG = toDouble(meta.value("in"));
    out.yieldG = toDouble(meta.value("out"));
    if (out.yieldG <= 0.0) out.yieldG = toDouble(totals.value("weight").toArray().last());
    out.grinderSetting = metaGrinder.value("setting").toString();
    if (out.grinderSetting.isEmpty()) out.grinderSetting = metaBean.value("grinder").toString();

    out.pressure = buildSeries(pressureObj.value("pressure").toArray(), elapsed);
    out.flow = buildSeries(flowObj.value("flow").toArray(), elapsed);
    out.pressureGoal = buildSeries(pressureObj.value("goal").toArray(), elapsed);
    out.flowGoal = buildSeries(flowObj.value("goal").toArray(), elapsed);

    if (out.pressure.size() < 5) {
        if (errOut) *errOut = QStringLiteral("not enough samples");
        return false;
    }

    // Decenza's profile.steps carries the canonical control mode ("pump":
    // "pressure" or "flow"). Walk steps into phase markers by mapping each
    // step to its time boundary — we locate boundaries by scanning goal
    // curves for discontinuities, then assign the pre-computed isFlowMode
    // per step. When the step count doesn't match boundaries we fall back
    // to goal-curve inference (same path as visualizer format).
    const QJsonArray steps = profile.value("steps").toArray();
    if (!steps.isEmpty()) {
        QList<HistoryPhaseMarker> markers;
        // Heuristic boundary detection: step into a new frame when the
        // PRIMARY goal for the active mode jumps by >0.5 units, OR when the
        // mode itself flips (pressure→flow or flow→pressure).
        auto prevMode = [&](double pg, double fg) -> int {
            const bool pActive = pg >= ShotAnalysis::WINDOW_MIN_GOAL;
            const bool fActive = fg >= ShotAnalysis::WINDOW_MIN_GOAL;
            if (fActive && !pActive) return 1;
            if (pActive && !fActive) return 0;
            return -1;
        };
        int stepIdx = 0;
        int lastMode = -1;
        double lastSetpoint = 0.0;
        const double setpointJump = 0.5;  // bar or ml/s — coarse boundary sniff
        for (qsizetype i = 0; i < out.pressure.size() && stepIdx < steps.size(); ++i) {
            const double t = out.pressure[i].x();
            const double pg = i < out.pressureGoal.size() ? out.pressureGoal[i].y() : 0.0;
            const double fg = i < out.flowGoal.size() ? out.flowGoal[i].y() : 0.0;
            const int mode = prevMode(pg, fg);
            if (mode < 0) continue;
            const double setpoint = (mode == 1) ? fg : pg;
            const bool modeChanged = (lastMode >= 0 && mode != lastMode);
            const bool setpointShift = (lastMode >= 0
                && std::abs(setpoint - lastSetpoint) > setpointJump);
            if (lastMode < 0 || modeChanged || setpointShift) {
                // Advance to the next step on a transition — unless this is
                // the very first marker (lastMode < 0).
                if (lastMode >= 0) stepIdx = std::min<int>(stepIdx + 1, steps.size() - 1);
                const QJsonObject step = steps[stepIdx].toObject();
                HistoryPhaseMarker m;
                m.time = t;
                m.label = step.value("name").toString();
                m.frameNumber = stepIdx;
                m.isFlowMode = step.value("pump").toString().toLower() == QStringLiteral("flow");
                markers.append(m);
            }
            lastMode = mode;
            lastSetpoint = setpoint;
        }
        if (!markers.isEmpty()) out.phases = markers;
    }
    if (out.phases.isEmpty()) {
        // Fall back to pure goal-curve inference.
        out.phases = inferPhasesFromGoals(out.pressure, out.pressureGoal, out.flowGoal);
    }
    return true;
}

bool loadShotFile(const QString& path, LoadedShot& out, QString* errOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errOut) *errOut = QStringLiteral("cannot open: %1").arg(f.errorString());
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject()) {
        if (errOut) *errOut = QStringLiteral("invalid JSON: %1").arg(pe.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    out.path = path;

    const bool ok = looksLikeDecenzaFormat(root)
        ? loadDecenzaFormat(root, out, errOut)
        : loadVisualizerFormat(root, out, errOut);
    if (!ok) return false;
    out.conductance = Conductance::fromPressureFlow(out.pressure, out.flow);
    out.conductanceDerivative = Conductance::derivative(out.conductance);

    out.pourStart = pourStartFromCurves(out.pressure, out.flow);
    out.pourEnd = out.pressure.last().x();
    return true;
}

const char* severityName(ShotAnalysis::ChannelingSeverity s)
{
    switch (s) {
        case ShotAnalysis::ChannelingSeverity::Sustained: return "Sustained";
        case ShotAnalysis::ChannelingSeverity::Transient: return "Transient";
        case ShotAnalysis::ChannelingSeverity::None:      return "None";
    }
    return "?";
}

struct EvaluatedShot {
    LoadedShot shot;
    ShotAnalysis::ChannelingSeverity baseline = ShotAnalysis::ChannelingSeverity::None;
    ShotAnalysis::ChannelingSeverity modeAware = ShotAnalysis::ChannelingSeverity::None;
    int baselineElevated = 0;
    double baselineMaxSpike = 0.0;
    double baselineSpikeTime = 0.0;
    int maskElevated = 0;
    double maskMaxSpike = 0.0;
    double maskSpikeTime = 0.0;
    double maskSeconds = 0.0;
    double maskPct = 0.0;
    bool grindIssue = false;
    double grindDelta = 0.0;
    int grindSamples = 0;
    bool grindHasData = false;
    bool grindSkipped = false;
    bool skippedInProd = false;  // production-path short-circuit (cleaning/filter/etc)
    bool pourTruncated = false;  // peak pressure never reached PRESSURE_FLOOR_BAR
    QString verdict;
};

// Count elevated samples + find max spike in a dC/dt series across a time
// window (matches the core loop of detectChannelingFromDerivative but
// exposes the intermediate metrics for the report).
void tallyDerivative(const QVector<QPointF>& dcdt, double t0, double t1,
                     const QVector<ShotAnalysis::DetectionWindow>& windows,
                     int* outCount, double* outMaxSpike, double* outMaxSpikeTime)
{
    int count = 0;
    double maxSpike = 0.0;
    double maxSpikeTime = 0.0;
    auto inWindow = [&](double t) {
        if (windows.isEmpty()) return true;
        for (const auto& w : windows) if (t >= w.start && t <= w.end) return true;
        return false;
    };
    for (const auto& pt : dcdt) {
        if (pt.x() < t0) continue;
        if (pt.x() > t1) break;
        if (!inWindow(pt.x())) continue;
        const double v = std::abs(pt.y());
        if (v > maxSpike) { maxSpike = v; maxSpikeTime = pt.x(); }
        if (v > ShotAnalysis::CHANNELING_DC_ELEVATED) ++count;
    }
    if (outCount) *outCount = count;
    if (outMaxSpike) *outMaxSpike = maxSpike;
    if (outMaxSpikeTime) *outMaxSpikeTime = maxSpikeTime;
}

EvaluatedShot evaluate(const LoadedShot& s)
{
    EvaluatedShot ev;
    ev.shot = s;
    const double analysisStart = s.pourStart + ShotAnalysis::CHANNELING_DC_POUR_SKIP_SEC;
    // Keep the reporting tally aligned with what detectChannelingFromDerivative
    // actually analyzes — trim both ends so baseline/mode-aware counts agree
    // with the Sustained/Transient/None verdicts.
    const double analysisEnd = s.pourEnd - ShotAnalysis::CHANNELING_DC_POUR_SKIP_END_SEC;

    // Faithful to the production callsite: cleaning / tea / steam /
    // filter / pourover and turbo-style shots never reach the channeling
    // detector in live Decenza (ShotHistoryStorage::storeRecord() gates
    // on shouldSkipChannelingCheck). Mirror that here so the evaluation
    // report reflects real app behavior — not the raw detector's answer
    // on shots it would never have seen.
    const bool skippedInProd = ShotAnalysis::shouldSkipChannelingCheck(
        s.beverageType, s.flow, s.pourStart, s.pourEnd);
    ev.skippedInProd = skippedInProd;

    if (skippedInProd) {
        ev.baseline = ShotAnalysis::ChannelingSeverity::None;
        ev.modeAware = ShotAnalysis::ChannelingSeverity::None;
    } else {
        // Baseline detector — unrestricted analysis. Pass a single
        // whole-pour window to force the legacy "no masking" path (empty
        // windows now report None under the new production contract).
        const QVector<ShotAnalysis::DetectionWindow> wholePour{
            {s.pourStart, s.pourEnd},
        };
        ev.baseline = ShotAnalysis::detectChannelingFromDerivative(
            s.conductanceDerivative, s.pourStart, s.pourEnd, wholePour, &ev.baselineSpikeTime);
        tallyDerivative(s.conductanceDerivative, analysisStart, analysisEnd, wholePour,
                        &ev.baselineElevated, &ev.baselineMaxSpike, &ev.baselineSpikeTime);

        // Mode-aware detector — mask from phase-inferred windows.
        const auto windows = ShotAnalysis::buildChannelingWindows(
            s.pressure, s.flow, s.pressureGoal, s.flowGoal, s.phases, s.pourStart, s.pourEnd);
        ev.modeAware = ShotAnalysis::detectChannelingFromDerivative(
            s.conductanceDerivative, s.pourStart, s.pourEnd, windows, &ev.maskSpikeTime);
        tallyDerivative(s.conductanceDerivative, analysisStart, analysisEnd, windows,
                        &ev.maskElevated, &ev.maskMaxSpike, &ev.maskSpikeTime);

        double maskSec = 0.0;
        for (const auto& w : windows) maskSec += (w.end - w.start);
        ev.maskSeconds = maskSec;
        const double pourSpan = std::max(0.001, s.pourEnd - s.pourStart);
        ev.maskPct = 100.0 * maskSec / pourSpan;
    }

    // Grind direction — mode-aware.
    const auto grind = ShotAnalysis::analyzeFlowVsGoal(
        s.flow, s.flowGoal, s.phases, s.pourStart, s.pourEnd, s.beverageType);
    ev.grindDelta = grind.delta;
    ev.grindSamples = grind.sampleCount;
    ev.grindHasData = grind.hasData;
    ev.grindSkipped = grind.skipped;
    ev.grindIssue = grind.hasData
        && std::abs(grind.delta) > ShotAnalysis::FLOW_DEVIATION_THRESHOLD;

    // Pour-truncated: catches shots the dC/dt + grind detectors miss
    // because the puck never built pressure at all.
    ev.pourTruncated = ShotAnalysis::detectPourTruncated(
        s.pressure, s.pourStart, s.pourEnd, s.beverageType);

    // Short verdict for the table: report any direction the two detectors
    // disagree on — that's what the user cares about when evaluating a
    // proposed algorithm change.
    if (ev.baseline != ev.modeAware) {
        ev.verdict = QStringLiteral("%1 -> %2")
                         .arg(severityName(ev.baseline), severityName(ev.modeAware));
    } else {
        ev.verdict = severityName(ev.modeAware);
    }
    return ev;
}

void expandInputPaths(const QStringList& inputs, QStringList& files)
{
    for (const auto& p : inputs) {
        const QFileInfo fi(p);
        if (fi.isDir()) {
            QDir dir(p);
            const auto entries = dir.entryInfoList({"*.json"}, QDir::Files, QDir::Name);
            for (const auto& e : entries) files.append(e.absoluteFilePath());
        } else if (fi.exists()) {
            files.append(fi.absoluteFilePath());
        } else if (p.contains('*') || p.contains('?')) {
            // Glob through the parent directory.
            QDir dir(fi.absolutePath());
            const auto entries = dir.entryInfoList({fi.fileName()}, QDir::Files, QDir::Name);
            for (const auto& e : entries) files.append(e.absoluteFilePath());
        } else {
            QTextStream(stderr) << "warning: skipping missing path: " << p << '\n';
        }
    }
}

void printTable(const QList<EvaluatedShot>& shots, QTextStream& out)
{
    // Column widths sized for typical profile titles; truncated if needed.
    out << QStringLiteral("%1  %2  %3  %4  %5  %6  %7  %8  %9  %10\n")
               .arg("id", -12)
               .arg("profile", -28)
               .arg("dur", 6)
               .arg("yield", 6)
               .arg("baseline", -20)
               .arg("mode-aware", -20)
               .arg("mask%", 6)
               .arg("maxΔ", 6)
               .arg("grind", -10)
               .arg("puck");
    out << QString(132, '-') << '\n';
    for (const auto& ev : shots) {
        QString id = ev.shot.id.left(12);
        QString title = ev.shot.profileTitle.left(28);
        QString baseline = QStringLiteral("%1 (%2/%3)")
                               .arg(severityName(ev.baseline))
                               .arg(ev.baselineElevated)
                               .arg(ev.baselineMaxSpike, 0, 'f', 2);
        QString mode = QStringLiteral("%1 (%2/%3)")
                           .arg(severityName(ev.modeAware))
                           .arg(ev.maskElevated)
                           .arg(ev.maskMaxSpike, 0, 'f', 2);
        QString grind;
        if (ev.grindSkipped) grind = "skip";
        else if (!ev.grindHasData) grind = "n/a";
        else grind = QStringLiteral("%1 (%2%3)")
                         .arg(ev.grindIssue ? "ISSUE" : "ok",
                              ev.grindDelta >= 0 ? "+" : "")
                         .arg(ev.grindDelta, 0, 'f', 2);
        QString puck = ev.pourTruncated ? "TRUNCATED" : "ok";
        out << QStringLiteral("%1  %2  %3  %4  %5  %6  %7  %8  %9  %10\n")
                   .arg(id, -12)
                   .arg(title, -28)
                   .arg(ev.shot.durationSec, 6, 'f', 1)
                   .arg(ev.shot.yieldG, 6, 'f', 1)
                   .arg(baseline, -20)
                   .arg(mode, -20)
                   .arg(ev.maskPct, 6, 'f', 1)
                   .arg(ev.maskMaxSpike, 6, 'f', 2)
                   .arg(grind, -10)
                   .arg(puck);
    }
}

QJsonObject toJsonRow(const EvaluatedShot& ev)
{
    QJsonObject o;
    o["id"] = ev.shot.id;
    o["path"] = ev.shot.path;
    o["profileTitle"] = ev.shot.profileTitle;
    o["durationSec"] = ev.shot.durationSec;
    o["yieldG"] = ev.shot.yieldG;
    o["doseG"] = ev.shot.doseG;
    o["grinderSetting"] = ev.shot.grinderSetting;
    o["enjoyment"] = ev.shot.enjoyment;
    o["pourStart"] = ev.shot.pourStart;
    o["pourEnd"] = ev.shot.pourEnd;
    o["phaseCount"] = static_cast<int>(ev.shot.phases.size());

    QJsonObject baseline;
    baseline["verdict"] = severityName(ev.baseline);
    baseline["elevatedCount"] = ev.baselineElevated;
    baseline["maxSpike"] = ev.baselineMaxSpike;
    baseline["maxSpikeTime"] = ev.baselineSpikeTime;
    o["baseline"] = baseline;

    QJsonObject modeAware;
    modeAware["verdict"] = severityName(ev.modeAware);
    modeAware["elevatedCount"] = ev.maskElevated;
    modeAware["maxSpike"] = ev.maskMaxSpike;
    modeAware["maxSpikeTime"] = ev.maskSpikeTime;
    modeAware["maskSeconds"] = ev.maskSeconds;
    modeAware["maskPct"] = ev.maskPct;
    o["modeAware"] = modeAware;
    o["skippedInProd"] = ev.skippedInProd;
    o["beverageType"] = ev.shot.beverageType;
    o["pourTruncated"] = ev.pourTruncated;

    QJsonObject grind;
    grind["skipped"] = ev.grindSkipped;
    grind["hasData"] = ev.grindHasData;
    grind["delta"] = ev.grindDelta;
    grind["sampleCount"] = ev.grindSamples;
    grind["issue"] = ev.grindIssue;
    o["grind"] = grind;

    return o;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("shot_eval");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Evaluate ShotAnalysis heuristics against visualizer.coffee shot JSONs.\n"
        "Links production src/ai/shotanalysis.cpp + src/ai/conductance.cpp so\n"
        "results match whatever the live algorithm does.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("paths",
        "JSON files, directories, or globs. Download a shot with:\n"
        "  curl https://visualizer.coffee/api/shots/<uuid>/download > shot.json",
        "<paths...>");
    QCommandLineOption jsonOpt({"j", "json"},
        "Emit one JSON object per shot to stdout (machine-readable).");
    parser.addOption(jsonOpt);
    QCommandLineOption validateOpt("validate",
        "Validate shots against a manifest.json with expected verdicts.\n"
        "Exits non-zero on mismatch. Positional args ignored; manifest is\n"
        "the sole input. See tests/data/shots/manifest.json for format.",
        "manifest");
    parser.addOption(validateOpt);
    parser.process(app);

    const QStringList inputs = parser.positionalArguments();
    const bool validating = parser.isSet(validateOpt);
    if (inputs.isEmpty() && !validating) {
        parser.showHelp(1);
    }

    QTextStream out(stdout);

    if (validating) {
        const QString manifestPath = parser.value(validateOpt);
        QFile mf(manifestPath);
        if (!mf.open(QIODevice::ReadOnly)) {
            QTextStream(stderr) << "cannot open manifest: " << manifestPath << '\n';
            return 1;
        }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &pe);
        if (doc.isNull() || !doc.isObject()) {
            QTextStream(stderr) << "invalid manifest JSON: " << pe.errorString() << '\n';
            return 1;
        }
        const QJsonArray shotArr = doc.object().value("shots").toArray();
        const QFileInfo mfInfo(manifestPath);
        const QString baseDir = mfInfo.absolutePath();

        int passed = 0, failed = 0;
        for (const auto& v : shotArr) {
            const QJsonObject entry = v.toObject();
            const QString relFile = entry.value("file").toString();
            const QJsonObject expect = entry.value("expect").toObject();
            const QString path = baseDir + "/" + relFile;

            LoadedShot shot;
            QString err;
            if (!loadShotFile(path, shot, &err)) {
                out << "FAIL  " << relFile << "  (load error: " << err << ")\n";
                ++failed;
                continue;
            }
            const EvaluatedShot ev = evaluate(shot);

            // Compare expected vs actual. Missing expect-fields are not
            // checked — authors opt into each invariant explicitly.
            QStringList mismatches;
            if (expect.contains("channeling")) {
                const QString want = expect.value("channeling").toString();
                const QString got = severityName(ev.modeAware);
                if (want != got)
                    mismatches << QStringLiteral("channeling: want=%1 got=%2").arg(want, got);
            }
            if (expect.contains("grindIssue")) {
                const bool want = expect.value("grindIssue").toBool();
                if (want != ev.grindIssue)
                    mismatches << QStringLiteral("grindIssue: want=%1 got=%2")
                                      .arg(want ? "true" : "false",
                                           ev.grindIssue ? "true" : "false");
            }
            if (expect.contains("pourTruncated")) {
                const bool want = expect.value("pourTruncated").toBool();
                if (want != ev.pourTruncated)
                    mismatches << QStringLiteral("pourTruncated: want=%1 got=%2")
                                      .arg(want ? "true" : "false",
                                           ev.pourTruncated ? "true" : "false");
            }

            if (mismatches.isEmpty()) {
                out << "PASS  " << relFile << "\n";
                ++passed;
            } else {
                out << "FAIL  " << relFile << "\n";
                for (const auto& m : mismatches)
                    out << "      " << m << "\n";
                ++failed;
            }
        }
        out << QStringLiteral("\n%1 / %2 shots passed.\n")
                   .arg(passed).arg(passed + failed);
        return failed == 0 ? 0 : 1;
    }

    QStringList files;
    expandInputPaths(inputs, files);
    if (files.isEmpty()) {
        QTextStream(stderr) << "no input files found\n";
        return 1;
    }

    QList<EvaluatedShot> results;
    for (const auto& f : files) {
        LoadedShot shot;
        QString err;
        if (!loadShotFile(f, shot, &err)) {
            QTextStream(stderr) << "skip " << f << ": " << err << '\n';
            continue;
        }
        results.append(evaluate(shot));
    }

    if (parser.isSet(jsonOpt)) {
        QJsonArray arr;
        for (const auto& ev : results) arr.append(toJsonRow(ev));
        out << QJsonDocument(arr).toJson(QJsonDocument::Indented);
    } else {
        printTable(results, out);
        // Summary line: how many verdicts changed.
        int downgraded = 0, upgraded = 0, same = 0;
        for (const auto& ev : results) {
            const int b = static_cast<int>(ev.baseline);
            const int m = static_cast<int>(ev.modeAware);
            if (m < b) ++downgraded;
            else if (m > b) ++upgraded;
            else ++same;
        }
        out << '\n'
            << QStringLiteral("Summary: %1 shots — %2 relaxed, %3 tightened, %4 unchanged.\n")
                   .arg(results.size()).arg(downgraded).arg(upgraded).arg(same);
    }
    return 0;
}
