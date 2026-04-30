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

#include "ai/shotsummarizer.h"
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

// Time-series sample for the live-path builder. addSample() takes a flat
// argument list, but the tests want declarative shapes. Using a struct
// keeps the call sites readable when there are 8+ values per sample.
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

// Builds a real ShotDataModel from a flat list of LiveSample structs and a
// matching set of phase markers. Live-path tests need a ShotDataModel*
// (not a QVariantMap), so we exercise the same public ingestion API the
// production code uses (addSample, addWeightSample, addPhaseMarker), then
// run computeConductanceDerivative() to populate the dC/dt series.
//
// Lives in test scope rather than as a generic test fixture so the
// ShotDataModel parent (the QObject* arg) stays explicit at the call
// site — every test method owns its model and disposes via QObject parent
// ownership.
void populateLiveShot(ShotDataModel* model,
                      const std::vector<LiveSample>& samples,
                      const QList<std::tuple<double, QString, int, bool>>& phases,
                      const std::vector<QPointF>& weightSamples = {})
{
    for (const auto& [t, label, frameNumber, isFlowMode] : phases) {
        model->addPhaseMarker(t, label, frameNumber, isFlowMode);
    }
    for (const LiveSample& s : samples) {
        model->addSample(s.t, s.pressure, s.flow, s.temperature, s.temperature,
                         s.pressureGoal, s.flowGoal, s.temperatureGoal,
                         /*frameNumber=*/-1, s.isFlowMode);
        // Synthesise a default cumulative weight sample if the caller didn't
        // supply one — enough to give findValueAtTime something to interpolate.
        if (weightSamples.empty()) {
            model->addWeightSample(s.t, /*weight=*/s.t * 1.2);
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
        shot["duration"] = 30.0;
        shot["doseWeight"] = 18.0;
        shot["finalWeight"] = 36.0;

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
        ShotSummary summary = summarizer.summarizeFromHistory(shot);

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
        shot["duration"] = 3.0;  // very short — died during preinfusion-start
        shot["doseWeight"] = 18.0;
        shot["finalWeight"] = 0.5;

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
        ShotSummary summary = summarizer.summarizeFromHistory(shot);

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

    // Sanity: a healthy shot (peak pressure ~9 bar) flows through the same
    // path but pourTruncatedDetected stays false and the cascade does not
    // suppress observations. This guards against an over-aggressive gate.
    void healthyShotKeepsObservationsAndDoesNotTruncate()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["duration"] = 30.0;
        shot["doseWeight"] = 18.0;
        shot["finalWeight"] = 36.0;

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
        ShotSummary summary = summarizer.summarizeFromHistory(shot);

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
        shot["duration"] = 30.0;
        shot["doseWeight"] = 18.0;
        shot["finalWeight"] = 36.0;
        shot["yieldOverride"] = 36.0;

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
        const ShotSummary summary = summarizer.summarizeFromHistory(shot);

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
        const ShotSummary summary = summarizer.summarizeFromHistory(shot);

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
        const ShotSummary slowSummary = summarizer.summarizeFromHistory(slowShot);

        // Now build a fast-path shot by stuffing the slow-path's lines and
        // pourTruncated into a fresh map. summarizeFromHistory MUST produce
        // an equivalent summary.
        QVariantMap fastShot = buildHealthyShotMap();
        fastShot["summaryLines"] = slowSummary.summaryLines;
        QVariantMap detectors;
        detectors["pourTruncated"] = slowSummary.pourTruncatedDetected;
        fastShot["detectorResults"] = detectors;
        const ShotSummary fastSummary = summarizer.summarizeFromHistory(fastShot);

        QCOMPARE(fastSummary.summaryLines.size(), slowSummary.summaryLines.size());
        for (qsizetype i = 0; i < slowSummary.summaryLines.size(); ++i) {
            QCOMPARE(fastSummary.summaryLines[i].toMap().value("text").toString(),
                     slowSummary.summaryLines[i].toMap().value("text").toString());
            QCOMPARE(fastSummary.summaryLines[i].toMap().value("type").toString(),
                     slowSummary.summaryLines[i].toMap().value("type").toString());
        }
        QCOMPARE(fastSummary.pourTruncatedDetected, slowSummary.pourTruncatedDetected);
    }

    // Cascade integrity through the fast path: when shotData carries a
    // detectorResults.pourTruncated == true, summarizeFromHistory MUST set
    // summary.pourTruncatedDetected = true AND skip the per-phase temp
    // instability marking, exactly like the slow path's cascade.
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
    // real ShotDataModel rather than a QVariantMap.
    void summarize_pourTruncated_suppressesChannelingAndTempLines_live()
    {
        ShotDataModel model;
        std::vector<LiveSample> samples;
        for (double t = 0.0; t <= 30.0 + 1e-9; t += 0.1) {
            samples.push_back({
                /*t=*/t, /*pressure=*/1.0, /*flow=*/1.5,
                /*temperature=*/88.0, /*pressureGoal=*/0.0, /*flowGoal=*/0.0,
                /*temperatureGoal=*/93.0, /*isFlowMode=*/false});
        }
        populateLiveShot(&model, samples,
            {{0.0, QStringLiteral("Preinfusion"), 0, true},
             {8.0, QStringLiteral("Pour"), 1, false}});

        ShotMetadata metadata;
        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarize(&model, /*profile=*/nullptr,
            metadata, /*doseWeight=*/18.0, /*finalWeight=*/36.0);

        QVERIFY2(summary.pourTruncatedDetected,
                 "live-path puck-failure shape must set pourTruncatedDetected");
        QVERIFY2(linesContain(summary.summaryLines, QStringLiteral("Pour never pressurized")),
                 "live-path summaryLines must contain the puck-failed warning");
        QVERIFY2(!linesContain(summary.summaryLines, QStringLiteral("Sustained channeling")),
                 "live-path channeling line must be suppressed by the cascade");
        QVERIFY2(!linesContain(summary.summaryLines, QStringLiteral("Temperature drifted")),
                 "live-path temperature drift line must be suppressed by the cascade");
    }

    // Live-path aborted-preinfusion: pin the reachedExtractionPhase gate on
    // the live path. Mirrors abortedPreinfusionDoesNotFlagPerPhaseTemp.
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
                /*temperatureGoal=*/93.0, /*isFlowMode=*/true});
        }
        // Frame 0 only — reachedExtractionPhase must return false.
        populateLiveShot(&model, samples,
            {{0.0, QStringLiteral("Preinfusion"), 0, true}});

        ShotMetadata metadata;
        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarize(&model, /*profile=*/nullptr,
            metadata, /*doseWeight=*/18.0, /*finalWeight=*/0.5);

        QVERIFY2(!summary.pourTruncatedDetected,
                 "test setup: 4-bar peak should not trip pourTruncated");
        for (const PhaseSummary& phase : summary.phases) {
            QVERIFY2(!phase.temperatureUnstable,
                     "live-path per-phase temp markers must stay false on aborted-preinfusion shots");
        }
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
             {8.0, QStringLiteral("Pour"), 1, false}});

        ShotMetadata metadata;
        ShotSummarizer summarizer;
        const ShotSummary summary = summarizer.summarize(&model, /*profile=*/nullptr,
            metadata, /*doseWeight=*/18.0, /*finalWeight=*/36.0);

        QVERIFY2(!summary.pourTruncatedDetected,
                 "healthy 9-bar shot must not be flagged as puck-failure on the live path");
        QVERIFY2(!linesContain(summary.summaryLines, QStringLiteral("Pour never pressurized")),
                 "live-path puck-failed warning must be absent on a healthy shot");
        QVERIFY2(linesContainType(summary.summaryLines, QStringLiteral("verdict")),
                 "every live-path shot must end with a verdict line");
    }

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
        const ShotSummary summary = summarizer.summarizeFromHistory(shot);

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
        shot["duration"] = 30.0;
        shot["doseWeight"] = 18.0;
        shot["finalWeight"] = 36.0;

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
        const ShotSummary summary = summarizer.summarizeFromHistory(shot);

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
        shot["duration"] = 30.0;
        shot["doseWeight"] = 18.0;
        shot["finalWeight"] = 36.0;

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
        const ShotSummary summary = summarizer.summarizeFromHistory(shot);

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
};

QTEST_GUILESS_MAIN(tst_ShotSummarizer)

#include "tst_shotsummarizer.moc"
