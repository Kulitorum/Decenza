// tst_shotsummarizer — verifies that ShotSummarizer's AI-prompt path shares
// the same suppression cascade as the in-app Shot Summary dialog. Issue #921
// closed the gap where ShotSummarizer ran its own channeling/temperature
// detectors on the puck-failure population (peak pressure < PRESSURE_FLOOR_BAR
// = 2.5 bar) and produced misleading observations the AI advisor would then
// dial-in against.
//
// Post-#933 the canonical pipeline is ShotAnalysis::analyzeShot, which
// returns both prose lines and a structured DetectorResults struct.
// ShotSummarizer's live path calls it via the generateSummary wrapper
// (lines only); the historical-shot path (post-#935) reuses
// shotData.summaryLines from ShotHistoryStorage::convertShotRecord's
// analyzeShot pass when present, falling back to an inline re-run for
// legacy or partial shots. Either way the suppression cascade is
// enforced in exactly one place. These tests pin the contract:
// pourTruncatedDetected fires on low-peak shots, channeling/temp lines are
// suppressed, the "Puck failed" warning + verdict reach the prompt, and a
// healthy shot still surfaces the normal observations.

#include <QtTest>

#include <QVariantMap>
#include <QVariantList>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "ai/shotsummarizer.h"
#include "history/shotprojection.h"
#include "models/shotdatamodel.h"
#include "network/visualizeruploader.h"

namespace {

// Append a constant-value sample series sampled at `rateHz` across [t0, t1].
void appendFlat(QVariantList& out, double t0, double t1, double value, double rateHz = 10.0)
{
    const double dt = 1.0 / rateHz;
    for (double t = t0; t <= t1 + 1e-9; t += dt) {
        QVariantMap p;
        p["x"] = t;
        p["y"] = value;
        out.append(p);
    }
}

// Append a phase marker. Defaults to pressure-mode (isFlowMode=false) and
// frameNumber=1 so the marker counts toward reachedExtractionPhase()'s
// "real frame ran" check.
void appendPhase(QVariantList& out, double time, const QString& label,
                 int frameNumber = 1, bool isFlowMode = false)
{
    QVariantMap m;
    m["time"] = time;
    m["label"] = label;
    m["frameNumber"] = frameNumber;
    m["isFlowMode"] = isFlowMode;
    m["transitionReason"] = QString();
    out.append(m);
}

bool linesContain(const QVariantList& lines, const QString& needle)
{
    for (const QVariant& v : lines) {
        if (v.toMap().value("text").toString().contains(needle))
            return true;
    }
    return false;
}

bool linesContainType(const QVariantList& lines, const QString& type)
{
    for (const QVariant& v : lines) {
        if (v.toMap().value("type").toString() == type) return true;
    }
    return false;
}

// Time-series sample for the live-path builder. ShotDataModel::addSample
// takes 10 positional args (time, pressure, flow, temperature, mixTemp,
// pressureGoal, flowGoal, temperatureGoal, frameNumber, isFlowMode); this
// struct elides mixTemp (aliased to temperature inside the builder) and
// frameNumber (production code's addSample marks frameNumber Q_UNUSED, so
// the value never lands anywhere — only the phase markers carry frame
// information). Tests should declare per-sample shapes here and let the
// builder fan them out.
struct LiveSample {
    double t;
    double pressure;
    double flow;
    double temperature;
    double pressureGoal;
    double flowGoal;
    double temperatureGoal;
    bool isFlowMode;
};

// Builds a real ShotDataModel from a flat list of LiveSample structs plus
// matching phase markers. Live-path tests need a ShotDataModel* (not a
// QVariantMap), so we exercise the same public ingestion API the production
// code uses (addSample, addWeightSample, addPhaseMarker), then run
// computeConductanceDerivative() to populate the dC/dt series.
//
// `finalWeight` lets the caller anchor the synthesized cumulative weight
// curve to the same final weight passed to summarize() — without it, the
// curve and the summarize() finalWeight argument disagree, and any future
// test extension that asserts on weight-derived detector output (yield
// arms, weight gained) would silently read inconsistent data. When
// `weightSamples` is supplied explicitly the synthetic curve is skipped.
//
// The ShotDataModel is owned by the caller's stack frame; destruction
// happens at scope exit via RAII, no QObject parent involved.
void populateLiveShot(ShotDataModel* model,
                      const std::vector<LiveSample>& samples,
                      const QList<std::tuple<double, QString, int, bool>>& phases,
                      double finalWeight,
                      const std::vector<QPointF>& weightSamples = {})
{
    for (const auto& [t, label, frameNumber, isFlowMode] : phases) {
        model->addPhaseMarker(t, label, frameNumber, isFlowMode);
    }
    const double totalDuration = samples.empty() ? 0.0 : samples.back().t;
    for (const LiveSample& s : samples) {
        model->addSample(s.t, s.pressure, s.flow, s.temperature, s.temperature,
                         s.pressureGoal, s.flowGoal, s.temperatureGoal,
                         /*frameNumber=*/-1, s.isFlowMode);
        // Synthesize a linear cumulative weight ramp from 0 to finalWeight
        // when the caller didn't supply explicit weight samples. Using
        // finalWeight as the endpoint keeps the curve consistent with the
        // summarize() finalWeight argument so weight-dependent detectors
        // see matching data on both sides.
        if (weightSamples.empty() && totalDuration > 0.0) {
            const double w = (s.t / totalDuration) * finalWeight;
            // ShotDataModel::addWeightSample drops samples below 0.1 g, so
            // skip the start-of-shot 0 g sample explicitly.
            if (w >= 0.1) {
                model->addWeightSample(s.t, w);
            }
        }
    }
    for (const QPointF& w : weightSamples) {
        model->addWeightSample(w.x(), w.y());
    }
    model->computeConductanceDerivative();
}

} // namespace

class tst_ShotSummarizer : public QObject {
    Q_OBJECT

private slots:
    // Puck-failure shape: peak pressure ~1.0 bar across the entire pour
    // window. Without the cascade, dC/dt and temp detectors on
    // ShotSummarizer's old code path would have read off the (nonexistent)
    // pour curves and emitted observations the AI would treat as gospel.
    // generateSummary's cascade now forces channeling/temp/grind to silence
    // and emits only the "Pour never pressurized" warning + the "Don't tune
    // off this shot" verdict.
    void pourTruncatedSuppressesChannelingAndTempLines()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;

        // Pressure that never builds — peak stays at 1.0 bar across the
        // whole pour window. detectPourTruncated fires (peak < 2.5).
        QVariantList pressure;
        appendFlat(pressure, 0.0, 30.0, 1.0);

        // Flow that tracks a normal preinfusion goal — would normally make
        // analyzeFlowVsGoal report "no signal" (delta ~0); cascade ensures
        // we don't emit that as a clean-shot signal.
        QVariantList flow;
        appendFlat(flow, 0.0, 30.0, 1.5);

        // Temperature drifting 5°C below goal — would trigger
        // temperatureUnstable on its own. Must be suppressed.
        QVariantList temperature, temperatureGoal;
        appendFlat(temperature, 0.0, 30.0, 88.0);
        appendFlat(temperatureGoal, 0.0, 30.0, 93.0);

        // Conductance derivative with sustained spikes — would normally
        // trip the channeling detector. Must be suppressed (puck never
        // built, conductance saturates → derivative is meaningless).
        QVariantList derivative;
        appendFlat(derivative, 0.0, 30.0, 5.0);

        QVariantList weight;
        appendFlat(weight, 0.0, 30.0, 36.0);

        QVariantList phases;
        appendPhase(phases, 0.0, QStringLiteral("Preinfusion"), 0);
        appendPhase(phases, 8.0, QStringLiteral("Pour"), 1);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = derivative;
        shot["weight"] = weight;
        shot["phases"] = phases;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();

        ShotSummarizer summarizer;
        ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QVERIFY2(summary.pourTruncatedDetected, "puck-failure shape must set pourTruncatedDetected");
        QVERIFY2(linesContain(summary.summaryLines, QStringLiteral("Pour never pressurized")),
                 "summaryLines must contain the puck-failed warning from generateSummary");
        QVERIFY2(!linesContain(summary.summaryLines, QStringLiteral("Sustained channeling")),
                 "channeling line must be suppressed by the cascade");
        QVERIFY2(!linesContain(summary.summaryLines, QStringLiteral("Temperature drifted")),
                 "temperature drift line must be suppressed by the cascade");
        // Verdict line dominates with the meta-action — see SHOT_REVIEW.md §3.
        QVERIFY2(linesContainType(summary.summaryLines, QStringLiteral("verdict")),
                 "every shot must end with a verdict line");

