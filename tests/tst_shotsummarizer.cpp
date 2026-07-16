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
// frameNumber=1 so the marker indicates a real extraction frame.
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
    void init() { QTest::failOnWarning(); }

    // add-ai-taste-intake: a tapped taste axis counts as tasting feedback.
    // tastingFeedback.hasTasteAxis must be true and carry the tapped values, and
    // the prose must surface the taste — so the advisor reasons on it instead of
    // opening by asking "how did it taste?".
    void tasteAxis_countsAsTastingFeedbackAndRendersInProse()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;
        shot["profileName"] = QStringLiteral("80's Espresso");
        shot["tasteBalance"] = QStringLiteral("sour");
        shot["tasteBody"] = QStringLiteral("thin");
        // No enjoyment / notes — taste taps are the ONLY feedback signal here.

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
        const QJsonObject payload = QJsonDocument::fromJson(prompt.toUtf8()).object();

        const QJsonObject tf = payload.value(QStringLiteral("tastingFeedback")).toObject();
        QVERIFY2(tf.value(QStringLiteral("hasTasteAxis")).toBool(),
                 "a tapped taste axis must set tastingFeedback.hasTasteAxis");
        QCOMPARE(tf.value(QStringLiteral("tasteBalance")).toString(), QStringLiteral("sour"));
        QCOMPARE(tf.value(QStringLiteral("tasteBody")).toString(), QStringLiteral("thin"));

        const QString prose = payload.value(QStringLiteral("shotAnalysis")).toString();
        QVERIFY2(prose.contains(QStringLiteral("sour")),
                 "taste must surface in the shotAnalysis prose");
        QVERIFY2(!prose.contains(QStringLiteral("No tasting feedback provided")),
                 "a tapped taste axis must not read as 'no tasting feedback'");
    }
    // Puck-failure shape: peak pressure ~1.0 bar across the entire pour
    // window. Without the cascade, dC/dt and temp detectors on
    // ShotSummarizer's old code path would have read off the (nonexistent)
    // pour curves and emitted observations the AI would treat as gospel.
    // generateSummary's cascade now forces channeling/temp/grind to silence
    // and emits only the "Pour never pressurized" warning + the "Don't tune
    // off this shot" verdict.
    void pourTruncatedSuppressesChannelingLines()
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
        QVERIFY2(!prompt.contains(QStringLiteral("Sustained channeling")),
                 "channeling line must not reach the prompt on a truncated pour");
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
    // Integration coverage for openspec change
    // skip-grind-arm1-when-kb-unresolved: the gate inside analyzeFlowVsGoal
    // is unit-tested in tst_shotanalysis, but the bit that drives it —
    // `!summary.profileKbId.isEmpty()` derived inside runShotAnalysisAndPopulate —
    // would silently invert on a missing `!` and slip past the algorithm
    // tests (they pass profileKbResolved directly as an argument). This
    // test pins that wire-up. Same curves, two profileKbId values, asserts
    // opposite grindCoverage projections.
    //
    // Shot shape: flow-mode pour with actual=0.8 vs goal=1.7 → delta=-0.9,
    // well past the 0.4 trigger. Pressure 3.5 bar throughout sits above
    // pourTruncated's 2.5 floor (cascade dormant) AND below Arm 2's 4-bar
    // gate (Arm 2 silent on both runs), so Arm 1 is the only thing the
    // gate can flip. Yield ratio 30/36 = 0.83 keeps the yield-shortfall
    // arm silent too. With Arm 2 silent, Arm 1 skipped ⇒ "notAnalyzable",
    // Arm 1 ran ⇒ "verified" (delta past threshold ⇒ grindIssue fires).
    void summarizeFromHistory_profileKbResolvedThreadsToGate()
    {
        auto buildArm1WouldFireShot = []() {
            QVariantMap shot;
            shot["beverageType"] = QStringLiteral("espresso");
            shot["durationSec"] = 30.0;
            shot["doseWeightG"] = 18.0;
            shot["finalWeightG"] = 30.0;
            shot["targetWeightG"] = 36.0;

            QVariantList pressure, flow, flowGoal, temperature, temperatureGoal,
                         derivative, weight;
            appendFlat(pressure, 0.0, 30.0, 3.5);
            appendFlat(flow, 0.0, 30.0, 0.8);
            appendFlat(flowGoal, 0.0, 30.0, 1.7);
            appendFlat(temperature, 0.0, 30.0, 92.0);
            appendFlat(temperatureGoal, 0.0, 30.0, 92.0);
            appendFlat(derivative, 0.0, 30.0, 0.0);
            appendFlat(weight, 0.0, 30.0, 30.0);

            QVariantList phases;
            appendPhase(phases, 0.0,  QStringLiteral("preinfusion"), 0,
                        /*isFlowMode=*/true);
            appendPhase(phases, 10.0, QStringLiteral("pour"), 1,
                        /*isFlowMode=*/true);

            shot["pressure"] = pressure;
            shot["flow"] = flow;
            shot["flowGoal"] = flowGoal;
            shot["pressureGoal"] = QVariantList();
            shot["temperature"] = temperature;
            shot["temperatureGoal"] = temperatureGoal;
            shot["conductanceDerivative"] = derivative;
            shot["weight"] = weight;
            shot["phases"] = phases;
            return shot;
        };

        ShotSummarizer summarizer;

        // Resolved: profileKbId points at a real KB entry. Arm 1 runs,
        // delta hits threshold, grindIssue fires, coverage is "verified".
        QVariantMap resolvedShot = buildArm1WouldFireShot();
        resolvedShot["profileName"] = QStringLiteral("Adaptive v2");
        resolvedShot["profileKbId"] = QStringLiteral("adaptive-v2");
        const ShotSummary resolved = summarizer.summarizeFromHistory(
            ShotProjection::fromVariantMap(resolvedShot));
        bool resolvedSawNotAnalyzable = false;
        for (const QVariant& v : resolved.summaryLines) {
            const QVariantMap m = v.toMap();
            if (m["text"].toString().contains(
                    QStringLiteral("Could not analyze grind"),
                    Qt::CaseInsensitive))
                resolvedSawNotAnalyzable = true;
        }
        QVERIFY2(!resolvedSawNotAnalyzable,
                 "resolved profile must NOT emit the notAnalyzable observation");

        // Unresolved: profileKbId is empty. Arm 1 is skipped; with Arm 2
        // also silent, coverage falls into "notAnalyzable" and the
        // [observation] line + alternate verdict fire.
        QVariantMap unresolvedShot = buildArm1WouldFireShot();
        unresolvedShot["profileName"] = QStringLiteral("Jeff's Custom Profile 47");
        unresolvedShot["profileKbId"] = QString();
        const ShotSummary unresolved = summarizer.summarizeFromHistory(
            ShotProjection::fromVariantMap(unresolvedShot));
        bool unresolvedSawNotAnalyzable = false;
        bool unresolvedSawAlternateVerdict = false;
        for (const QVariant& v : unresolved.summaryLines) {
            const QVariantMap m = v.toMap();
            const QString text = m["text"].toString();
            if (m["type"].toString() == QStringLiteral("observation")
                && text.contains(QStringLiteral("Could not analyze grind"),
                                 Qt::CaseInsensitive))
                unresolvedSawNotAnalyzable = true;
            if (m["type"].toString() == QStringLiteral("verdict")
                && text.contains(QStringLiteral("grind could not be evaluated"),
                                 Qt::CaseInsensitive))
                unresolvedSawAlternateVerdict = true;
        }
        QVERIFY2(unresolvedSawNotAnalyzable,
                 "unresolved profile must emit the [observation] notAnalyzable line");
        QVERIFY2(unresolvedSawAlternateVerdict,
                 "unresolved profile must emit the alternate verdict");
    }

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

    // Cascade integrity through the fast path: when shotData carries a
    // detectorResults.pourTruncated == true, summarizeFromHistory MUST set
    // summary.pourTruncatedDetected = true.
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

    // fix-multishot-advice-tracking, task 5.4: the strengthened multi-shot
    // taste-feedback gate (2+ consecutive untasted shots) must ship
    // alongside — not replace — the existing single-shot tastingFeedback
    // rule pinned by the test above.
    void shotAnalysisSystemPrompt_carriesRepeatedUntastedShotsGate()
    {
        const QString prompt = ShotSummarizer::shotAnalysisSystemPrompt(
            QStringLiteral("espresso"), QStringLiteral("80's Espresso"),
            QString(), QString());
        QVERIFY2(prompt.contains(QStringLiteral("Repeated untasted shots")),
                 "system prompt must teach the repeated-untasted-shots gate");
        QVERIFY2(prompt.contains(QStringLiteral("LAST 2 OR MORE shots")),
                 "system prompt must state the 2+ consecutive shot threshold");
        QVERIFY2(prompt.contains(QStringLiteral("preliminary")),
                 "system prompt must frame curve-only observations as preliminary pending taste");
        // Still present, not replaced.
        QVERIFY2(prompt.contains(QStringLiteral("ASK the user")),
                 "single-shot tastingFeedback rule must still be present");
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

    // Issue #1459: the "Current Profile Knowledge" section used to inject
    // the matched KB entry's prose with no name attached, so the model
    // sometimes attributed a DIFFERENT catalog profile's name to the
    // shot's actual profile (e.g. calling a Rao Allongé shot "TurboTurbo"
    // because the two profiles' curated descriptions read similarly). The
    // section header must now name the matched entry explicitly, and the
    // resolved name must come from the SAME entry as the injected content.
    void shotAnalysisSystemPrompt_labelsCurrentProfileKnowledgeWithMatchedName()
    {
        const QString prompt = ShotSummarizer::shotAnalysisSystemPrompt(
            QStringLiteral("espresso"), QStringLiteral("Rao Allongé"),
            QString(), QString());
        QVERIFY2(prompt.contains(QStringLiteral("## Current Profile Knowledge: Allonge")),
                 "section header must carry the matched KB entry's own display name");
        QVERIFY2(!prompt.contains(QStringLiteral("## Current Profile Knowledge: TurboTurbo")),
                 "an Allongé-matched shot's section must not be headed with an unrelated profile's name");
        // "TurboTurbo" legitimately appears elsewhere (the cross-profile
        // catalog lists every KB profile by name) — only the section
        // header identifying THIS shot's matched entry is under test here.

        // A custom/renamed title (bean-prefixed, no exact/prefix alias match)
        // must not silently fall back to an unlabeled or mismatched section —
        // it should simply carry no KB section, never someone else's name.
        const QString unmatchedPrompt = ShotSummarizer::shotAnalysisSystemPrompt(
            QStringLiteral("espresso"), QStringLiteral("Yirgacheffe G2 - My Custom Blend"),
            QString(), QString());
        QVERIFY2(!unmatchedPrompt.contains(QStringLiteral("## Current Profile Knowledge:")),
                 "an unresolvable custom title must not fabricate a KB name label");
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
            {"text", QStringLiteral("Sustained channeling detected in dC/dt")}});
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

    // -------------------------------------------------------------
    // shot-metadata-capture: profile catalog surfaces [family: <name>]
    // tags so the model can identify mechanically-equivalent profiles
    // at a glance. Verifies (a) at least one [family: ...] tag is
    // present, and (b) the family teaching + anti-hallucination rule
    // ship in the same prompt build.
    // -------------------------------------------------------------
    void shotAnalysisSystemPrompt_catalogContainsFamilyTags()
    {
        const QString prompt = ShotSummarizer::shotAnalysisSystemPrompt(
            QStringLiteral("Espresso"),
            QStringLiteral("D-Flow / Q"),
            QStringLiteral("flow"),
            QStringLiteral("d-flow"));

        QVERIFY2(prompt.contains(QStringLiteral("[family:")),
                 "Profile catalog must surface family tags");
        // The Londinium family is the canonical case the rule was added
        // for (recommending D-Flow → LRv2 isn't a meaningful change since
        // both are lever-decline). Confirm both render in the same family.
        QVERIFY2(prompt.contains(QStringLiteral("[family: lever-decline]")),
                 "Catalog must contain at least one [family: lever-decline] entry");
        QVERIFY2(prompt.contains(QStringLiteral("D-Flow")),
                 "Catalog must include D-Flow entry");
        QVERIFY2(prompt.contains(QStringLiteral("Londinium")),
                 "Catalog must include Londinium entry");

        // Profile families teaching + anti-hallucination rule + bean-
        // correction acknowledgement teaching must all ship together.
        QVERIFY2(prompt.contains(QStringLiteral("Profile families")),
                 "System prompt must teach the family rule when the catalog is present");
        QVERIFY2(prompt.contains(QStringLiteral("Other-profile parameter discipline")),
                 "System prompt must teach the anti-hallucination rule");
        QVERIFY2(prompt.contains(QStringLiteral("Conversational metadata corrections")),
                 "System prompt must teach the bean-correction acknowledgement rule");
    }

    // -------------------------------------------------------------
    // #1280: standalone shot block carries stoppedBy so the LLM has a
    // stop-reason anchor instead of inventing "you stopped manually"
    // when yieldG looks short. Allowlist mirrors dialing_blocks.cpp.
    // -------------------------------------------------------------
    void buildUserPrompt_shotBlock_carriesStoppedByWeight()
    {
        QVariantMap shot = makeHealthyShotMap();
        shot["stoppedBy"] = QStringLiteral("weight");
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary);
        const QJsonObject payload = QJsonDocument::fromJson(prompt.toUtf8()).object();
        const QJsonObject shotBlock = payload.value(QStringLiteral("shot")).toObject();
        QCOMPARE(shotBlock.value(QStringLiteral("stoppedBy")).toString(),
                 QStringLiteral("weight"));
    }

    void buildUserPrompt_shotBlock_carriesStoppedByManual()
    {
        QVariantMap shot = makeHealthyShotMap();
        shot["stoppedBy"] = QStringLiteral("manual");
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary);
        const QJsonObject shotBlock = QJsonDocument::fromJson(prompt.toUtf8()).object()
            .value(QStringLiteral("shot")).toObject();
        QCOMPARE(shotBlock.value(QStringLiteral("stoppedBy")).toString(),
                 QStringLiteral("manual"));
    }

    void buildUserPrompt_shotBlock_omitsStoppedByForProfileEnd()
    {
        // profileEnd is intentionally omitted from the allowlist — the
        // system prompt's "stoppedBy → real outcome or user choice?" rubric
        // documents that an ABSENT field means "ran to completion OR DE1
        // hardware button". Emitting "profileEnd" explicitly would conflict
        // with that absence semantics.
        QVariantMap shot = makeHealthyShotMap();
        shot["stoppedBy"] = QStringLiteral("profileEnd");
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary);
        const QJsonObject shotBlock = QJsonDocument::fromJson(prompt.toUtf8()).object()
            .value(QStringLiteral("shot")).toObject();
        QVERIFY2(!shotBlock.contains(QStringLiteral("stoppedBy")),
                 "profileEnd must NOT serialize to the standalone shot block — "
                 "the rubric documents absent-field semantics that conflict with it");
    }

    void buildUserPrompt_shotBlock_omitsStoppedByWhenEmpty()
    {
        QVariantMap shot = makeHealthyShotMap();
        // No stoppedBy key set — projection field defaults to empty string.
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary);
        const QJsonObject shotBlock = QJsonDocument::fromJson(prompt.toUtf8()).object()
            .value(QStringLiteral("shot")).toObject();
        QVERIFY2(!shotBlock.contains(QStringLiteral("stoppedBy")),
                 "empty stoppedBy must not emit the field");
    }

    void buildUserPrompt_shotBlock_carriesStoppedByVolume()
    {
        // The third allowlist case: SAV (stop-at-volume) shots. Without
        // a dedicated test, a future refactor that converts the three
        // string comparisons in buildShotBlock into a QSet<QString> and
        // accidentally lists only {manual, weight} would slip past the
        // existing two cases.
        QVariantMap shot = makeHealthyShotMap();
        shot["stoppedBy"] = QStringLiteral("volume");
        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        const QString prompt = summarizer.buildUserPrompt(summary);
        const QJsonObject shotBlock = QJsonDocument::fromJson(prompt.toUtf8()).object()
            .value(QStringLiteral("shot")).toObject();
        QCOMPARE(shotBlock.value(QStringLiteral("stoppedBy")).toString(),
                 QStringLiteral("volume"));
    }

};

QTEST_GUILESS_MAIN(tst_ShotSummarizer)

#include "tst_shotsummarizer.moc"
