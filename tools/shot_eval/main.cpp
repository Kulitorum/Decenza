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
#include "ai/shotsummarizer.h"                  // KB-cluster: flags + expert band
#include "history/shothistorystorage.h"  // HistoryPhaseMarker
#include "history/shotbadgeprojection.h"         // deriveBadgesFromAnalysis

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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
    double targetWeightG = 0.0;  // SAW target; not in standard visualizer format, hand-added per fixture
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

// Visualizer's public download drops `app.data.settings.advanced_shot` so we
// can't see the per-frame `pump` field. The same shot's profile is reachable
// at /api/shots/<uuid>/profile in raw Tcl; downloaders can save it next to
// the shot JSON as `<basename>.profile.tcl` (or `<basename>.profile`). This
// helper extracts the ordered pump modes from the advanced_shot block — the
// only field we need for proper isFlowMode attribution.
//
// Returns an empty list when no sidecar is present or the regex matches
// nothing — callers fall back to the goal-curve inference path.
QStringList loadSidecarPumpModes(const QString& shotJsonPath)
{
    const QFileInfo fi(shotJsonPath);
    const QString base = fi.absolutePath() + "/" + fi.completeBaseName();
    QString profilePath = base + ".profile.tcl";
    if (!QFileInfo::exists(profilePath)) {
        profilePath = base + ".profile";
        if (!QFileInfo::exists(profilePath)) return {};
    }
    QFile f(profilePath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QString content = QString::fromUtf8(f.readAll());

    // The advanced_shot block lists steps in order; each step has exactly one
    // `pump <pressure|flow>` field. No other Tcl key uses the literal `pump `
    // prefix followed by `pressure` or `flow` as a bare word (the rest are
    // `pressure_end`, `flow_profile_*`, etc.), so a flat ordered match
    // suffices without nested-brace parsing.
    static const QRegularExpression re(QStringLiteral(R"(\bpump\s+(pressure|flow)\b)"));
    QStringList modes;
    auto it = re.globalMatch(content);
    while (it.hasNext()) modes.append(it.next().captured(1));
    return modes;
}

// Build phase markers from Visualizer's espresso_state_change array combined
// with the ordered pump modes from the sidecar profile. The state_change
// array carries one entry per pressure sample, alternating sign at each
// frame transition (de1app convention). Frame N's mode comes from
// pumpModes[N]; frames past the end of pumpModes are treated as unknown
// mode (we emit them as pressure-mode markers since that's the safer
// default for the grind detector — Arm 1 ignores pressure-mode samples).
//
// Returns empty if either input is missing or the state_change array's
// length disagrees with the pressure series — callers fall back to the
// goal-curve inference path.
QList<HistoryPhaseMarker> phasesFromStateChange(
    const QVector<QPointF>& pressure,
    const QJsonArray& stateChange,
    const QStringList& pumpModes)
{
    QList<HistoryPhaseMarker> markers;
    if (pumpModes.isEmpty() || stateChange.size() != pressure.size()) return markers;

    int frame = 0;
    auto emitMarker = [&](double t) {
        HistoryPhaseMarker m;
        m.time = t;
        m.frameNumber = frame;
        m.isFlowMode = (frame < pumpModes.size()
            && pumpModes[frame].compare(QStringLiteral("flow"), Qt::CaseInsensitive) == 0);
        m.label = (frame < pumpModes.size())
            ? QStringLiteral("frame%1_%2").arg(frame).arg(pumpModes[frame])
            : QStringLiteral("frame%1").arg(frame);
        markers.append(m);
    };

    // Frame 0 starts at the first sample.
    emitMarker(pressure[0].x());
    bool prevPositive = toDouble(stateChange[0]) > 0;
    for (qsizetype i = 1; i < stateChange.size(); ++i) {
        const bool curPositive = toDouble(stateChange[i]) > 0;
        if (curPositive != prevPositive) {
            ++frame;
            emitMarker(pressure[i].x());
        }
        prevPositive = curPositive;
    }
    return markers;
}

// Native-export variant of phasesFromStateChange. Decenza's `state_change`
// encodes the machine SUBSTATE boundary (preinfusion vs pour), not one
// flip per profile frame: consecutive same-substate frames (e.g.
// Filling+Infusing) collapse into a single segment, so the number of
// sign-flip segments is typically <= the profile step count. The
// visualizer head-aligns segment k -> pumpModes[k]; doing that here would
// leave the final flow-controlled pour step unreached whenever
// preinfusion frames collapsed (the segment count fell short), silently
// marking the whole pour pressure-mode and killing the flow-vs-goal grind
// arm (regressed malabar_grind_too_fine). Tail-align instead: the LAST
// segment is always the pour and maps to the LAST profile step's pump
// mode, the previous segment to the previous step, etc. The grind
// detector only needs the pour segment's mode correct; tail-alignment
// guarantees exactly that and is identity when segments == steps.
// Returns empty (→ caller falls back) when the inputs can't yield a real
// segmentation: empty pumpModes, a length-mismatched state_change, or a
// degenerate no-flip state_change on a multi-step profile (a bailed-out
// shot with no recorded substate transition — see the guard below).
QList<HistoryPhaseMarker> phasesFromNativeStateChange(
    const QVector<QPointF>& pressure,
    const QJsonArray& stateChange,
    const QStringList& pumpModes)
{
    QList<HistoryPhaseMarker> markers;
    if (pumpModes.isEmpty() || stateChange.size() != pressure.size()) return markers;

    QList<qsizetype> boundaries;       // sample index where each segment starts
    boundaries.append(0);
    bool prevPositive = toDouble(stateChange[0]) > 0;
    for (qsizetype i = 1; i < stateChange.size(); ++i) {
        const bool curPositive = toDouble(stateChange[i]) > 0;
        if (curPositive != prevPositive) boundaries.append(i);
        prevPositive = curPositive;
    }
    const int segCount = static_cast<int>(boundaries.size());
    const int stepCount = static_cast<int>(pumpModes.size());

    // A state_change with no sign flips (one segment) but a multi-step
    // profile is degenerate: the machine never recorded a substate
    // transition (e.g. a bailed-out short shot), so there is no real
    // preinfusion/pour boundary to recover. Tail-aligning would emit ONE
    // marker for the whole shot — non-empty, so it would BLOCK the
    // fallback chain while being structurally wrong (the multi-frame
    // profile flattened to a single tail-step frame at t=0). Return empty
    // instead so the goal-jump / inferPhasesFromGoals fallbacks engage —
    // the same recover path an absent state_change takes. A genuinely
    // single-step profile (stepCount == 1) is not degenerate and is left
    // to the normal path below.
    if (segCount == 1 && stepCount > 1) return markers;

    const int offset = stepCount - segCount;  // >=0 when frames collapsed
    for (int seg = 0; seg < segCount; ++seg) {
        const int stepIdx = std::clamp(seg + offset, 0, stepCount - 1);
        HistoryPhaseMarker m;
        m.time = pressure[boundaries[seg]].x();
        m.frameNumber = stepIdx;
        m.isFlowMode =
            pumpModes[stepIdx].compare(QStringLiteral("flow"), Qt::CaseInsensitive) == 0;
        m.label = QStringLiteral("frame%1_%2").arg(stepIdx).arg(pumpModes[stepIdx]);
        markers.append(m);
    }
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

bool loadVisualizerFormat(const QJsonObject& root, const QString& path,
                           LoadedShot& out, QString* errOut)
{
    out.id = root.value("id").toString();
    out.profileTitle = root.value("profile_title").toString();
    out.beverageType = root.value("beverage_type").toString();
    out.durationSec = toDouble(root.value("duration"));
    out.enjoyment = root.value("espresso_enjoyment").toInt();
    out.doseG = toDouble(root.value("bean_weight"));
    out.yieldG = toDouble(root.value("drink_weight"));
    out.targetWeightG = toDouble(root.value("target_weight"));
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
    // Prefer state_change + sidecar profile when both are available — this
    // matches the per-sample isFlowMode the production code captures from
    // BLE, instead of inferring mode from goal curves (which silently drops
    // pressure-mode preinfusion samples whose flow goal is a safety
    // limiter, see PRs #811/#864). Fall back to inferPhasesFromGoals when
    // either source is absent.
    const QJsonArray stateChange = data.value("espresso_state_change").toArray();
    const QStringList pumpModes = loadSidecarPumpModes(path);
    if (!stateChange.isEmpty() && !pumpModes.isEmpty()) {
        out.phases = phasesFromStateChange(out.pressure, stateChange, pumpModes);
    }
    if (out.phases.isEmpty()) {
        out.phases = inferPhasesFromGoals(out.pressure, out.pressureGoal, out.flowGoal);
    }
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

    const QJsonArray steps = profile.value("steps").toArray();

    // Authoritative path: Decenza native exports carry a per-sample
    // `state_change` array (de1app convention: one entry per pressure
    // sample, sign flips at each frame transition) — the SAME data the app
    // persists and the visualizer loader already prefers via
    // phasesFromStateChange. Pair it with the embedded profile.steps[].pump
    // modes for the real per-frame isFlowMode. This makes the native
    // loader's pour window frame-faithful to the app instead of inferring
    // boundaries from goal-curve jumps. The goal-jump heuristic mis-places
    // frame boundaries on D-Flow/Q-style profiles whose pressure goal ramps
    // to 0 during a flow-controlled pour; empirically that drove the
    // production pour-window peak to 0.0 bar and spuriously fired the
    // expert band (observed on shot 782: real peak ~7.7 bar, in-band,
    // reported 0.0). Falls back to the goal heuristic only when
    // state_change is absent, length-mismatched, or degenerate (no flips).
    const QJsonArray stateChange = root.value("state_change").toArray();
    QStringList pumpModes;
    pumpModes.reserve(steps.size());
    for (const auto& s : steps)
        pumpModes << s.toObject().value("pump").toString();
    if (!stateChange.isEmpty() && !pumpModes.isEmpty()) {
        out.phases = phasesFromNativeStateChange(out.pressure, stateChange, pumpModes);
        // Provenance: this tool is a feature-gate instrument; a silent
        // drop from the authoritative path to the goal heuristic is
        // exactly the degradation that previously hid 22 spurious fires.
        // Surface it on stderr, matching the loader's "skip <file>: <err>"
        // convention, so an auditor can see which shots did NOT use the
        // app-faithful path.
        if (out.phases.isEmpty())
            QTextStream(stderr) << "note " << out.path
                << ": native state_change present but unusable "
                   "(degenerate/length-mismatch) — using goal-jump fallback\n";
    }

    // Fallback: profile.steps carries the canonical control mode but no
    // explicit boundaries, so locate them by scanning goal curves for
    // discontinuities and assign isFlowMode per step. Used only when the
    // authoritative state_change path above produced nothing.
    if (out.phases.isEmpty() && !steps.isEmpty()) {
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
        : loadVisualizerFormat(root, path, out, errOut);
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
    QString verdict;             // channeling-severity label for the table view (NOT the user-facing verdict)
    QString summaryVerdict;      // user-facing verdict text from the production analyzeShot path
    QString verdictCategory;     // DetectorResults::verdictCategory (drives the app's tint)
    bool expertBandFired = false;
    QString expertBandText;      // the expert_band_deviation observation line, if any
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

    // --- App parity (scoped — read this) ---
    // Route through the ONE production analyzeShot with the SAME inputs the
    // app resolves (analysisFlags + expertBand), instead of the old
    // empty-flags reimpl that silently diverged on flow_trend_ok
    // suppression + the expert-band line.
    //
    // SCOPE / NOT a general guarantee: this resolves kbId from the title
    // with an EMPTY editorType, whereas the app's prepareAnalysisInputs
    // passes the profile's real editorType. matchProfileKey's editor-type
    // fallback (dflow→d-flow/default, aflow→a-flow) is the resolution path
    // for *custom-titled* D-Flow/A-Flow profiles that don't title-match a
    // KB key. For the current tests/data/shots/ corpus this is moot — every
    // fixture title-matches a KB key directly and carries no profile JSON
    // (so frameCount/firstFrameSeconds are -1 on BOTH sides) — so for the
    // corpus the inputs are identical. It is NOT identical for
    // custom-titled editor profiles (band-fire counts would under-report
    // vs the app there), and TstShotCorpus validates shot_eval against its
    // own manifest baseline, NOT against the live app — it cannot, and does
    // not, assert app↔tool equality. Treat the parity as "corpus-scoped,
    // title-match path only." (To make it general: carry editorType in
    // fixtures and thread it here.)
    const QString kbId =
        ShotSummarizer::computeProfileKbId(s.profileTitle, QString());
    const QStringList analysisFlags = ShotSummarizer::getAnalysisFlags(kbId);
    const std::optional<ShotAnalysis::ExpertBand> expertBand =
        ShotSummarizer::expertBandForKbId(kbId);

    const ShotAnalysis::AnalysisResult R = ShotAnalysis::analyzeShot(
        s.pressure, s.flow, /*weight=*/{}, s.conductanceDerivative, s.phases,
        s.beverageType, s.durationSec, s.pressureGoal, s.flowGoal,
        analysisFlags, /*firstFrameConfiguredSeconds=*/-1.0,
        s.targetWeightG, s.yieldG, /*expectedFrameCount=*/-1, expertBand);

    const decenza::BadgeFlags badges =
        decenza::deriveBadgesFromAnalysis(R.detectors);

    // Grind diagnostic columns + the two grind/pour badges — all from the
    // single analyzeShot result (no separate empty-flags call).
    ev.grindDelta   = R.detectors.grindFlowDeltaMlPerSec;
    ev.grindSamples = R.detectors.grindSampleCount;
    ev.grindHasData = R.detectors.grindHasData;
    ev.grindSkipped = !R.detectors.grindChecked;
    ev.grindIssue    = badges.grindIssueDetected;     // the app's badge
    ev.pourTruncated = badges.pourTruncatedDetected;   // the app's badge
    ev.verdictCategory = R.detectors.verdictCategory;

    for (const QVariant& v : R.lines) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == QStringLiteral("verdict"))
            ev.summaryVerdict = m.value(QStringLiteral("text")).toString();
        if (m.value(QStringLiteral("kind")).toString()
                == QStringLiteral("expert_band_deviation")) {
            ev.expertBandFired = true;
            ev.expertBandText = m.value(QStringLiteral("text")).toString();
        }
    }

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

    // verdictCategory (drives the app's tint) + the expert-band fire
    // decision per shot — the A6.1/A6.2 instrument. `summaryVerdict` is the
    // user-facing verdict line from the production analyzeShot.
    o["verdictCategory"] = ev.verdictCategory;
    o["summaryVerdict"] = ev.summaryVerdict;
    QJsonObject expertBand;
    expertBand["fired"] = ev.expertBandFired;
    if (ev.expertBandFired)
        expertBand["text"] = ev.expertBandText;
    o["expertBand"] = expertBand;

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
            // Substring match against the user-facing verdict text — guards
            // wording regressions in the verdict cascade (e.g. the puck-failed
            // "Don't tune off this shot" lead-in from PR #922). Substring
            // rather than exact so manifest authors can lock in the
            // diagnostic phrase without freezing the entire sentence.
            if (expect.contains("summaryVerdictContains")) {
                const QString want = expect.value("summaryVerdictContains").toString();
                if (!ev.summaryVerdict.contains(want, Qt::CaseInsensitive))
                    mismatches << QStringLiteral("summaryVerdictContains: want=%1 got=%2")
                                      .arg(want, ev.summaryVerdict);
            }
            // Expert-band regression lock (Phase B/C): fixtures assert
            // whether the cited band line fired and, optionally, the
            // verdictCategory the band drives. This is the corpus guard
            // that keeps Adaptive v2 / Allongé / the median-flow measure
            // working — without it, band-eligible fixtures are inert.
            if (expect.contains("expertBandFired")) {
                const bool want = expect.value("expertBandFired").toBool();
                if (want != ev.expertBandFired)
                    mismatches << QStringLiteral("expertBandFired: want=%1 got=%2")
                                      .arg(want ? "true" : "false",
                                           ev.expertBandFired ? "true" : "false");
            }
            if (expect.contains("verdictCategory")) {
                const QString want = expect.value("verdictCategory").toString();
                if (want != ev.verdictCategory)
                    mismatches << QStringLiteral("verdictCategory: want=%1 got=%2")
                                      .arg(want, ev.verdictCategory);
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