        const QString prompt = summarizer.buildUserPrompt(summary);
        QVERIFY2(prompt.contains(QStringLiteral("## Detector Observations")),
                 "prompt must include the Detector Observations section header");
        // Verdict is computed (asserted on summary.summaryLines above) but
        // deliberately NOT emitted to the AI prompt — the prescriptive
        // conclusion would anchor the LLM. The AI reasons from the same
        // observations the verdict was built from.
        QVERIFY2(!prompt.contains(QStringLiteral("## Dialog Verdict")),
                 "verdict section must not be rendered in the AI prompt");
        QVERIFY2(!prompt.contains(QStringLiteral("Don't tune off this shot")),
                 "verdict text must not leak into the AI prompt");
        QVERIFY2(prompt.contains(QStringLiteral("Pour never pressurized")),
                 "prompt must surface the puck-failed warning to the AI");
        QVERIFY2(!prompt.contains(QStringLiteral("Puck integrity")),
                 "old hand-rolled 'Puck integrity' line must be gone");
        QVERIFY2(!prompt.contains(QStringLiteral("Temperature deviation")),
                 "old hand-rolled 'Temperature deviation' line must be gone");
        QVERIFY2(!prompt.contains(QStringLiteral("Sustained channeling")),
                 "channeling line must not reach the prompt on a truncated pour");
    }

    // Aborted-during-preinfusion shape: frame 0 only, no real extraction phase.
    // Pin the contract that markPerPhaseTempInstability is gated on
    // ShotAnalysis::reachedExtractionPhase — without the gate, the per-phase
    // prompt block would emit "Temperature instability" on the preheat ramp
    // even though generateSummary correctly suppresses the aggregate caution.
    // Matches the gate the aggregate detector got in PR #898.
    void abortedPreinfusionDoesNotFlagPerPhaseTemp()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 3.0;  // very short — died during preinfusion-start
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 0.5;

        // Pressure built enough to clear pourTruncated (peak >= 2.5 bar) — we
        // want to isolate the reachedExtractionPhase gate, not the puck-failure
        // cascade.
        QVariantList pressure;
        appendFlat(pressure, 0.0, 3.0, 4.0);

        QVariantList flow;
        appendFlat(flow, 0.0, 3.0, 0.5);

        // 5°C below goal — would trigger per-phase temperatureUnstable on its
        // own. Must stay false because the shot never reached extraction.
        QVariantList temperature, temperatureGoal;
        appendFlat(temperature, 0.0, 3.0, 88.0);
        appendFlat(temperatureGoal, 0.0, 3.0, 93.0);

        QVariantList weight;
        appendFlat(weight, 0.0, 3.0, 0.5);

        // Only frame 0 marker — no frame >= 1 sample lasted, so
        // reachedExtractionPhase must return false.
        QVariantList phases;
        appendPhase(phases, 0.0, QStringLiteral("Preinfusion"), 0);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = QVariantList();
        shot["weight"] = weight;
        shot["phases"] = phases;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();

        ShotSummarizer summarizer;
        ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QVERIFY2(!summary.pourTruncatedDetected,
                 "test setup: pressure peaked above floor so pourTruncated should not fire");
        for (const PhaseSummary& phase : summary.phases) {
            QVERIFY2(!phase.temperatureUnstable,
                     "per-phase temp markers must be suppressed when the shot didn't reach extraction");
        }
        const QString prompt = summarizer.buildUserPrompt(summary);
        QVERIFY2(!prompt.contains(QStringLiteral("Temperature instability")),
                 "preheat-ramp drift must not surface in the prompt for aborted-preinfusion shots");
    }

    // When the profile goal steps temperature across phases (flat goal
    // within each phase, different goal each phase — e.g. 82→72°C), the
    // per-phase hasIntentionalTempStepping check returns false but the
    // global detector flags the shot as stepping. The per-phase prose
    // gate must use the global flag, not the per-phase signal, or the
    // prose contradicts the detectorResults envelope.
    void intentionalCrossPhaseSteppingSuppressesPerPhaseTempProse()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;

        QVariantList pressure;
        appendFlat(pressure, 0.0, 8.0, 2.0);
        appendFlat(pressure, 8.0, 30.0, 9.0);

        QVariantList flow;
        appendFlat(flow, 0.0, 30.0, 1.8);

        // Per-phase the goal is flat (82 in preinfusion, 72 in pour). The
        // bounded hasIntentionalTempStepping returns false for both phases
        // (no per-phase range). Globally the goal spans 82→72 = 10°C, well
        // above TEMP_STEPPING_RANGE — the global detector flags the shot as
        // intentionally stepping.
        QVariantList temperature, temperatureGoal;
        appendFlat(temperature, 0.0, 8.0, 79.0);   // 3°C below goal in preinfusion
        appendFlat(temperature, 8.0 + 0.1, 30.0, 69.0);  // 3°C below goal in pour
        appendFlat(temperatureGoal, 0.0, 8.0, 82.0);
        appendFlat(temperatureGoal, 8.0 + 0.1, 30.0, 72.0);

        QVariantList derivative;
        appendFlat(derivative, 0.0, 30.0, 0.0);

        QVariantList weight;
        appendFlat(weight, 0.0, 30.0, 36.0);

        QVariantList phases;
        appendPhase(phases, 0.0, QStringLiteral("Preinfusion"), 0);
        appendPhase(phases, 8.0, QStringLiteral("Pour"), 1);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = derivative;
        shot["weight"] = weight;
        shot["phases"] = phases;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QVERIFY2(summary.tempIntentionalStepping,
                 "82->72 cross-phase goal must set tempIntentionalStepping (matches detectorResults envelope)");
        QVERIFY2(!summary.pourTruncatedDetected,
                 "test setup: 9-bar peak should not trip pourTruncated");

        const QString prompt = summarizer.buildUserPrompt(summary);
        QVERIFY2(!prompt.contains(QStringLiteral("Temperature instability")),
                 "per-phase Temperature instability prose must be suppressed when the profile is intentionally stepping");
    }

    // Defensive: when a fast-path shotData carries no `tempStability`
    // envelope, the .toMap().value(...).toBool() chain must default to
    // false rather than crashing. Without this, suppression on rows
    // without the envelope would silently de-couple from the live path.
    void summarizeFromHistory_fastPathMissingTempStabilityEnvelopeDefaultsFalse()
    {
        QVariantMap shot = buildHealthyShotMap();
        QVariantMap line;
        line["text"] = QStringLiteral("dummy");
        line["type"] = QStringLiteral("good");
        QVariantList lines;
        lines.append(line);
        shot["summaryLines"] = lines;

        // Detector envelope present but no tempStability key.
        // pourTruncated stays false.
        QVariantMap detectors;
        detectors["pourTruncated"] = false;
        shot["detectorResults"] = detectors;

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QVERIFY2(!summary.tempIntentionalStepping,
                 "missing tempStability envelope must default to !intentionalStepping (legacy rows)");
    }

    // Fast path end-to-end: with non-empty summaryLines (skips analyzeShot)
    // and a tempStability envelope flagging intentionalStepping, the per-phase
    // markPerPhaseTempInstability still marks phases unstable (per-phase goal
    // is flat, avg deviation > 2°C) but the gate in buildUserPrompt must
    // suppress the prose because of the global flag. Asserts the prompt
    // string itself, not just the bool — without that, a future refactor
    // that breaks the gate while leaving propagation intact would slip past.
    void summarizeFromHistory_fastPathSuppressesProseWhenSteppingFlagSet()
    {
        QVariantMap shot = buildHealthyShotMap();
        // Override temp curves so each phase has avg deviation ~3°C against
        // a flat-per-phase goal — phase.temperatureUnstable will be set
        // unless the gate suppresses it.
        QVariantList temperature, temperatureGoal;
        appendFlat(temperature, 0.0, 8.0, 79.0);
        appendFlat(temperature, 8.0 + 0.1, 30.0, 69.0);
        appendFlat(temperatureGoal, 0.0, 8.0, 82.0);
        appendFlat(temperatureGoal, 8.0 + 0.1, 30.0, 72.0);
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;

        QVariantMap line;
        line["text"] = QStringLiteral("dummy");
        line["type"] = QStringLiteral("good");
        QVariantList lines;
        lines.append(line);
        shot["summaryLines"] = lines;

        QVariantMap tempStability;
        tempStability["checked"] = true;
        tempStability["intentionalStepping"] = true;
        tempStability["avgDeviationC"] = 3.0;
        tempStability["unstable"] = false;
        QVariantMap detectors;
        detectors["pourTruncated"] = false;
        detectors["tempStability"] = tempStability;
        shot["detectorResults"] = detectors;

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QVERIFY2(summary.tempIntentionalStepping,
                 "fast path must derive tempIntentionalStepping from detectorResults.tempStability");
        const QString prompt = summarizer.buildUserPrompt(summary);
        QVERIFY2(!prompt.contains(QStringLiteral("Temperature instability")),
                 "fast path must suppress per-phase prose when the envelope flags intentional stepping");
    }

    // Negative control for the gate: same temp deviation as the test above,
    // but the envelope explicitly says !intentionalStepping. The per-phase
    // "Temperature instability" prose must still emit. Without this control,
    // an over-aggressive gate (default-true, inverted condition) would
    // silently swallow legitimate warnings on every shot and every other
    // test would still pass.
    void summarizeFromHistory_fastPathEmitsProseWhenNotStepping()
    {
        QVariantMap shot = buildHealthyShotMap();
        QVariantList temperature, temperatureGoal;
        appendFlat(temperature, 0.0, 8.0, 79.0);
        appendFlat(temperature, 8.0 + 0.1, 30.0, 69.0);
        appendFlat(temperatureGoal, 0.0, 8.0, 82.0);
        appendFlat(temperatureGoal, 8.0 + 0.1, 30.0, 72.0);
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;

        QVariantMap line;
        line["text"] = QStringLiteral("dummy");
        line["type"] = QStringLiteral("good");
        QVariantList lines;
        lines.append(line);
        shot["summaryLines"] = lines;

        QVariantMap tempStability;
        tempStability["checked"] = true;
        tempStability["intentionalStepping"] = false;
        tempStability["avgDeviationC"] = 3.0;
        tempStability["unstable"] = true;
        QVariantMap detectors;
        detectors["pourTruncated"] = false;
        detectors["tempStability"] = tempStability;
        shot["detectorResults"] = detectors;

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QVERIFY2(!summary.tempIntentionalStepping,
                 "control: explicit !intentionalStepping must not flip the flag");
        const QString prompt = summarizer.buildUserPrompt(summary);
        QVERIFY2(prompt.contains(QStringLiteral("Temperature instability")),
                 "without the stepping flag the per-phase instability prose must still surface");
    }

    // Sanity: a healthy shot (peak pressure ~9 bar) flows through the same
    // path but pourTruncatedDetected stays false and the cascade does not
    // suppress observations. This guards against an over-aggressive gate.
    void healthyShotKeepsObservationsAndDoesNotTruncate()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;

        QVariantList pressure;
        appendFlat(pressure, 0.0, 8.0, 2.0);     // preinfusion
        appendFlat(pressure, 8.0, 30.0, 9.0);    // pour at full pressure

        QVariantList flow;
        appendFlat(flow, 0.0, 30.0, 2.0);

        QVariantList temperature, temperatureGoal;
        appendFlat(temperature, 0.0, 30.0, 93.0);
        appendFlat(temperatureGoal, 0.0, 30.0, 93.0);

        QVariantList derivative;
        appendFlat(derivative, 0.0, 30.0, 0.0);

        QVariantList weight;
        appendFlat(weight, 0.0, 30.0, 36.0);

        QVariantList phases;
        appendPhase(phases, 0.0, QStringLiteral("Preinfusion"), 0);
        appendPhase(phases, 8.0, QStringLiteral("Pour"), 1);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = derivative;
        shot["weight"] = weight;
        shot["phases"] = phases;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();

        ShotSummarizer summarizer;
        ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QVERIFY2(!summary.pourTruncatedDetected,
                 "healthy 9-bar shot must not be flagged as puck-failure");
        QVERIFY2(!linesContain(summary.summaryLines, QStringLiteral("Pour never pressurized")),
                 "puck-failed warning must be absent on a healthy shot");
        // generateSummary always emits a verdict line; on a clean shot it's
        // "Clean shot. Puck held well." or similar.
        QVERIFY2(linesContainType(summary.summaryLines, QStringLiteral("verdict")),
                 "every shot must end with a verdict line");

        const QString prompt = summarizer.buildUserPrompt(summary);
        QVERIFY2(prompt.contains(QStringLiteral("## Detector Observations")),
                 "Observations section must still render on healthy shots");
        QVERIFY2(!prompt.contains(QStringLiteral("## Dialog Verdict")),
                 "verdict section is never emitted to the AI prompt");
    }
    // ---- Fast path: pre-computed summaryLines from convertShotRecord ----
    //
    // PR #933 made ShotHistoryStorage::convertShotRecord run analyzeShot per
    // shot conversion and stash the prose in shotData["summaryLines"]. The
    // historical-shot AI advisor path used to call generateSummary inline
    // anyway — running the full detector pipeline a second time on the same
    // data. summarizeFromHistory now reuses the pre-computed lines when
    // present, falling back to the inline computation only for legacy
    // shotData maps that didn't flow through convertShotRecord.

    // Helper: build a healthy-shot QVariantMap (peak pressure ~9 bar, normal
    // flow, no drift). Used by the fast/slow path equivalence tests below.
    static QVariantMap buildHealthyShotMap()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;
        shot["targetWeightG"] = 36.0;

        QVariantList pressure, flow, temperature, temperatureGoal, derivative, weight;
        appendFlat(pressure, 0.0, 8.0, 1.0);
        appendFlat(pressure, 8.0, 30.0, 9.0);
        appendFlat(flow, 0.0, 30.0, 1.8);
        appendFlat(temperature, 0.0, 30.0, 92.0);
        appendFlat(temperatureGoal, 0.0, 30.0, 92.0);
        appendFlat(derivative, 0.0, 30.0, 0.0);
        appendFlat(weight, 0.0, 30.0, 36.0);

        QVariantList phases;
        appendPhase(phases, 0.0, QStringLiteral("Preinfusion"), 0);
        appendPhase(phases, 8.0, QStringLiteral("Pour"), 1);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = derivative;
        shot["weight"] = weight;
        shot["phases"] = phases;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();
        return shot;
    }

    // Sentinel test: when shotData carries a non-empty summaryLines field,
    // summarizeFromHistory MUST return those exact lines without recomputing.
    // Achieved by stuffing a clearly-fake sentinel into summaryLines that no
    // real detector would produce — if recomputation ran, the sentinel would
    // be replaced with the real (non-sentinel) line list.
    void summarizeFromHistory_usesPreComputedLines()
    {
        QVariantMap shot = buildHealthyShotMap();

        // Sentinel that no real analyzer would emit.
        QVariantMap sentinel;
        sentinel["text"] = QStringLiteral("__SENTINEL__ pre-computed line");
        sentinel["type"] = QStringLiteral("good");
        QVariantMap sentinelVerdict;
        sentinelVerdict["text"] = QStringLiteral("Verdict: __SENTINEL__");
        sentinelVerdict["type"] = QStringLiteral("verdict");

        QVariantList preLines;
        preLines.append(sentinel);
        preLines.append(sentinelVerdict);
        shot["summaryLines"] = preLines;

        // Also stash a detectorResults map so pourTruncatedDetected gets
        // derived from there rather than computed.
        QVariantMap detectors;
        detectors["pourTruncated"] = false;
        shot["detectorResults"] = detectors;

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QCOMPARE(summary.summaryLines.size(), 2);
        QCOMPARE(summary.summaryLines[0].toMap().value("text").toString(),
                 QStringLiteral("__SENTINEL__ pre-computed line"));
        QCOMPARE(summary.summaryLines[1].toMap().value("text").toString(),
                 QStringLiteral("Verdict: __SENTINEL__"));
        QVERIFY2(!summary.pourTruncatedDetected,
                 "pourTruncatedDetected must be derived from detectorResults.pourTruncated");
    }

    // Fallback test: when summaryLines is missing/empty, the inline detector
    // path still runs and produces real (non-sentinel) lines. Locks in that
    // legacy callers (imported shots, direct test invocations) keep working.
    void summarizeFromHistory_fallsBackWhenNoSummaryLines()
    {
        QVariantMap shot = buildHealthyShotMap();
        // Deliberately omit summaryLines.

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QVERIFY2(!summary.summaryLines.isEmpty(),
                 "fallback inline detector path must populate summaryLines");
        QVERIFY2(linesContainType(summary.summaryLines, QStringLiteral("verdict")),
                 "every shot must end with a verdict line");
        // Healthy shot: should NOT be flagged as truncated.
        QVERIFY2(!summary.pourTruncatedDetected,
                 "healthy shot must not flag pourTruncatedDetected");
    }

    // Equivalence test: a shotData with pre-computed summaryLines AND a
    // shotData without must produce identical summary.summaryLines (modulo
    // the fact that the pre-computed path uses whatever was passed in). To
    // make this meaningful, run the slow path FIRST to get the real lines,
    // then feed those into the fast path and confirm the result matches.
    // This catches drift if the fast-path branch is ever modified to do
    // something different than just reading the pre-computed field.
    void summarizeFromHistory_fastAndSlowPathsAgree()
    {
        QVariantMap slowShot = buildHealthyShotMap();
        ShotSummarizer summarizer;
        const ShotSummary slowSummary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(slowShot));

        // Now build a fast-path shot by stuffing the slow-path's lines and
        // pourTruncated into a fresh map. summarizeFromHistory MUST produce
        // an equivalent summary.
        QVariantMap fastShot = buildHealthyShotMap();
        fastShot["summaryLines"] = slowSummary.summaryLines;
        QVariantMap detectors;
        detectors["pourTruncated"] = slowSummary.pourTruncatedDetected;
        fastShot["detectorResults"] = detectors;
        const ShotSummary fastSummary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(fastShot));

        QCOMPARE(fastSummary.summaryLines.size(), slowSummary.summaryLines.size());
        for (qsizetype i = 0; i < slowSummary.summaryLines.size(); ++i) {
            QCOMPARE(fastSummary.summaryLines[i].toMap().value("text").toString(),
                     slowSummary.summaryLines[i].toMap().value("text").toString());
            QCOMPARE(fastSummary.summaryLines[i].toMap().value("type").toString(),
                     slowSummary.summaryLines[i].toMap().value("type").toString());
        }
        QCOMPARE(fastSummary.pourTruncatedDetected, slowSummary.pourTruncatedDetected);
    }

    // ---- Live-path tests (summarize via ShotDataModel*) ----
    //
    // The history-path tests above feed QVariantMap shapes into
    // summarizeFromHistory(); these mirror them on the live path so the
    // ShotDataModel input adapter doesn't regress. After PR #945 (I) both
    // paths share runShotAnalysisAndPopulate() — so a divergence here
    // would mean the live-path adapter built different inputs (curves,
    // markers, frame info, target weight) than the history adapter for
    // the same shot, not a detector-orchestration drift.

    // Live-path puck-failure: same shape as
    // pourTruncatedSuppressesChannelingAndTempLines but the input is a
    // real ShotDataModel rather than a QVariantMap. Sets pressureGoal=9.0
    // throughout the pour-mode phase so buildChannelingWindows produces
    // a real inclusion window — without that, channeling would stay
    // silent because no flow/pressure goal exists, and the assertion
    // !"Sustained channeling" would pass for the wrong reason.
    void summarize_pourTruncated_suppressesChannelingAndTempLines_live()
    {
        ShotDataModel model;
        std::vector<LiveSample> samples;
        // Preinfusion 0–8 s: flow-mode, flowGoal 1.5.
        for (double t = 0.0; t <= 8.0; t += 0.1) {
            samples.push_back({
                /*t=*/t, /*pressure=*/1.0, /*flow=*/1.5,
                /*temperature=*/88.0, /*pressureGoal=*/0.0, /*flowGoal=*/1.5,
                /*temperatureGoal=*/93.0, /*isFlowMode=*/true});
        }
        // Pour 8–30 s: pressure-mode, pressureGoal 9.0. Actual pressure
        // stays at 1.0 bar to trip pourTruncated; the steady pressureGoal
        // gives buildChannelingWindows a non-empty window, so the cascade
        // actually has something to suppress.
        for (double t = 8.0 + 0.1; t <= 30.0 + 1e-9; t += 0.1) {
            samples.push_back({
                /*t=*/t, /*pressure=*/1.0, /*flow=*/1.5,
                /*temperature=*/88.0, /*pressureGoal=*/9.0, /*flowGoal=*/0.0,
                /*temperatureGoal=*/93.0, /*isFlowMode=*/false});
        }
        populateLiveShot(&model, samples,
            {{0.0, QStringLiteral("Preinfusion"), 0, true},
             {8.0, QStringLiteral("Pour"), 1, false}},
            /*finalWeight=*/36.0);

        ShotMetadata metadata;
        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarize(&model, /*profile=*/nullptr,
            metadata, /*doseWeight=*/18.0, /*finalWeight=*/36.0);

        QVERIFY2(summary.pourTruncatedDetected,
                 "puck-failure shape must set pourTruncatedDetected");
        QVERIFY2(linesContain(summary.summaryLines, QStringLiteral("Pour never pressurized")),
                 "summaryLines must contain the puck-failed warning");
        QVERIFY2(!linesContain(summary.summaryLines, QStringLiteral("Sustained channeling")),
                 "channeling line must be suppressed by the cascade");
        QVERIFY2(!linesContain(summary.summaryLines, QStringLiteral("Temperature drifted")),
                 "temperature drift line must be suppressed by the cascade");
        QVERIFY2(linesContainType(summary.summaryLines, QStringLiteral("verdict")),
                 "every shot must end with a verdict line");
    }

    // Live-path aborted-preinfusion: pin the reachedExtractionPhase gate on
    // the live path. Mirrors abortedPreinfusionDoesNotFlagPerPhaseTemp;
    // sample isFlowMode and the marker isFlowMode are both `false` to
    // match the history-path mirror exactly.
    void summarize_abortedPreinfusion_doesNotFlagPerPhaseTemp_live()
    {
        ShotDataModel model;
        std::vector<LiveSample> samples;
        // Pressure peaks at 4 bar so pourTruncated does NOT fire — we want to
        // isolate the per-phase temp gate, not the puck-failure cascade.
        for (double t = 0.0; t <= 3.0 + 1e-9; t += 0.1) {
            samples.push_back({
                /*t=*/t, /*pressure=*/4.0, /*flow=*/0.5,
                /*temperature=*/88.0, /*pressureGoal=*/0.0, /*flowGoal=*/0.0,
                /*temperatureGoal=*/93.0, /*isFlowMode=*/false});
        }
        // Frame 0 only, isFlowMode=false to mirror the history-path test —
        // reachedExtractionPhase must return false.
        populateLiveShot(&model, samples,
            {{0.0, QStringLiteral("Preinfusion"), 0, false}},
            /*finalWeight=*/0.5);

        ShotMetadata metadata;
        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarize(&model, /*profile=*/nullptr,
            metadata, /*doseWeight=*/18.0, /*finalWeight=*/0.5);

        QVERIFY2(!summary.pourTruncatedDetected,
                 "test setup: 4-bar peak should not trip pourTruncated");
        for (const PhaseSummary& phase : summary.phases) {
            QVERIFY2(!phase.temperatureUnstable,
                     "per-phase temp markers must stay false on aborted-preinfusion shots");
        }
        QVERIFY2(linesContainType(summary.summaryLines, QStringLiteral("verdict")),
                 "every shot must end with a verdict line");
    }

    // Live-path healthy shot: sanity check that the live adapter doesn't
    // over-suppress observations on a clean 9-bar shot. Mirrors
    // healthyShotKeepsObservationsAndDoesNotTruncate.
    void summarize_healthyShot_keepsObservations_live()
    {
        ShotDataModel model;
        std::vector<LiveSample> samples;
        // Preinfusion 0–8 s @ 2 bar, pour 8–30 s @ 9 bar.
        for (double t = 0.0; t <= 8.0; t += 0.1) {
            samples.push_back({
                /*t=*/t, /*pressure=*/2.0, /*flow=*/2.0,
                /*temperature=*/93.0, /*pressureGoal=*/0.0, /*flowGoal=*/2.0,
                /*temperatureGoal=*/93.0, /*isFlowMode=*/true});
        }
        for (double t = 8.0 + 0.1; t <= 30.0 + 1e-9; t += 0.1) {
            samples.push_back({
                /*t=*/t, /*pressure=*/9.0, /*flow=*/2.0,
                /*temperature=*/93.0, /*pressureGoal=*/9.0, /*flowGoal=*/0.0,
                /*temperatureGoal=*/93.0, /*isFlowMode=*/false});
        }
        populateLiveShot(&model, samples,
            {{0.0, QStringLiteral("Preinfusion"), 0, true},
             {8.0, QStringLiteral("Pour"), 1, false}},
            /*finalWeight=*/36.0);

        ShotMetadata metadata;
        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarize(&model, /*profile=*/nullptr,
            metadata, /*doseWeight=*/18.0, /*finalWeight=*/36.0);

        QVERIFY2(!summary.pourTruncatedDetected,
                 "healthy 9-bar shot must not be flagged as puck-failure");
        QVERIFY2(!linesContain(summary.summaryLines, QStringLiteral("Pour never pressurized")),
                 "puck-failed warning must be absent on a healthy shot");
        QVERIFY2(linesContainType(summary.summaryLines, QStringLiteral("verdict")),
                 "every shot must end with a verdict line");
    }

    // Cascade integrity through the fast path: when shotData carries a
    // detectorResults.pourTruncated == true, summarizeFromHistory MUST set
    // summary.pourTruncatedDetected = true AND skip the per-phase temp
    // instability marking, exactly like the slow path's cascade.
    void summarizeFromHistory_fastPathPreservesPourTruncatedCascade()
    {
        QVariantMap shot = buildHealthyShotMap();
        // Stash any non-empty summaryLines (content irrelevant for this assertion).
        QVariantMap line;
        line["text"] = QStringLiteral("dummy");
        line["type"] = QStringLiteral("good");
        QVariantList lines;
        lines.append(line);
        shot["summaryLines"] = lines;

        QVariantMap detectors;
        detectors["pourTruncated"] = true;
        shot["detectorResults"] = detectors;

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QVERIFY2(summary.pourTruncatedDetected,
                 "fast path must derive pourTruncatedDetected from detectorResults");
        // Per-phase temperature markers must NOT be set when pourTruncated fires.
        for (const PhaseSummary& phase : summary.phases) {
            QVERIFY2(!phase.temperatureUnstable,
                     "pourTruncated cascade must suppress per-phase temp markers in fast path");
        }
    }

    // ---- buildPhaseSummariesForRange dedup (post-G) ----
    //
    // The shared helper consolidates ~50 lines of per-marker phase metric
    // computation that used to be duplicated across summarize() and
    // summarizeFromHistory(). These tests exercise it indirectly through
    // the public summarizeFromHistory interface to lock in the dedup
    // contract: degenerate spans contribute no PhaseSummary, per-phase
    // metrics are computed correctly, marker list construction is unchanged.

    // Degenerate span: when two consecutive markers share a timestamp,
    // the helper skips the empty-span phase but the marker stream
    // analyzeShot consumes still gets every marker (frame transitions
    // matter to skip-first-frame detection regardless of span width).
    void summarizeFromHistory_degenerateSpansSkipped()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;

        QVariantList pressure, flow, temperature, temperatureGoal, derivative, weight;
        appendFlat(pressure, 0.0, 8.0, 1.0);
        appendFlat(pressure, 8.0, 30.0, 9.0);
        appendFlat(flow, 0.0, 30.0, 1.8);
        appendFlat(temperature, 0.0, 30.0, 92.0);
        appendFlat(temperatureGoal, 0.0, 30.0, 92.0);
        appendFlat(derivative, 0.0, 30.0, 0.0);
        appendFlat(weight, 0.0, 30.0, 36.0);

        // Three markers, but the first two share a timestamp → first phase
        // is degenerate (endTime == startTime).
        QVariantList phaseList;
        appendPhase(phaseList, 0.0, QStringLiteral("preinfusion"), 0);
        appendPhase(phaseList, 0.0, QStringLiteral("transition"), 1);
        appendPhase(phaseList, 8.0, QStringLiteral("pour"), 2);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = derivative;
        shot["weight"] = weight;
        shot["phases"] = phaseList;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        // marker[0]: startTime=0, endTime=markers[1].time=0 → degenerate, skip.
        // marker[1]: startTime=0, endTime=markers[2].time=8 → 8s span.
        // marker[2]: startTime=8, endTime=30 → 22s span.
        // → 2 PhaseSummary entries expected (the degenerate first marker dropped).
        QCOMPARE(summary.phases.size(), 2);
        QCOMPARE(summary.phases[0].name, QStringLiteral("transition"));
        QCOMPARE(summary.phases[0].startTime, 0.0);
        QCOMPARE(summary.phases[0].endTime, 8.0);
        QCOMPARE(summary.phases[1].name, QStringLiteral("pour"));
        QCOMPARE(summary.phases[1].startTime, 8.0);
        QCOMPARE(summary.phases[1].endTime, 30.0);
    }

    // Per-phase metrics: a known-shape shot must produce known per-phase
    // metric values. Locks in that the helper computes the same
    // averages/extrema/weight-gain as the legacy inline loop.
    void summarizeFromHistory_perPhaseMetricsAreCorrect()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;

        // Two phases: preinfusion 0–7.9s at 1.0 bar / 1.8 ml/s; pour
        // 8.1–30s at 9.0 bar / 1.8 ml/s. Sampling deliberately leaves a
        // gap at t=8.0 (the marker boundary) so calculateAverage's
        // inclusive [start, end] window doesn't pick up either side's
        // boundary sample with the wrong value. Weight ramps linearly
        // 0→36g over [0, 30].
        QVariantList pressure, flow, temperature, temperatureGoal, derivative, weight;
        appendFlat(pressure, 0.0, 7.9, 1.0);
        appendFlat(pressure, 8.1, 30.0, 9.0);
        appendFlat(flow, 0.0, 30.0, 1.8);
        appendFlat(temperature, 0.0, 30.0, 92.0);
        appendFlat(temperatureGoal, 0.0, 30.0, 92.0);
        appendFlat(derivative, 0.0, 30.0, 0.0);
        for (double t = 0.0; t <= 30.0 + 1e-9; t += 0.1) {
            QVariantMap p; p["x"] = t; p["y"] = 36.0 * (t / 30.0);
            weight.append(p);
        }

        QVariantList phaseList;
        appendPhase(phaseList, 0.0, QStringLiteral("Preinfusion"), 0);
        appendPhase(phaseList, 8.0, QStringLiteral("Pour"), 1);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = derivative;
        shot["weight"] = weight;
        shot["phases"] = phaseList;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QCOMPARE(summary.phases.size(), 2);
        // Preinfusion (0–8s): pressure flat at 1.0, flow flat at 1.8,
        // temp flat at 92, weight grew from 0 to ~9.6g.
        QCOMPARE(summary.phases[0].name, QStringLiteral("Preinfusion"));
        QCOMPARE(summary.phases[0].avgPressure, 1.0);
        QCOMPARE(summary.phases[0].avgFlow, 1.8);
        QCOMPARE(summary.phases[0].avgTemperature, 92.0);
        QVERIFY(qFuzzyCompare(summary.phases[0].weightGained, 9.6));
        // Pour (8–30s): pressure flat at 9.0, weight grew ~26.4g.
        QCOMPARE(summary.phases[1].name, QStringLiteral("Pour"));
        QCOMPARE(summary.phases[1].avgPressure, 9.0);
        QVERIFY(qFuzzyCompare(summary.phases[1].weightGained, 26.4));
    }

    // Openspec optimize-dialing-context-payload, task 3: the detector-
    // observations legend (the seven-line preamble explaining
    // [warning]/[caution]/[good]/[observation] tags) lives in the system
    // prompt now, not in every per-call prose body. Per-line tags still
    // emit on individual detector lines; only the legend explanation
    // moved.
    void buildUserPrompt_doesNotCarryDetectorLegendPreamble()
    {
        // Use the puck-failure shape since it generates non-empty
        // detector lines (so the `## Detector Observations` header emits).
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;

        QVariantList pressure;
        appendFlat(pressure, 0.0, 30.0, 1.0);
        QVariantList flow;
        appendFlat(flow, 0.0, 30.0, 1.5);
        QVariantList temperature, temperatureGoal;
        appendFlat(temperature, 0.0, 30.0, 88.0);
        appendFlat(temperatureGoal, 0.0, 30.0, 93.0);
        QVariantList weight;
        appendFlat(weight, 0.0, 30.0, 36.0);
        QVariantList phases;
        appendPhase(phases, 0.0, QStringLiteral("Preinfusion"), 0);
        appendPhase(phases, 8.0, QStringLiteral("Pour"), 1);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = QVariantList();
        shot["weight"] = weight;
        shot["phases"] = phases;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));
        const QString prompt = summarizer.buildUserPrompt(summary);

        QVERIFY2(prompt.contains(QStringLiteral("## Detector Observations")),
                 "section header still emits when detector lines are present");
        // The legend preamble lines must NOT appear in the prose.
        QVERIFY2(!prompt.contains(QStringLiteral("The lines below come from the same deterministic detectors")),
                 "legend preamble must NOT appear in per-call prose");
        QVERIFY2(!prompt.contains(QStringLiteral("Severity tags reflect detector confidence")),
                 "legend explanation must NOT appear in per-call prose");
        QVERIFY2(!prompt.contains(QStringLiteral("[warning] high-confidence failure mode")),
                 "legend explanation bullets must NOT appear in per-call prose");
        // Per-line tags themselves are still rendered.
        QVERIFY2(prompt.contains(QStringLiteral("[warning]")) || prompt.contains(QStringLiteral("[caution]")),
                 "per-line severity tags still emit on detector lines");
    }

    void shotAnalysisSystemPrompt_carriesDetectorLegend_espresso()
    {
        const QString prompt = ShotSummarizer::shotAnalysisSystemPrompt(
            QStringLiteral("espresso"), QStringLiteral("80's Espresso"),
            QString(), QString());
        QVERIFY2(prompt.contains(QStringLiteral("Reading Detector Observations")),
                 "system prompt must teach how to read detector tags");
        QVERIFY2(prompt.contains(QStringLiteral("[warning] high-confidence failure mode")),
                 "system prompt must contain the warning-tag explanation");
        QVERIFY2(prompt.contains(QStringLiteral("[caution] directional hint")),
                 "system prompt must contain the caution-tag explanation");
        QVERIFY2(prompt.contains(QStringLiteral("[good] positive signal")),
                 "system prompt must contain the good-tag explanation");
        QVERIFY2(prompt.contains(QStringLiteral("[observation] context")),
                 "system prompt must contain the observation-tag explanation");
    }

    void shotAnalysisSystemPrompt_carriesDetectorLegend_filter()
    {
        // Filter still wants the legend — detector observations apply
        // across beverage types (skip-first-frame, channeling).
        const QString prompt = ShotSummarizer::shotAnalysisSystemPrompt(
            QStringLiteral("filter"), QStringLiteral("Generic Filter"),
            QString(), QString());
        QVERIFY2(prompt.contains(QStringLiteral("Reading Detector Observations")),
                 "filter system prompt must also carry the detector legend");
    }

    // Openspec optimize-dialing-context-payload, task 4.4: the system
    // prompt teaches structural-field gating once per conversation,
    // replacing per-call framing strings (`tastingFeedback.recommendation`,
    // `inferredNote`, `daysSinceRoastNote`) that the AI was skimming past.
    void shotAnalysisSystemPrompt_carriesStructuralFieldGuidance()
    {
        const QString prompt = ShotSummarizer::shotAnalysisSystemPrompt(
            QStringLiteral("espresso"), QStringLiteral("80's Espresso"),
            QString(), QString());
        QVERIFY2(prompt.contains(QStringLiteral("How to Read Structured Fields")),
                 "system prompt must include the structural-field guidance section");
        // tastingFeedback gating
        QVERIFY2(prompt.contains(QStringLiteral("tastingFeedback")),
                 "system prompt must teach tastingFeedback gating");
        QVERIFY2(prompt.contains(QStringLiteral("ASK the user how the shot tasted"))
                 || prompt.contains(QStringLiteral("ASK the user")),
                 "system prompt must contain the imperative ASK directive for taste");
        // beanFreshness gating
        QVERIFY2(prompt.contains(QStringLiteral("beanFreshness")),
                 "system prompt must teach beanFreshness gating");
        QVERIFY2(prompt.contains(QStringLiteral("freshnessKnown")),
                 "system prompt must reference the freshnessKnown gate");
        QVERIFY2(prompt.contains(QStringLiteral("freeze")),
                 "system prompt must mention freezing as the storage variable that breaks calendar age");
        // currentBean is sourced solely from the resolved shot, so no
        // fields are "inferred" — the system prompt SHALL NOT carry an
        // inferredFields clause.
        QVERIFY2(!prompt.contains(QStringLiteral("inferredFields")),
                 "system prompt must NOT teach a removed inferredFields field");
        // Empty-string semantics: an empty currentBean field means the
        // shot did not record it, not that the user has no grinder/bean.
        // The prompt MUST teach this so the LLM doesn't read a blank as a
        // negation. Match on a stable phrase from the prompt body.
        QVERIFY2(prompt.contains(QStringLiteral("did NOT record")),
                 "system prompt must teach empty-string semantics for currentBean fields");
    }

    // Openspec optimize-dialing-context-payload, tasks 8 + 9: the prose
    // body carries shot-VARIABLE data only. Bean identity (`Coffee:`),
    // roast date (`roasted YYYY-MM-DD`), grinder brand/model/burrs, and
    // profile identity (`Profile:` / `Profile intent:` / `## Profile
    // Recipe`) all live in structured JSON blocks (`currentBean`,
    // `currentBean.beanFreshness`, `dialInSessions[].context`,
    // `result.profile`). The grinder *setting* is shot-variable so it
    // still emits, on a renamed `Grind setting:` line that carries no
    // brand/model/burrs prefix.
    void buildUserPrompt_carriesOnlyShotVariableFields()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;
        shot["profileName"] = QStringLiteral("80's Espresso");
        shot["profileNotes"] = QStringLiteral("0.5–1.2 ml/s target through extraction");
        shot["beanBrand"] = QStringLiteral("Northbound Coffee Roasters");
        shot["beanType"] = QStringLiteral("Spring Tour 2026 #2");
        shot["roastLevel"] = QStringLiteral("Dark");
        shot["roastDate"] = QStringLiteral("2026-03-30");
        shot["grinderBrand"] = QStringLiteral("Niche");
        shot["grinderModel"] = QStringLiteral("Zero");
        shot["grinderBurrs"] = QStringLiteral("63mm Mazzer Kony conical");
        shot["grinderSetting"] = QStringLiteral("4.0");

        QVariantList pressure, flow, temperature, temperatureGoal, derivative, weight;
        appendFlat(pressure, 0.0, 8.0, 2.0);
        appendFlat(pressure, 8.0, 30.0, 9.0);
        appendFlat(flow, 0.0, 30.0, 1.8);
        appendFlat(temperature, 0.0, 30.0, 92.0);
        appendFlat(temperatureGoal, 0.0, 30.0, 92.0);
        appendFlat(derivative, 0.0, 30.0, 0.0);
        appendFlat(weight, 0.0, 30.0, 36.0);

        QVariantList phases;
        appendPhase(phases, 0.0, QStringLiteral("Preinfusion"), 0);
        appendPhase(phases, 8.0, QStringLiteral("Pour"), 1);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = derivative;
        shot["weight"] = weight;
        shot["phases"] = phases;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));
        const QString prompt = summarizer.buildUserPrompt(summary);

        // The user prompt is a JSON envelope. Bean/grinder identity lives
        // in `currentBean.*`; profile identity lives in `profile.*` —
        // matching dialing_get_context's response shape so a single system
        // prompt reads correctly off either surface. The "must not appear
        // in prose" guarantees still hold against the prose body, which
        // now lives under the `shotAnalysis` key.
        const QJsonObject payload = QJsonDocument::fromJson(prompt.toUtf8()).object();
        const QString prose = payload.value(QStringLiteral("shotAnalysis")).toString();
        const QJsonObject currentBean = payload.value(QStringLiteral("currentBean")).toObject();
        const QJsonObject profileBlock = payload.value(QStringLiteral("profile")).toObject();

        // Shot-variable data still emits in the prose body.
        QVERIFY2(prose.contains(QStringLiteral("**Dose**")),
                 "shot-variable Dose line must still emit in shotAnalysis prose");
        QVERIFY2(prose.contains(QStringLiteral("**Yield**")),
                 "shot-variable Yield line must still emit in shotAnalysis prose");
        QVERIFY2(prose.contains(QStringLiteral("**Duration**")),
                 "shot-variable Duration line must still emit in shotAnalysis prose");
        QVERIFY2(prose.contains(QStringLiteral("**Grind setting**: 4.0")),
                 "shot-variable grinder *setting* still emits on a brand/model-free line");

        // Profile identity is structured under `profile` (not `currentProfile`
        // — matches dialing_get_context's key naming).
        QVERIFY2(!payload.contains(QStringLiteral("currentProfile")),
                 "key must be `profile`, not `currentProfile` (system prompt teaches `result.profile.*`)");
        QCOMPARE(profileBlock.value(QStringLiteral("title")).toString(),
                 QStringLiteral("80's Espresso"));
        QCOMPARE(profileBlock.value(QStringLiteral("intent")).toString(),
                 QStringLiteral("0.5–1.2 ml/s target through extraction"));
        QVERIFY2(!prose.contains(QStringLiteral("**Profile**:")) &&
                 !prose.contains(QStringLiteral("Profile:")),
                 "Profile line must NOT appear in shotAnalysis prose");
        QVERIFY2(!prose.contains(QStringLiteral("Profile intent:")) &&
                 !prose.contains(QStringLiteral("**Profile intent**:")),
                 "Profile intent line must NOT appear in shotAnalysis prose");
        QVERIFY2(!prose.contains(QStringLiteral("## Profile Recipe")),
                 "Profile Recipe section must NOT appear in shotAnalysis prose");

        // Bean identity is now structured under currentBean and must NOT
        // appear inside the prose body.
        QCOMPARE(currentBean.value(QStringLiteral("brand")).toString(),
                 QStringLiteral("Northbound Coffee Roasters"));
        QCOMPARE(currentBean.value(QStringLiteral("type")).toString(),
                 QStringLiteral("Spring Tour 2026 #2"));
        QVERIFY2(!prose.contains(QStringLiteral("Northbound Coffee Roasters")),
                 "bean brand must NOT appear in shotAnalysis prose");
        QVERIFY2(!prose.contains(QStringLiteral("Spring Tour 2026 #2")),
                 "bean type must NOT appear in shotAnalysis prose");
        QVERIFY2(!prose.contains(QStringLiteral("**Coffee**")) &&
                 !prose.contains(QStringLiteral("Coffee:")),
                 "Coffee line must NOT appear in shotAnalysis prose");

        // Roast date now lives in currentBean.beanFreshness, NOT in prose.
        const QJsonObject beanFreshness = currentBean.value(QStringLiteral("beanFreshness")).toObject();
        QCOMPARE(beanFreshness.value(QStringLiteral("roastDate")).toString(),
                 QStringLiteral("2026-03-30"));
        QCOMPARE(beanFreshness.value(QStringLiteral("freshnessKnown")).toBool(), false);
        QVERIFY2(!prose.contains(QStringLiteral("2026-03-30")),
                 "roast date must NOT appear in shotAnalysis prose");
        QVERIFY2(!prose.contains(QStringLiteral("roasted ")),
                 "no `roasted YYYY-MM-DD` literal allowed in shotAnalysis prose");
        QVERIFY2(!prose.contains(QStringLiteral("days since roast")),
                 "prose must NOT precompute a day-count parenthetical");
        QVERIFY2(!prose.contains(QStringLiteral("days post-roast")),
                 "prose must NOT use any day-count phrasing");

        // Grinder brand/model/burrs are structured under currentBean.grinder*
        // and must NOT appear in the prose body.
        QCOMPARE(currentBean.value(QStringLiteral("grinderBrand")).toString(),
                 QStringLiteral("Niche"));
        QCOMPARE(currentBean.value(QStringLiteral("grinderModel")).toString(),
                 QStringLiteral("Zero"));
        QCOMPARE(currentBean.value(QStringLiteral("grinderBurrs")).toString(),
                 QStringLiteral("63mm Mazzer Kony conical"));
        QVERIFY2(!prose.contains(QStringLiteral("Niche")),
                 "grinder brand must NOT appear in shotAnalysis prose");
        QVERIFY2(!prose.contains(QStringLiteral("63mm Mazzer Kony conical")),
                 "grinder burr identity must NOT appear in shotAnalysis prose");
        QVERIFY2(!prose.contains(QStringLiteral("**Grinder**")) &&
                 !prose.contains(QStringLiteral("- Grinder:")),
                 "Grinder identity line must NOT appear in shotAnalysis prose (only Grind setting:)");
    }

    // Openspec optimize-dialing-context-payload, task 8.3 / 9.4: the
    // system prompt teaches where each piece of data lives. The "How to
    // Read Structured Fields" section gained pointers to result.profile
    // (canonical surface for profile metadata) and currentBean (canonical
    // surface for bean/grinder identity).
    void shotAnalysisSystemPrompt_teachesCanonicalSourcesForProfileAndBean()
    {
        const QString prompt = ShotSummarizer::shotAnalysisSystemPrompt(
            QStringLiteral("espresso"), QStringLiteral("80's Espresso"),
            QString(), QString());
        QVERIFY2(prompt.contains(QStringLiteral("`result.profile`")),
                 "system prompt must point at result.profile as canonical profile surface");
        QVERIFY2(prompt.contains(QStringLiteral("intent")) &&
                 prompt.contains(QStringLiteral("recipe")),
                 "system prompt must name profile intent + recipe as living in result.profile");
        QVERIFY2(prompt.contains(QStringLiteral("`currentBean`")),
                 "system prompt must point at currentBean as canonical bean/grinder identity surface");
    }

    // Openspec optimize-dialing-context-payload, task 10.5: Standalone vs
    // HistoryBlock render modes differ ONLY in the two top-level header
    // lines (`## Shot Summary` and `## Detector Observations`). Body
    // content (dose, yield, duration, grind setting, peaks, phase data,
    // per-line detector tags, tasting feedback) is identical so the AI
    // sees the same shot facts under either wrapper.
    void buildUserPrompt_historyBlockMode_omitsOnlyTopLevelHeaders()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;
        shot["grinderSetting"] = QStringLiteral("4.0");

        QVariantList pressure, flow, temperature, temperatureGoal, derivative, weight;
        appendFlat(pressure, 0.0, 8.0, 2.0);
        appendFlat(pressure, 8.0, 30.0, 9.0);
        appendFlat(flow, 0.0, 30.0, 1.8);
        appendFlat(temperature, 0.0, 30.0, 92.0);
        appendFlat(temperatureGoal, 0.0, 30.0, 92.0);
        appendFlat(derivative, 0.0, 30.0, 0.0);
        appendFlat(weight, 0.0, 30.0, 36.0);
        QVariantList phases;
        appendPhase(phases, 0.0, QStringLiteral("Preinfusion"), 0);
        appendPhase(phases, 8.0, QStringLiteral("Pour"), 1);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = derivative;
        shot["weight"] = weight;
        shot["phases"] = phases;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();

        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));
        const QString standalone = summarizer.buildUserPrompt(summary, ShotSummarizer::RenderMode::Standalone);
        const QString historyBlock = summarizer.buildUserPrompt(summary, ShotSummarizer::RenderMode::HistoryBlock);

        // Standalone carries the two top-level header lines.
        QVERIFY2(standalone.contains(QStringLiteral("## Shot Summary")),
                 "Standalone mode must emit ## Shot Summary header");

        // HistoryBlock omits ONLY those two header lines.
        QVERIFY2(!historyBlock.contains(QStringLiteral("## Shot Summary")),
                 "HistoryBlock must NOT emit ## Shot Summary (caller wraps in ### Shot (date))");
        QVERIFY2(!historyBlock.contains(QStringLiteral("## Detector Observations")),
                 "HistoryBlock must NOT emit ## Detector Observations header");

        // Body content is identical between modes.
        QVERIFY2(historyBlock.contains(QStringLiteral("**Dose**")),
                 "HistoryBlock must still carry shot-variable Dose");
        QVERIFY2(historyBlock.contains(QStringLiteral("**Grind setting**: 4.0")),
                 "HistoryBlock must still carry the per-shot grinder setting");
        QVERIFY2(historyBlock.contains(QStringLiteral("## Phase Data")),
                 "HistoryBlock must still emit Phase Data section (shot-variable diagnostic)");
        QVERIFY2(historyBlock.contains(QStringLiteral("## Tasting Feedback")),
                 "HistoryBlock must still emit Tasting Feedback section");
    }

    // Openspec optimize-dialing-context-payload, task 10.4: buildHistoryContext
    // hoists Profile + Recipe to a single header at the top of its
    // output rather than emitting them per shot.
    void buildHistoryContext_hoistsProfileAndRecipeToSingleHeader()
    {
        QVariantList shots;
        for (int i = 0; i < 3; ++i) {
            QVariantMap m;
            m["id"] = i + 1;
            m["timestampIso"] = QStringLiteral("2026-04-30T10:0%1:00").arg(i);
            m["profileName"] = QStringLiteral("80's Espresso");
            m["doseWeightG"] = 18.0;
            m["finalWeightG"] = 36.0 + i;  // small variation
            m["durationSec"] = 30.0;
            // Minimal valid profile JSON so describeFramesFromJson returns something.
            m["profileJson"] = QStringLiteral(
                R"({"version":2,"title":"80's Espresso","steps":[)"
                R"({"name":"preinfusion","seconds":8,"flow":4.0,"temperature":92,"transition":"fast"},)"
                R"({"name":"pour","seconds":22,"pressure":9.0,"temperature":92,"transition":"smooth"}]})");
            shots.append(m);
        }

        const QString out = ShotSummarizer::buildHistoryContext(shots);

        // Single Profile header at top.
        QVERIFY2(out.contains(QStringLiteral("### Profile: 80's Espresso")),
                 "history context must emit the Profile header once at the top");
        QCOMPARE(out.count(QStringLiteral("### Profile:")), 1);
        // The recipe (## Profile Recipe ...) is hoisted to the same top
        // section, so it appears at most once in the whole output.
        QVERIFY2(out.count(QStringLiteral("## Profile Recipe")) <= 1,
                 "Profile Recipe must appear at most once in history context");
        // Per-shot blocks must NOT carry the per-shot Profile/Recipe lines.
        QVERIFY2(!out.contains(QStringLiteral("- Profile: ")),
                 "per-shot blocks must not carry `- Profile:` lines");
        QVERIFY2(!out.contains(QStringLiteral("- Recipe: ")),
                 "per-shot blocks must not carry `- Recipe:` lines");
    }

    // openspec migrate-advisor-user-prompt-to-json: byte-stability is the
    // load-bearing precondition for prompt caching. Anthropic's cache_control
    // hits when the cached prefix is byte-equivalent to the new request, so
    // any per-call drift (wall-clock, request id, locale-formatted floats)
    // would silently bust the cache. Pin determinism here.
    void buildUserPrompt_byteStableForSameInput()
    {
        QVariantMap shot = makeHealthyShotMap();
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString a = summarizer.buildUserPrompt(summary);
        const QString b = summarizer.buildUserPrompt(summary);
        QCOMPARE(a, b);
        QVERIFY2(!a.isEmpty(), "Standalone JSON payload must not be empty for a populated summary");
    }

    // openspec migrate-advisor-user-prompt-to-json: explicit guard against
    // a wall-clock or per-call value sneaking into the payload. The
    // dialing_get_context tool ships `currentDateTime` at the top of its
    // response — that field MUST NOT appear in the in-app advisor's user
    // prompt, otherwise the cache breaks every minute.
    void buildUserPrompt_omitsWallClockFields()
    {
        QVariantMap shot = makeHealthyShotMap();
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary);
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(prompt.toUtf8(), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());
        const QJsonObject obj = doc.object();

        const QStringList forbidden = {
            QStringLiteral("currentDateTime"),
            QStringLiteral("requestId"),
            QStringLiteral("nowMs"),
            QStringLiteral("nowSec"),
            QStringLiteral("timestamp"),
            QStringLiteral("clock")
        };
        for (const QString& key : forbidden) {
            QVERIFY2(!obj.contains(key),
                     qPrintable(QStringLiteral("payload must not carry wall-clock-ish key: %1").arg(key)));
        }
    }

    // openspec migrate-advisor-user-prompt-to-json: out-of-scope fields
    // (dialInSessions / bestRecentShot / sawPrediction / grinderContext)
    // need DB or MainController scope the in-app advisor lacks. They MUST
    // be omitted, not nulled — `null` would mislead the LLM into "we
    // checked and there isn't one" when the truth is "we didn't check."
    void buildUserPrompt_omitsOutOfScopeKeys()
    {
        QVariantMap shot = makeHealthyShotMap();
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary);
        const QJsonObject obj = QJsonDocument::fromJson(prompt.toUtf8()).object();

        const QStringList outOfScope = {
            QStringLiteral("dialInSessions"),
            QStringLiteral("bestRecentShot"),
            QStringLiteral("sawPrediction"),
            QStringLiteral("grinderContext")
        };
        for (const QString& key : outOfScope) {
            QVERIFY2(!obj.contains(key),
                     qPrintable(QStringLiteral("payload must not carry out-of-scope key: %1").arg(key)));
        }
    }

    // openspec migrate-advisor-user-prompt-to-json: shotAnalysis prose
    // preservation. The prose body the legacy buildUserPrompt produced
    // moves under `shotAnalysis` verbatim — same headers, same per-line
    // tags, same numeric formatting. Regex consumers
    // (AIConversation::processShotForConversation,
    // AIConversation::summarizeShotMessage) match on those substrings;
    // any drift breaks change-detection between adjacent shots in a
    // multi-shot conversation.
    void buildUserPrompt_shotAnalysisFieldPreservesProseSubstrings()
    {
        QVariantMap shot = makeHealthyShotMap();
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary);
        const QJsonObject obj = QJsonDocument::fromJson(prompt.toUtf8()).object();
        QVERIFY2(obj.contains(QStringLiteral("shotAnalysis")),
                 "Standalone payload must carry shotAnalysis field");

        const QString analysis = obj.value(QStringLiteral("shotAnalysis")).toString();
        QVERIFY2(analysis.contains(QStringLiteral("## Shot Summary")),
                 "shotAnalysis must preserve the ## Shot Summary header");
        QVERIFY2(analysis.contains(QStringLiteral("**Dose**:")),
                 "shotAnalysis must preserve **Dose** marker (regex consumers depend on it)");
        QVERIFY2(analysis.contains(QStringLiteral("**Yield**:")),
                 "shotAnalysis must preserve **Yield** marker");
        QVERIFY2(analysis.contains(QStringLiteral("**Duration**:")),
                 "shotAnalysis must preserve **Duration** marker");
    }

    // tastingFeedback ships only the structural booleans —
    // hasEnjoymentScore / hasNotes / hasRefractometer. The "ask the user
    // before suggesting changes" framing is taught once by the system
    // prompt's "How to Read Structured Fields" section, not repeated as a
    // per-call `recommendation` string. Mirrors dialing_get_context's
    // tastingFeedback shape so a single system prompt reads correctly off
    // either surface.
    void buildUserPrompt_tastingFeedbackBooleansOnly()
    {
        QVariantMap shot = makeHealthyShotMap();
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary);
        const QJsonObject obj = QJsonDocument::fromJson(prompt.toUtf8()).object();
        QVERIFY(obj.contains(QStringLiteral("tastingFeedback")));

        const QJsonObject tf = obj.value(QStringLiteral("tastingFeedback")).toObject();
        QCOMPARE(tf.value(QStringLiteral("hasEnjoymentScore")).toBool(), false);
        QCOMPARE(tf.value(QStringLiteral("hasNotes")).toBool(), false);
        QCOMPARE(tf.value(QStringLiteral("hasRefractometer")).toBool(), false);
        QVERIFY2(!tf.contains(QStringLiteral("recommendation")),
                 "per-call recommendation framing was moved to the system prompt — "
                 "block must NOT include a recommendation field (matches dialing_get_context)");
    }

    // openspec migrate-advisor-user-prompt-to-json: HistoryBlock mode
    // stays prose. JSON-per-shot under a `### Shot (date)` header would
    // be unreadable when concatenated, and the multi-shot history caller
    // hoists profile/setup identity to a single header above the blocks.
    void buildUserPrompt_historyBlockModeReturnsProseNotJson()
    {
        QVariantMap shot = makeHealthyShotMap();
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary, ShotSummarizer::RenderMode::HistoryBlock);
        QVERIFY2(!prompt.trimmed().startsWith(QLatin1Char('{')),
                 "HistoryBlock output must be prose, not a JSON object");
        QJsonParseError err{};
        QJsonDocument::fromJson(prompt.toUtf8(), &err);
        QVERIFY2(err.error != QJsonParseError::NoError,
                 "HistoryBlock prose must not coincidentally parse as JSON");
    }

    // helper for the openspec tests — minimal shot with all fields populated
    // enough for summarizeFromHistory to return a non-empty summary.
    static QVariantMap makeHealthyShotMap()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 28.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;
        shot["targetWeightG"] = 36.0;

        QVariantList pressure, flow, temperature, temperatureGoal, derivative, weight;
        appendFlat(pressure, 0.0, 8.0, 2.0);
        appendFlat(pressure, 8.0, 28.0, 9.0);
        appendFlat(flow, 0.0, 28.0, 2.0);
        appendFlat(temperature, 0.0, 28.0, 92.0);
        appendFlat(temperatureGoal, 0.0, 28.0, 93.0);
        appendFlat(derivative, 0.0, 28.0, 0.0);
        appendFlat(weight, 0.0, 28.0, 36.0);

        QVariantList phases;
        appendPhase(phases, 0.0, QStringLiteral("Preinfusion"), 0);
        appendPhase(phases, 8.0, QStringLiteral("Pour"), 1);

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = derivative;
        shot["weight"] = weight;
        shot["phases"] = phases;
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();
        shot["beanBrand"] = QStringLiteral("Northbound");
        shot["beanType"] = QStringLiteral("Spring Tour");
        shot["roastLevel"] = QStringLiteral("medium-light");
        shot["grinderBrand"] = QStringLiteral("Niche");
        shot["grinderModel"] = QStringLiteral("Zero");
        shot["grinderBurrs"] = QStringLiteral("63mm Mazzer Kony conical");
        shot["grinderSetting"] = QStringLiteral("4.5");
        shot["profileName"] = QStringLiteral("80's Espresso");
        return shot;
    }

    // ---------------------------------------------------------------------
    // Structured per-phase JSON (issue #1037). The user-prompt envelope's
    // `shot` block now carries `phases[]`, `detectorObservations[]`,
    // and `overallPeaks` so consumers can iterate over phase data and
    // detector signals without pattern-matching prose.
    // ---------------------------------------------------------------------
    void buildUserPrompt_shotBlock_carriesStructuredPhasesAndDetectors()
    {
        QVariantMap shot = buildHealthyShotMap();
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary);
        const QJsonObject payload = QJsonDocument::fromJson(prompt.toUtf8()).object();
        QVERIFY(payload.contains(QStringLiteral("shot")));

        const QJsonObject shotBlock = payload.value(QStringLiteral("shot")).toObject();
        QVERIFY2(shotBlock.contains(QStringLiteral("overallPeaks")),
                 "shot.overallPeaks must ship for any shot with a non-trivial pressure or flow curve");
        const QJsonObject overall = shotBlock.value(QStringLiteral("overallPeaks")).toObject();
        QVERIFY(overall.contains(QStringLiteral("pressureBar")));
        const QJsonObject pPeak = overall.value(QStringLiteral("pressureBar")).toObject();
        QVERIFY(pPeak.contains(QStringLiteral("value")));
        QVERIFY(pPeak.contains(QStringLiteral("atSec")));

        QVERIFY2(shotBlock.contains(QStringLiteral("phases")),
                 "shot.phases must ship when the shot has phase markers");
        const QJsonArray phases = shotBlock.value(QStringLiteral("phases")).toArray();
        QVERIFY2(!phases.isEmpty(),
                 "phases array must be non-empty for a healthy shot");
        const QJsonObject firstPhase = phases[0].toObject();
        QVERIFY(firstPhase.contains(QStringLiteral("name")));
        QVERIFY(firstPhase.contains(QStringLiteral("durationSec")));
        QVERIFY(firstPhase.contains(QStringLiteral("controlMode")));
        // Human-readable enum, not a numeric code.
        const QString controlMode = firstPhase.value(QStringLiteral("controlMode")).toString();
        QVERIFY2(controlMode == QStringLiteral("flow") || controlMode == QStringLiteral("pressure"),
                 qPrintable(QString("controlMode must be 'flow' or 'pressure', got: %1").arg(controlMode)));
    }

    // detectorObservations[] omits the `verdict` line so the LLM still
    // reasons independently from raw detector signals (see the long
    // rationale in renderShotAnalysisProse). The deterministic verdict
    // would anchor the LLM on a pre-cooked answer.
    void buildUserPrompt_shotBlock_detectorObservationsOmitsVerdict()
    {
        QVariantMap shot = buildHealthyShotMap();
        // Inject a synthetic summary line set so the verdict-suppression
        // contract is exercised regardless of the analyzer's classification.
        QVariantList lines;
        lines.append(QVariantMap{
            {"type", QStringLiteral("warning")},
            {"text", QStringLiteral("Channeling detected during pour")}});
        lines.append(QVariantMap{
            {"type", QStringLiteral("verdict")},
            {"text", QStringLiteral("Coarsen significantly.")}});
        shot["summaryLines"] = lines;

        ShotSummarizer summarizer;
        ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));
        // Force the synthetic summary so the test does not depend on the
        // detector pipeline's classification of the healthy fixture.
        summary.summaryLines = lines;

        const QString prompt = summarizer.buildUserPrompt(summary);
        const QJsonObject payload = QJsonDocument::fromJson(prompt.toUtf8()).object();
        const QJsonArray detectors = payload.value(QStringLiteral("shot")).toObject()
            .value(QStringLiteral("detectorObservations")).toArray();
        bool sawWarning = false;
        bool sawVerdict = false;
        for (const QJsonValue& v : detectors) {
            const QString type = v.toObject().value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("warning")) sawWarning = true;
            if (type == QStringLiteral("verdict")) sawVerdict = true;
        }
        QVERIFY2(sawWarning, "detectorObservations must carry warning lines");
        QVERIFY2(!sawVerdict, "detectorObservations must NOT carry the verdict line");
    }
};

QTEST_GUILESS_MAIN(tst_ShotSummarizer)

#include "tst_shotsummarizer.moc"
