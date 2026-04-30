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
// (lines only); the historical-shot path (post-#935) prefers the
// pre-computed shotData.summaryLines that ShotHistoryStorage::convertShotRecord
// populates from its own analyzeShot pass. Either way the suppression cascade
// is enforced in exactly one place. These tests pin the contract:
// pourTruncatedDetected fires on low-peak shots, channeling/temp lines are
// suppressed, the "Puck failed" warning + verdict reach the prompt, and a
// healthy shot still surfaces the normal observations.

#include <QtTest>

#include <QVariantMap>
#include <QVariantList>
#include <QString>

#include "ai/shotsummarizer.h"

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
};

QTEST_GUILESS_MAIN(tst_ShotSummarizer)

#include "tst_shotsummarizer.moc"
