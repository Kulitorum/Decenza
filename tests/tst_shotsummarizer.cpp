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
#include <QFileInfo>
#include <functional>

#include <QVariantMap>
#include <QVariantList>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QRegularExpression>
#include <QJsonParseError>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "ai/shotsummarizer.h"
#include "history/shotprojection.h"
#include "profile/profile.h"
#include "ai/profileshapeindex.h"
#include "history/shothistorystorage_internal.h"
#include "history/shothistorystorage.h"

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

    // ---------------------------------------------------------------------
    // Canonical string-encoded profile numerics
    // (openspec change align-profile-json-with-reaprime).
    //
    // A stored profile snapshot writes its scalars as STRINGS ("93.00",
    // "18.0") in the canonical DE1 v2 format, so every reader has to go
    // through the dual-tolerant profileJsonToDouble(). A raw
    // QJsonValue::toDouble() on a string returns 0.0 *silently*, and
    // buildCurrentProfileBlock() gates both fields on `> 0` — so the
    // failure mode is not a wrong number, it is the brew temperature and
    // the recommended dose vanishing from the advisor payload entirely
    // with nothing logged. These tests are the regression guard: revert
    // either read in summarizeFromHistory() to `.toDouble()` and the
    // string-encoded rows below fail on 0.0 / a missing key.
    // ---------------------------------------------------------------------

    // `tempLiteral` / `doseLiteral` are spliced in as raw JSON text, so a
    // caller passes `93.0` for the numeric encoding and `"93.00"` (quotes
    // included) for the canonical string encoding.
    static QString makeProfileJson(const QString& tempLiteral,
                                   const QString& doseLiteral,
                                   bool hasRecommendedDose = true)
    {
        return QStringLiteral(R"({
            "title": "Test Profile",
            "type": "advanced",
            "version": 2,
            "espresso_temperature": %1,
            "has_recommended_dose": %2,
            "recommended_dose": %3,
            "steps": [
                {"name":"preinfusion","temperature":93,"seconds":8,"flow":4.0,"transition":"fast","exit":{"type":"pressure","condition":"over","value":4.0}},
                {"name":"pour","temperature":93,"seconds":22,"pressure":9.0,"transition":"smooth"}
            ]
        })")
            .arg(tempLiteral,
                 hasRecommendedDose ? QStringLiteral("true") : QStringLiteral("false"),
                 doseLiteral);
    }

    void summarizeFromHistory_profileNumericsTolerateStringEncoding_data()
    {
        QTest::addColumn<QString>("tempLiteral");
        QTest::addColumn<QString>("doseLiteral");
        // The pre-change encoding: bare JSON numbers. Still has to work —
        // every shot saved before this change carries this shape.
        QTest::newRow("numeric")        << QStringLiteral("93.0")
                                        << QStringLiteral("18.0");
        // The canonical encoding produced by Profile::toJsonObject().
        QTest::newRow("string-decimal") << QStringLiteral("\"93.00\"")
                                        << QStringLiteral("\"18.0\"");
        // Integer-looking strings — de1app writes these for whole values.
        QTest::newRow("string-integer") << QStringLiteral("\"93\"")
                                        << QStringLiteral("\"18\"");
    }

    void summarizeFromHistory_profileNumericsTolerateStringEncoding()
    {
        QFETCH(QString, tempLiteral);
        QFETCH(QString, doseLiteral);

        QVariantMap shot = makeHealthyShotMap();
        shot["profileJson"] = makeProfileJson(tempLiteral, doseLiteral);

        // Pin the hazard this test exists to guard, so the guard's purpose
        // survives without reading summarizeFromHistory(): on the string
        // rows the raw QJsonValue::toDouble() the reads must NOT use yields
        // 0.0, which is indistinguishable from "unset" downstream.
        const QJsonObject rawProfile =
            QJsonDocument::fromJson(shot["profileJson"].toString().toUtf8()).object();
        if (rawProfile.value(QStringLiteral("espresso_temperature")).isString()) {
            QCOMPARE(rawProfile.value(QStringLiteral("espresso_temperature")).toDouble(), 0.0);
            QCOMPARE(rawProfile.value(QStringLiteral("recommended_dose")).toDouble(), 0.0);
        }

        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QCOMPARE(summary.targetTemperatureC, 93.0);
        QCOMPARE(summary.recommendedDoseG, 18.0);
    }

    // End-to-end counterpart: the two encodings must render a byte-identical
    // `profile` block. Asserting key PRESENCE separately matters because the
    // `> 0` gate turns a bad read into an absent field, and a test that only
    // compared values would read `0 == 0` as agreement if both encodings
    // regressed together.
    void buildUserPrompt_profileBlockIdenticalAcrossNumericEncodings()
    {
        QVariantMap numericShot = makeHealthyShotMap();
        numericShot["profileJson"] =
            makeProfileJson(QStringLiteral("93.0"), QStringLiteral("18.0"));
        QVariantMap stringShot = makeHealthyShotMap();
        stringShot["profileJson"] =
            makeProfileJson(QStringLiteral("\"93.00\""), QStringLiteral("\"18.0\""));

        ShotSummarizer summarizer;
        auto profileBlockFor = [&summarizer](const QVariantMap& shot) {
            const ShotSummary s =
                summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));
            return QJsonDocument::fromJson(summarizer.buildUserPrompt(s).toUtf8())
                .object().value(QStringLiteral("profile")).toObject();
        };

        const QJsonObject numericProfile = profileBlockFor(numericShot);
        const QJsonObject stringProfile  = profileBlockFor(stringShot);

        QVERIFY2(stringProfile.contains(QStringLiteral("targetTemperatureC")),
                 "string-encoded espresso_temperature must still reach the profile block");
        QVERIFY2(stringProfile.contains(QStringLiteral("recommendedDoseG")),
                 "string-encoded recommended_dose must still reach the profile block");
        QCOMPARE(stringProfile.value(QStringLiteral("targetTemperatureC")).toDouble(), 93.0);
        QCOMPARE(stringProfile.value(QStringLiteral("recommendedDoseG")).toDouble(), 18.0);
        QCOMPARE(stringProfile, numericProfile);
    }

    // The per-pull temperature override wins over the profile's scalar, and
    // the string encoding must not change that precedence — a reader that
    // zeroed the profile value would make this test pass for the wrong
    // reason, which is why the companion test above pins the non-override
    // value too.
    void summarizeFromHistory_temperatureOverrideBeatsProfileTemperature()
    {
        QVariantMap shot = makeHealthyShotMap();
        shot["profileJson"] =
            makeProfileJson(QStringLiteral("\"93.00\""), QStringLiteral("\"18.0\""));
        shot["temperatureOverrideC"] = 95.5;

        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QCOMPARE(summary.targetTemperatureC, 95.5);
        // The dose is not overridable, so it still comes from the profile.
        QCOMPARE(summary.recommendedDoseG, 18.0);
    }

    // has_recommended_dose is the opt-in flag: a profile carrying a
    // recommended_dose value with the flag off must NOT publish a dose, or
    // the advisor would treat Profile::fromJson's 18 g default as an
    // author's recommendation.
    void summarizeFromHistory_recommendedDoseSkippedWhenFlagIsFalse()
    {
        QVariantMap shot = makeHealthyShotMap();
        shot["profileJson"] = makeProfileJson(QStringLiteral("\"93.00\""),
                                              QStringLiteral("\"18.0\""),
                                              /*hasRecommendedDose=*/false);

        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QCOMPARE(summary.recommendedDoseG, 0.0);
        // The temperature is unconditional and must still come through.
        QCOMPARE(summary.targetTemperatureC, 93.0);

        const QJsonObject profile =
            QJsonDocument::fromJson(summarizer.buildUserPrompt(summary).toUtf8())
                .object().value(QStringLiteral("profile")).toObject();
        QVERIFY2(!profile.contains(QStringLiteral("recommendedDoseG")),
                 "an unset recommended dose must not be emitted at all");
    }

    // A profile snapshot with no espresso_temperature key at all (older
    // exports, and visualizer's /profile?format=json) leaves the target
    // unset rather than defaulting to something plausible-looking.
    void summarizeFromHistory_absentTemperatureLeavesTargetUnset()
    {
        QVariantMap shot = makeHealthyShotMap();
        shot["profileJson"] = QStringLiteral(R"({
            "title": "Test Profile",
            "type": "advanced",
            "version": 2,
            "steps": [
                {"name":"pour","temperature":93,"seconds":22,"pressure":9.0,"transition":"smooth"}
            ]
        })");

        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QCOMPARE(summary.targetTemperatureC, 0.0);
        const QJsonObject profile =
            QJsonDocument::fromJson(summarizer.buildUserPrompt(summary).toUtf8())
                .object().value(QStringLiteral("profile")).toObject();
        QVERIFY2(!profile.contains(QStringLiteral("targetTemperatureC")),
                 "an absent brew temperature must not be emitted at all");
    }

    // ---------------------------------------------------------------------
    // editorType derivation ladder (summarizeFromHistory → profileTypeDescription).
    //
    // Four independent sources are consulted in order — title prefix,
    // legacy is_recipe_mode + recipe.editorType, legacy_profile_type,
    // profile_type — and the result is a human-readable style sentence the
    // advisor prompt leans on. None of the rungs had coverage, so a
    // reordering or a dropped fallback was invisible.
    // ---------------------------------------------------------------------
    void summarizeFromHistory_editorTypeDerivation_data()
    {
        QTest::addColumn<QString>("profileJson");
        QTest::addColumn<QString>("expectedFragment");

        auto framed = [](const QString& extraFields) {
            return QStringLiteral(R"({
                %1
                "version": 2,
                "steps": [
                    {"name":"pour","temperature":93,"seconds":22,"pressure":9.0,"transition":"smooth"}
                ]
            })").arg(extraFields);
        };

        QTest::newRow("title D-Flow")
            << framed(QStringLiteral("\"title\": \"D-Flow / Q\","))
            << QStringLiteral("D-Flow (lever-style");
        // A leading '*' marks a modified profile; it must be stripped before
        // the prefix test or every edited D-Flow loses its style description.
        QTest::newRow("title starred D-Flow")
            << framed(QStringLiteral("\"title\": \"*D-Flow / Q\","))
            << QStringLiteral("D-Flow (lever-style");
        QTest::newRow("title A-Flow")
            << framed(QStringLiteral("\"title\": \"A-Flow / Default\","))
            << QStringLiteral("A-Flow (pressure ramp");
        // Legacy pre-PR#579 shape: is_recipe_mode + recipe.editorType.
        QTest::newRow("legacy recipe editorType")
            << framed(QStringLiteral(
                   "\"title\": \"Custom Blend\", \"is_recipe_mode\": true,"
                   " \"recipe\": {\"editorType\": \"pressure\"},"))
            << QStringLiteral("Pressure profile");
        QTest::newRow("legacy_profile_type 2a")
            << framed(QStringLiteral(
                   "\"title\": \"Custom Blend\", \"legacy_profile_type\": \"settings_2a\","))
            << QStringLiteral("Pressure profile");
        QTest::newRow("profile_type 2b")
            << framed(QStringLiteral(
                   "\"title\": \"Custom Blend\", \"profile_type\": \"settings_2b\","))
            << QStringLiteral("Flow profile");
        // Nothing resolves — no style sentence rather than a guessed one.
        QTest::newRow("unresolvable")
            << framed(QStringLiteral(
                   "\"title\": \"Custom Blend\", \"profile_type\": \"settings_2c\","))
            << QString();
    }

    void summarizeFromHistory_editorTypeDerivation()
    {
        QFETCH(QString, profileJson);
        QFETCH(QString, expectedFragment);

        QVariantMap shot = makeHealthyShotMap();
        shot["profileJson"] = profileJson;

        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        if (expectedFragment.isEmpty()) {
            QVERIFY2(summary.profileType.isEmpty(),
                     qPrintable(QStringLiteral("expected no style description, got: ")
                                + summary.profileType));
        } else {
            QVERIFY2(summary.profileType.contains(expectedFragment),
                     qPrintable(QStringLiteral("expected '%1' in '%2'")
                                .arg(expectedFragment, summary.profileType)));
        }
    }

    // ---------------------------------------------------------------------
    // Whole-shot phase fallback (makeWholeShotPhase).
    //
    // Legacy shots and shots aborted before frame 0 emitted carry no phase
    // markers. Rather than hand callers an empty phase list to special-case,
    // summarizeFromHistory synthesizes one "Extraction" phase spanning the
    // shot. Untested until now, so a fallback that silently stopped firing
    // would only show up as an empty ## Phase Data section in a prompt.
    // ---------------------------------------------------------------------
    void summarizeFromHistory_noPhaseMarkersSynthesizesWholeShotPhase()
    {
        QVariantMap shot;
        shot["beverageType"] = QStringLiteral("espresso");
        shot["durationSec"] = 30.0;
        shot["doseWeightG"] = 18.0;
        shot["finalWeightG"] = 36.0;
        shot["targetWeightG"] = 36.0;

        QVariantList pressure, flow, temperature, temperatureGoal, derivative, weight;
        appendFlat(pressure, 0.0, 30.0, 9.0);
        appendFlat(flow, 0.0, 30.0, 1.8);
        appendFlat(temperature, 0.0, 30.0, 92.0);
        appendFlat(temperatureGoal, 0.0, 30.0, 92.0);
        appendFlat(derivative, 0.0, 30.0, 0.0);
        for (double t = 0.0; t <= 30.0 + 1e-9; t += 0.1) {
            QVariantMap p; p["x"] = t; p["y"] = 36.0 * (t / 30.0);
            weight.append(p);
        }

        shot["pressure"] = pressure;
        shot["flow"] = flow;
        shot["temperature"] = temperature;
        shot["temperatureGoal"] = temperatureGoal;
        shot["conductanceDerivative"] = derivative;
        shot["weight"] = weight;
        shot["phases"] = QVariantList();   // no markers at all
        shot["pressureGoal"] = QVariantList();
        shot["flowGoal"] = QVariantList();

        ShotSummarizer summarizer;
        const ShotSummary summary =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(shot));

        QCOMPARE(summary.phases.size(), 1);
        const PhaseSummary& phase = summary.phases.first();
        QCOMPARE(phase.name, QStringLiteral("Extraction"));
        QCOMPARE(phase.startTime, 0.0);
        QCOMPARE(phase.endTime, 30.0);
        QCOMPARE(phase.duration, 30.0);
        QCOMPARE(phase.avgPressure, 9.0);
        QCOMPARE(phase.avgFlow, 1.8);
        QCOMPARE(phase.avgTemperature, 92.0);
        QVERIFY(qFuzzyCompare(phase.weightGained, 36.0));
    }

    // Ratio is the one derived overall metric, and its divide-by-zero guard
    // matters: a dose-less shot (tea, or a shot saved before the dose was
    // entered) must report 0 rather than an inf that renders as "inf" in the
    // prompt. Debug builds run UBSan with float-divide-by-zero enabled, so an
    // unguarded divide would abort here rather than just look wrong.
    void summarizeFromHistory_ratioGuardsAgainstZeroDose()
    {
        QVariantMap withDose = makeHealthyShotMap();
        ShotSummarizer summarizer;
        const ShotSummary dosed =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(withDose));
        QVERIFY(qFuzzyCompare(dosed.ratio, 2.0));   // 36 g / 18 g

        QVariantMap noDose = makeHealthyShotMap();
        noDose["doseWeightG"] = 0.0;
        const ShotSummary undosed =
            summarizer.summarizeFromHistory(ShotProjection::fromVariantMap(noDose));
        QCOMPARE(undosed.ratio, 0.0);
    }

    // --- Recipe-alias boundary rule (change: resolve-profile-kb-by-shape) ---
    //
    // These are IDENTITY assertions, deliberately secondary to the corpus
    // fixture blooming_choker_renamed_profile.json, which asserts the same
    // regression as an OUTCOME (a shot that must not be told it channeled).
    // Identity is asserted too because it fails faster and names the cause
    // directly, rather than reporting a resolution break through a downstream
    // finding.
    //
    // Live case: a user's "Best practice (light roast)_cris" resolved to
    // nothing because '_' was absent from the old enumerated separator set
    // (/ - space ASCII-digit), so every shot on it lost that entry's
    // flow_trend_ok AND channeling_expected and was eligible for two
    // false-positive findings. The rule is now the complement — a boundary is
    // any character that is NOT a letter — because a letter is the only case
    // the rule actually turns on.
    void recipePrefix_nonLetterBoundaryResolvesRenamedProfiles_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("expectedId");

        // The live defect, pinned by name.
        QTest::newRow("underscore suffix (the #cris case)")
            << QStringLiteral("Best practice (light roast)_cris")
            << QStringLiteral("best-practice-light-roast");
        // Punctuation the old enumeration would equally have missed. Listed
        // not because these were reported but because the enumeration's
        // failure mode was "whatever the author did not think of".
        QTest::newRow("dot suffix")    << QStringLiteral("Londinium.v2")     << QStringLiteral("londinium");
        QTest::newRow("comma suffix")  << QStringLiteral("Londinium, decaf") << QStringLiteral("londinium");
        QTest::newRow("paren suffix")  << QStringLiteral("Londinium(decaf)") << QStringLiteral("londinium");
        // Separators the old set already admitted — unchanged behaviour, kept
        // so a future narrowing of the rule cannot pass silently.
        QTest::newRow("hyphen suffix") << QStringLiteral("Londinium - Jeff") << QStringLiteral("londinium");
        QTest::newRow("digit suffix")  << QStringLiteral("Londinium2")       << QStringLiteral("londinium");
    }

    void recipePrefix_nonLetterBoundaryResolvesRenamedProfiles()
    {
        QFETCH(QString, title);
        QFETCH(QString, expectedId);
        QCOMPARE(ShotSummarizer::computeProfileKbId(title), expectedId);
    }

    // The one case the rule exists to block. A following LETTER is not a
    // boundary, so a longer word that merely starts with an alias must not
    // inherit that alias's entry.
    void recipePrefix_followingLetterStillBlocks_data()
    {
        QTest::addColumn<QString>("title");

        QTest::newRow("longer word after alias") << QStringLiteral("D-Flow / Quark");
        QTest::newRow("alias run into a letter") << QStringLiteral("D-FlowX");
        QTest::newRow("longer word, no editor")  << QStringLiteral("Londiniumesque");
        // isLetter() is Unicode-aware: a non-Latin letter blocks exactly as an
        // ASCII one does. The old enumeration admitted these as boundaries by
        // omission, which was never intended.
        QTest::newRow("cyrillic letter after alias") << QStringLiteral("Londiniumд");
        QTest::newRow("cjk letter after alias")      << QStringLiteral("Londinium一");
    }

    void recipePrefix_followingLetterStillBlocks()
    {
        QFETCH(QString, title);
        // No editor hint: the editor-type default (step 3) is a separate path
        // and would mask what this asserts about step 2.
        QVERIFY2(ShotSummarizer::computeProfileKbId(title).isEmpty(),
                 qPrintable(QStringLiteral("expected no resolution for ") + title
                            + QStringLiteral(", got ")
                            + ShotSummarizer::computeProfileKbId(title)));
    }

    // Longest-wins across the boundary. A renamed variant must inherit the
    // MOST specific recipe alias it extends, never a shorter one belonging to
    // a different entry — "D-Flow / Q - Jeff" is D-Flow/Q, not band-less
    // D-Flow/default. This can regress from the longest-first sort or the
    // loop's first-hit-wins shortcut, and nothing else in the suite covers it:
    // the spec names a `tst_kb_resolution` binary that has never existed.
    void recipePrefix_longestAliasWinsAcrossBoundary()
    {
        const QString qVariant = ShotSummarizer::computeProfileKbId(
            QStringLiteral("D-Flow / Q"), QStringLiteral("dflow"));
        const QString dflowDefault = ShotSummarizer::computeProfileKbId(
            QStringLiteral("D-Flow"), QStringLiteral("dflow"));
        QVERIFY2(!qVariant.isEmpty() && qVariant != dflowDefault,
                 "fixture precondition: D-Flow / Q must be its own entry");

        // Each of these extends the longer alias across a boundary.
        for (const QString& t : { QStringLiteral("D-Flow / Q - Jeff"),
                                  QStringLiteral("D-Flow / Q2"),
                                  QStringLiteral("D-Flow / Q_cris") }) {
            QCOMPARE(ShotSummarizer::computeProfileKbId(t, QStringLiteral("dflow")), qVariant);
        }
    }

    // The shipped profile set as a MAINTAINER edits it, read from the source
    // tree via DECENZA_SOURCE_DIR (defined for every test target — see
    // add_decenza_test in tests/CMakeLists.txt).
    //
    // This binary now links profiles.qrc too — ProfileShapeIndex reads
    // `:/profiles`, so the 494 KB an earlier comment here argued against is
    // already compiled in. The slots below still read the source tree anyway:
    // they assert against the files a maintainer edits, so a profile renamed on
    // disk fails here even if the qrc list was not updated in the same commit.
    //
    // One definition, because six slots reach for this directory and a path
    // written out six times is six chances for one of them to point elsewhere.
    static QDir shippedProfileDir()
    {
        return QDir(QStringLiteral(DECENZA_SOURCE_DIR "/resources/profiles"));
    }

    // Corpus coverage: every shipped profile resolves to a KB entry. It can
    // genuinely fail — a shipped profile renamed without its KB alias being
    // updated drops to unresolved, silently losing that entry's suppression
    // flags for every shot taken on it, the same defect class as the '_cris'
    // case by a different route.
    //
    // "to a KB entry", not "to exactly one": computeProfileKbId returns a
    // single QString, so multiplicity is impossible by the return type and the
    // old name (…ResolvesToExactlyOneEntry) promised an assertion the body
    // could not make. Uniqueness across the alias set is what
    // recipePrefix_longestAliasWinsAcrossBoundary covers.
    void everyShippedProfileResolvesToAKbEntry()
    {
        const QDir dir = shippedProfileDir();
        const QStringList files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files);
        QVERIFY2(files.size() > 50,
                 qPrintable(QStringLiteral("expected the shipped profile set, found %1 files")
                                .arg(files.size())));

        QStringList unresolved;
        for (const QString& name : files) {
            QFile f(dir.filePath(name));
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            const QString title = o.value(QStringLiteral("title")).toString();
            if (title.isEmpty()) continue;
            // A-Flow/D-Flow editor outputs reach their entry through the
            // editor-type default (step 3), so pass the hint the app passes.
            QString hint;
            if (title.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive))
                hint = QStringLiteral("dflow");
            else if (title.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))
                hint = QStringLiteral("aflow");
            if (ShotSummarizer::computeProfileKbId(title, hint).isEmpty())
                unresolved << title;
        }
        QVERIFY2(unresolved.isEmpty(),
                 qPrintable(QStringLiteral("shipped profiles resolving to no KB entry: ")
                                + unresolved.join(QStringLiteral(", "))));
    }

    // === Shape grouping over the shipped set ===
    // (change: resolve-profile-kb-by-shape, task group 2)
    //
    // Lives here rather than in tst_builtinprofileformat, whose subject is the
    // shipped profile set: these slots need the KB resource (:/ai) to resolve
    // ids, and this is the binary that links ai.qrc. Putting them there meant
    // "Failed to load profile knowledge resource" and four vacuous zeros.
    // (This binary links profiles.qrc as well, since ProfileShapeIndex reads
    // `:/profiles`; the slots below still read the FILES from the source tree
    // so they assert against what a maintainer edits.)
    //
    // Every figure in that change's design.md was first derived from a Python
    // proxy reading raw JSON fields. `Profile::fromJson` NORMALIZES — simple
    // profiles regenerate their frames, preinfuseFrameCount is derived rather
    // than read for non-advanced profiles — so the real grouping can differ
    // from the proxy's. These slots re-derive the numbers from the shipped C++
    // path and pin them.
    //
    // Why pinning matters beyond this change: a shipped profile edited so that
    // it collapses into another's shape bucket silently changes which KB facts
    // a user's look-alike profile inherits. That is invisible at review time
    // and has no other detector.

    // Group the shipped profiles by shape, keyed by signature, valued by the
    // set of KB ids the bucket's profiles resolve to.
    static QMap<QString, QSet<QString>> shippedShapeBuckets(bool dropSeconds = false)
    {
        QMap<QString, QSet<QString>> buckets;
        const QDir dir = shippedProfileDir();
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString& name : files) {
            QFile f(dir.filePath(name));
            if (!f.open(QIODevice::ReadOnly)) continue;
            const Profile p = Profile::fromJson(QJsonDocument::fromJson(f.readAll()));
            const QString kbId =
                ShotSummarizer::computeProfileKbId(p.title(), p.editorType());
            if (kbId.isEmpty()) continue;   // unmapped shipped profile: not index material
            QString sig = p.shapeSignature();
            if (sig.isEmpty()) continue;
            if (dropSeconds) {
                // Strip only the duration term, reusing the real signature
                // rather than reimplementing it — this is a measurement of the
                // alternative, not a second definition of shape.
                sig.remove(QRegularExpression(QStringLiteral("\\|s=[0-9.]+")));
            }
            buckets[sig].insert(kbId);
        }
        return buckets;
    }

    // Population smoke check: enough shipped profiles resolve to a KB id for
    // the index to be worth building at all. It does NOT assert that a profile
    // finds itself — that is shapeIndex_shippedProfileFindsItsOwnEntry below,
    // which is strictly stronger. Named accordingly, after the old name
    // (shippedProfilesMatchThemselvesByShape) was found to describe the
    // stronger test rather than this one.
    void shippedProfileSetIsLargeEnoughToIndex()
    {
        const QMap<QString, QSet<QString>> buckets = shippedShapeBuckets();
        int mapped = 0;
        for (const QSet<QString>& ids : buckets) mapped += ids.size();
        QVERIFY2(mapped >= 40,
                 qPrintable(QStringLiteral("only %1 shipped profiles mapped to a KB id — the "
                                           "index would be nearly empty").arg(mapped)));
        QVERIFY(!buckets.isEmpty());
    }

    // Pin the collision STRUCTURE by member name, not merely by count. A future
    // shipped-profile edit that collapses d-flow-q-variant into d-flow fails
    // here rather than silently widening a bucket.
    //
    // THREE. Design.md first measured a fourth, {damians-lr-v2-v3, londinium},
    // and it was never a collision of two profiles — it was one profile
    // carrying two KB entries. londonium.json and damian_s_lrv2.json are
    // byte-identical across all seven frames, differing only in title,
    // reference_file, notes, target_weight, the hidden flag and one frame
    // popup; Londonium's own notes say "This is identical to the LRv2
    // profile, but renamed to be easier to understand." The entries were
    // merged (LRv2 now resolves to `londinium`, which carries the cited
    // pressure-peak band it was always entitled to) and LRv3 — a genuinely
    // different profile, eight frames at 90C with a 9-bar hold — was split
    // out to `damians-lr-v2-v3`. So this bucket does not reappear by
    // widening the shape key; it disappeared because the KB stopped saying
    // one thing twice.
    //
    // {adaptive-v2, adaptive-v3} is the third, added when the de1app sync
    // brought in Adaptive v3 (best_practice.tcl, KB entry `adaptive-v3`).
    // Unlike LRv2/Londonium this is a real two-profile collision, not a
    // duplicate: adaptive_v2.json and adaptive_v3.json share the identical
    // 7-frame skeleton (Prefill/Fill/Compressing/Dripping/Pressurize/
    // Extraction start/Extraction, same pump mode and duration per frame) —
    // best-practice-light-roast's own prose says it "subsequently evolved
    // into the Adaptive profile", and v2/v3 are two points on that lineage —
    // but differ in setpoints (temperature, pressure/flow targets) and in
    // KB facts (v3 has no expertBand; v3 carries channeling_expected for its
    // near-zero-pressure Dripping step, v2 does not). Kept as a real bucket.
    void shippedShapeCollisionsAreExactlyTheKnownThree()
    {
        const QMap<QString, QSet<QString>> buckets = shippedShapeBuckets();

        QList<QStringList> collisions;
        for (const QSet<QString>& ids : buckets) {
            if (ids.size() < 2) continue;
            QStringList sorted(ids.begin(), ids.end());
            sorted.sort();
            collisions << sorted;
        }
        std::sort(collisions.begin(), collisions.end(),
                  [](const QStringList& a, const QStringList& b) {
                      return a.join(QLatin1Char(',')) < b.join(QLatin1Char(','));
                  });

        QList<QStringList> expected{
            {QStringLiteral("adaptive-v2"), QStringLiteral("adaptive-v3")},
            {QStringLiteral("d-flow"), QStringLiteral("d-flow-la-pavoni-variant")},
            {QStringLiteral("gentle-flat-long-preinfusion-family"),
             QStringLiteral("preinfuse-then-45ml-of-water")},
        };
        std::sort(expected.begin(), expected.end(),
                  [](const QStringList& a, const QStringList& b) {
                      return a.join(QLatin1Char(',')) < b.join(QLatin1Char(','));
                  });

        QStringList got, want;
        for (const QStringList& c : collisions) got << c.join(QLatin1Char('+'));
        for (const QStringList& c : expected)   want << c.join(QLatin1Char('+'));
        QCOMPARE(got.join(QStringLiteral(" | ")), want.join(QStringLiteral(" | ")));
    }

    // The evidence for keeping frame durations in the shape key. Dropping them
    // must measurably WIDEN the buckets — if it does not, the decision has no
    // support and design.md's 23-vs-6 figure is wrong for the real path.
    void droppingDurationsFromTheKeyWidensTheBuckets()
    {
        auto involved = [](const QMap<QString, QSet<QString>>& b) {
            int n = 0;
            for (const QSet<QString>& ids : b) if (ids.size() > 1) n += ids.size();
            return n;
        };
        const QMap<QString, QSet<QString>> withSeconds = shippedShapeBuckets(false);
        const QMap<QString, QSet<QString>> without     = shippedShapeBuckets(true);

        QVERIFY2(without.size() < withSeconds.size(),
                 qPrintable(QStringLiteral("signatures: with=%1 without=%2")
                                .arg(withSeconds.size()).arg(without.size())));
        QVERIFY2(involved(without) > involved(withSeconds) * 2,
                 qPrintable(QStringLiteral("profiles in colliding buckets: with=%1 without=%2 "
                                           "— the >2x widening is design.md's justification for "
                                           "keeping durations in the key")
                                .arg(involved(withSeconds)).arg(involved(without))));

        // The specific separation #1198 exists to protect: D-Flow/Q must not
        // fall into the same bucket as D-Flow/default.
        for (const QSet<QString>& ids : withSeconds) {
            if (ids.contains(QStringLiteral("d-flow-q-variant")))
                QVERIFY2(!ids.contains(QStringLiteral("d-flow")),
                         "D-Flow/Q collapsed into D-Flow/default");
        }
    }

    // Per-fact transfer: for each colliding bucket, do its members agree? This
    // is what decides whether a fact may transfer to a look-alike profile at
    // all. Pinned because the ANSWER drives the transfer rules (design D5/D5a):
    // suppression flags agree almost everywhere, expert bands and UGS do not.
    void collidingBucketsDisagreeOnAssertiveFactsButAgreeOnSuppression()
    {
        const QMap<QString, QSet<QString>> buckets = shippedShapeBuckets();
        int bucketsSeen = 0, flagDisagreements = 0, bandDisagreements = 0;

        for (const QSet<QString>& ids : buckets) {
            if (ids.size() < 2) continue;
            ++bucketsSeen;
            QSet<QString> flagSets, bandSets;
            for (const QString& id : ids) {
                QStringList fl = ShotSummarizer::getAnalysisFlags(id);
                fl.sort();
                flagSets.insert(fl.join(QLatin1Char(',')));
                const auto band = ShotSummarizer::expertBandForKbId(id);
                bandSets.insert(band ? QStringLiteral("band") : QStringLiteral("none"));
            }
            if (flagSets.size() > 1) ++flagDisagreements;
            if (bandSets.size() > 1) ++bandDisagreements;
        }

        QCOMPARE(bucketsSeen, 3);
        // Measured: two buckets disagree on flags (flow_trend_ok, the safe
        // direction the union rule handles); two disagree on the band, which
        // is why the band requires unanimity and is withheld otherwise.
        //
        // Was 2 buckets / 1 flag / 1 band disagreement before the de1app sync
        // added {adaptive-v2, adaptive-v3} (see
        // shippedShapeCollisionsAreExactlyTheKnownThree) — that bucket
        // disagrees on both: v3 carries channeling_expected and v2 does not,
        // and v2 has an expertBand while v3 (no citation strong enough to
        // pin one) does not.
        //
        // Before that, was 3 buckets / 2 band disagreements. The extra one,
        // {damians-lr-v2-v3, londinium}, was never two profiles — it was one
        // profile with two KB entries, and its "band disagreement" was the KB
        // describing the same extraction twice with different completeness.
        // Merging the entries removed a disagreement rather than resolving
        // one.
        QCOMPARE(flagDisagreements, 2);
        QCOMPARE(bandDisagreements, 2);
    }

    // === ProfileShapeIndex (change: resolve-profile-kb-by-shape, group 3) ===
    //
    // The index maps a shipped profile's shape to the KB ids that shape can
    // lend facts to. These assert the two properties a caller relies on:
    // a profile finds ITSELF (or the index is useless), and the answer does not
    // depend on the order the shipped directory happened to enumerate in.

    void shapeIndex_shippedProfileFindsItsOwnEntry_data()
    {
        QTest::addColumn<QString>("filePath");
        const QDir dir = shippedProfileDir();
        const QStringList files =
            dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        QVERIFY(files.size() > 50);
        for (const QString& f : files)
            QTest::newRow(qPrintable(f)) << dir.absoluteFilePath(f);
    }

    void shapeIndex_shippedProfileFindsItsOwnEntry()
    {
        QFETCH(QString, filePath);
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const Profile p = Profile::fromJson(QJsonDocument::fromJson(f.readAll()));

        const QString ownId = ShotSummarizer::computeProfileKbId(p.title(), p.editorType());
        if (ownId.isEmpty()) return;          // unmapped shipped profile: not index material
        if (p.shapeSignature().isEmpty()) return;  // no frames: nothing to match on

        const QStringList got = ProfileShapeIndex::candidatesForShape(p);
        QVERIFY2(got.contains(ownId),
                 qPrintable(QStringLiteral("%1 (%2) did not find itself; got [%3]")
                                .arg(p.title(), ownId, got.join(QStringLiteral(", ")))));

        // The bucket must also name the FILE, not only the entry. The dial-in
        // difference block compares against a bundled profile's values, so a
        // bucket that resolves an id but cannot say which file backs it leaves
        // that block with nothing to diff against. Basenames because the index
        // reads `:/profiles` while this test enumerates the source tree.
        QStringList bundledNames;
        for (const ProfileShapeIndex::BundledMatch& m :
             ProfileShapeIndex::bundledProfilesForShape(p))
            bundledNames << QFileInfo(m.resourcePath).fileName();
        QVERIFY2(bundledNames.contains(QFileInfo(filePath).fileName()),
                 qPrintable(QStringLiteral("%1 is absent from its own bucket's files; got [%2]")
                                .arg(QFileInfo(filePath).fileName(),
                                     bundledNames.join(QStringLiteral(", ")))));
    }

    // A candidate set that varied with enumeration order would be an
    // order-dependent RESOLUTION — the exact property the KB resolver's
    // standing prohibition on non-deterministic matching rules out.
    void shapeIndex_resultIsOrderIndependentAndStable()
    {
        // d_flow_default, deliberately: its bucket has TWO members, so the
        // sorted-order assertion below can actually fail. A singleton bucket
        // is trivially sorted and would make that check decorative — which is
        // what this test did when it used blooming_espresso.
        QFile f(shippedProfileDir().filePath(QStringLiteral("d_flow_default.json")));
        QVERIFY2(f.open(QIODevice::ReadOnly), "fixture profile missing");
        const Profile p = Profile::fromJson(QJsonDocument::fromJson(f.readAll()));

        const QStringList first = ProfileShapeIndex::candidatesForShape(p);
        QVERIFY2(first.size() > 1,
                 qPrintable(QStringLiteral("fixture precondition: d_flow_default must sit in a "
                                           "multi-member bucket, got [%1]")
                                .arg(first.join(QStringLiteral(", ")))));

        // Rebuild from scratch and compare. Same input, same answer, and the
        // list is sorted rather than insertion-ordered.
        ProfileShapeIndex::resetForTesting();
        const QStringList second = ProfileShapeIndex::candidatesForShape(p);
        QCOMPARE(second, first);

        QStringList sorted = first;
        sorted.sort();
        QCOMPARE(first, sorted);
    }

    // A profile with no frames must match nothing rather than everything —
    // an empty signature bucketing together would make every malformed
    // profile a relative of every other.
    void shapeIndex_framelessProfileMatchesNothing()
    {
        const Profile empty;
        QVERIFY(ProfileShapeIndex::candidatesForShape(empty).isEmpty());
    }

    // A bucket's FILES, on the smaller of the two real collisions. Sorted for
    // the same reason the id list is: a caller picks a base from this list, and
    // an enumeration-order-dependent list is an enumeration-order-dependent
    // attribution shown to the user.
    void shapeIndex_bundledFilesCoverTheWholeBucketAndAreSorted()
    {
        QFile f(shippedProfileDir().filePath(QStringLiteral("d_flow_default.json")));
        QVERIFY2(f.open(QIODevice::ReadOnly), "fixture profile missing");
        const Profile p = Profile::fromJson(QJsonDocument::fromJson(f.readAll()));

        const QVector<ProfileShapeIndex::BundledMatch> got =
            ProfileShapeIndex::bundledProfilesForShape(p);

        QStringList names, ids;
        for (const ProfileShapeIndex::BundledMatch& m : got) {
            names << QFileInfo(m.resourcePath).fileName();
            ids << m.kbId;
        }
        QCOMPARE(names, (QStringList{ QStringLiteral("d_flow_default.json"),
                                      QStringLiteral("d_flow_la_pavoni.json") }));
        QCOMPARE(ids, (QStringList{ QStringLiteral("d-flow"),
                                    QStringLiteral("d-flow-la-pavoni-variant") }));

    }

    // === Dial-in base selection (change: summarize-profile-changes-from-builtin) ===
    //
    // Fixtures are the REAL colliding bucket — hybrid_pour_over_espresso and
    // preinfuse_then_45ml_of_water — because a synthetic pair would not exercise
    // the index, and because both are `type: flow`, so a retitled copy misses
    // every title step (the editor-type default covers only dflow/aflow) and
    // genuinely reaches the shape step this feature is built on.

    // Load a shipped profile, edit its JSON, hand back the Profile.
    static Profile shippedProfileEdited(const QString& file,
                                        const std::function<void(QJsonObject&)>& edit)
    {
        QFile f(shippedProfileDir().filePath(file));
        if (!f.open(QIODevice::ReadOnly)) return Profile();
        QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        edit(obj);
        return Profile::fromJson(QJsonDocument(obj));
    }

    // Set one key on every step.
    static void setOnEveryStep(QJsonObject& obj, const QString& key, const QJsonValue& v)
    {
        QJsonArray steps = obj[QStringLiteral("steps")].toArray();
        for (int i = 0; i < steps.size(); ++i) {
            QJsonObject st = steps[i].toObject();
            st[key] = v;
            steps[i] = st;
        }
        obj[QStringLiteral("steps")] = steps;
    }

    static void setOnStep(QJsonObject& obj, int index, const QString& key, const QJsonValue& v)
    {
        QJsonArray steps = obj[QStringLiteral("steps")].toArray();
        QJsonObject st = steps[index].toObject();
        st[key] = v;
        steps[index] = st;
        obj[QStringLiteral("steps")] = steps;
    }

    // A re-tuned copy picks the profile it was copied FROM, not its bucket-mate.
    // This is the whole point of selection: the shape alone cannot tell them
    // apart, and the dial-in values can.
    void dialInBase_aRetunedCopyPicksTheProfileItCameFrom()
    {
        const Profile user = shippedProfileEdited(
            QStringLiteral("preinfuse_then_45ml_of_water.json"), [](QJsonObject& o) {
                o[QStringLiteral("title")] = QStringLiteral("Zzz My Own Thing");
                setOnEveryStep(o, QStringLiteral("temperature"), QStringLiteral("89.00"));
            });
        QVERIFY(user.isValid());

        const KbResolution res = resolveProfileKb(user);
        QCOMPARE(res.origin, KbResolution::Origin::Shape);
        QVERIFY2(res.ids.size() > 1, "fixture precondition: this shape must be ambiguous");

        const DialInComparison cmp = compareWithBundledBase(user, res);
        QVERIFY(cmp.hasBase());
        QCOMPARE(cmp.baseKbId, QStringLiteral("preinfuse-then-45ml-of-water"));
        QCOMPARE(cmp.deltas.size(), 1);
        QCOMPARE(cmp.deltas.first().kind, QStringLiteral("temperature"));
        // Collapsed: one edit, not three frames' worth.
        QCOMPARE(cmp.deltas.first().frameIndex, -1);
    }

    // Equidistant means no base. Naming one here would be a coin flip presented
    // to the user as a fact, which is the failure the whole gate exists to
    // prevent — the six fields below are exactly the ones on which the two
    // bundled profiles disagree, and this fixture differs from both on all six.
    void dialInBase_anEquidistantProfileGetsNoBase()
    {
        const Profile user = shippedProfileEdited(
            QStringLiteral("hybrid_pour_over_espresso.json"), [](QJsonObject& o) {
                o[QStringLiteral("title")] = QStringLiteral("Zzz Equidistant Fixture");
                o[QStringLiteral("target_volume")] = QStringLiteral("18.0");   // 0 vs 36
                // The AUTHORED brew temperature is its own dial-in row, separate
                // from the frames', so it has to differ from both too or the
                // fixture is not equidistant.
                o[QStringLiteral("espresso_temperature")] = QStringLiteral("95.00"); // 92 vs 90
                setOnEveryStep(o, QStringLiteral("temperature"), QStringLiteral("95.00")); // 92 vs 90
                setOnStep(o, 0, QStringLiteral("flow"), QStringLiteral("5.00"));  // 2.00 vs 8.00
                setOnStep(o, 1, QStringLiteral("flow"), QStringLiteral("2.50"));  // 2.20 vs 2.00
                setOnStep(o, 2, QStringLiteral("flow"), QStringLiteral("1.40"));  // 1.80 vs 1.00
                QJsonArray steps = o[QStringLiteral("steps")].toArray();
                QJsonObject f0 = steps[0].toObject();
                QJsonObject exit = f0[QStringLiteral("exit")].toObject();
                exit[QStringLiteral("value")] = QStringLiteral("2.50");           // 1.50 vs 4.00
                f0[QStringLiteral("exit")] = exit;
                steps[0] = f0;
                o[QStringLiteral("steps")] = steps;
            });
        QVERIFY(user.isValid());

        const KbResolution res = resolveProfileKb(user);
        QCOMPARE(res.origin, KbResolution::Origin::Shape);

        // Precondition: genuinely equidistant. Asserted rather than assumed —
        // if a bundled profile is retuned later this must fail loudly rather
        // than quietly stop testing the tie path.
        const Profile hybrid =
            Profile::loadFromFile(QStringLiteral(":/profiles/hybrid_pour_over_espresso.json"));
        const Profile preinf =
            Profile::loadFromFile(QStringLiteral(":/profiles/preinfuse_then_45ml_of_water.json"));
        QCOMPARE(Profile::dialInDeltas(hybrid, user).size(),
                 Profile::dialInDeltas(preinf, user).size());

        QVERIFY(!compareWithBundledBase(user, res).hasBase());
    }

    // The title path: an in-place edit of a bundled profile keeps its name, so it
    // never reaches the shape step — yet it is the larger population and must
    // still get its differences.
    void dialInBase_aTitleResolvedInPlaceEditStillGetsItsBase()
    {
        const Profile user = shippedProfileEdited(
            QStringLiteral("hybrid_pour_over_espresso.json"), [](QJsonObject& o) {
                setOnEveryStep(o, QStringLiteral("temperature"), QStringLiteral("94.00"));
            });
        QVERIFY(user.isValid());

        const KbResolution res = resolveProfileKb(user);
        QCOMPARE(res.origin, KbResolution::Origin::Title);

        const DialInComparison cmp = compareWithBundledBase(user, res);
        QVERIFY(cmp.hasBase());
        QCOMPARE(cmp.baseTitle, QStringLiteral("Hybrid pour over espresso"));
        QCOMPARE(cmp.deltas.size(), 1);
        QCOMPARE(cmp.deltas.first().kind, QStringLiteral("temperature"));
    }

    // A title match says nothing about frame structure. Diffing a differently
    // shaped profile against its namesake would render "frame 4 does not exist"
    // noise and falsely present it as a modified copy.
    void dialInBase_aTitleMatchOfADifferentShapeGetsNoBase()
    {
        const Profile user = shippedProfileEdited(
            QStringLiteral("hybrid_pour_over_espresso.json"), [](QJsonObject& o) {
                QJsonArray steps = o[QStringLiteral("steps")].toArray();
                steps.removeLast();                       // structural edit
                o[QStringLiteral("steps")] = steps;
            });
        QVERIFY(user.isValid());

        const KbResolution res = resolveProfileKb(user);
        QCOMPARE(res.origin, KbResolution::Origin::Title);
        QVERIFY(!compareWithBundledBase(user, res).hasBase());
    }

    // A bundled profile IS the documentation. "An unchanged copy of yourself" is
    // not a thing to tell anyone.
    void dialInBase_aBundledProfileComparedWithItselfGetsNoBase()
    {
        const Profile self =
            Profile::loadFromFile(QStringLiteral(":/profiles/hybrid_pour_over_espresso.json"));
        QVERIFY(self.isValid());
        QVERIFY(!compareWithBundledBase(self, resolveProfileKb(self)).hasBase());
    }

    // A renamed but otherwise untouched copy DOES get a base, with no deltas —
    // the "unchanged copy of X" case, which must be distinguishable from having
    // no base at all.
    void dialInBase_aRenamedUntouchedCopyHasABaseAndNoDeltas()
    {
        const Profile user = shippedProfileEdited(
            QStringLiteral("preinfuse_then_45ml_of_water.json"), [](QJsonObject& o) {
                o[QStringLiteral("title")] = QStringLiteral("Zzz Renamed Only");
            });
        QVERIFY(user.isValid());

        const DialInComparison cmp = compareWithBundledBase(user, resolveProfileKb(user));
        QVERIFY(cmp.hasBase());
        QCOMPARE(cmp.baseKbId, QStringLiteral("preinfuse-then-45ml-of-water"));
        QVERIFY(cmp.deltas.isEmpty());
    }

    // The Title branch restricts candidates to the entry whose prose is on
    // screen. Without that filter the block can name a bundled profile from a
    // DIFFERENT entry — "Your changes from D-Flow / La Pavoni" printed under the
    // D-Flow entry's text. The fixture is discriminating: it carries La Pavoni's
    // content under D-Flow's title, so deleting the filter elects the La Pavoni
    // file (zero deltas) instead.
    void dialInBase_theTitleBranchStaysInsideItsOwnEntry()
    {
        QFile f(shippedProfileDir().filePath(QStringLiteral("d_flow_la_pavoni.json")));
        QVERIFY2(f.open(QIODevice::ReadOnly), "fixture profile missing");
        QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        obj[QStringLiteral("title")] = QStringLiteral("D-Flow / default");
        const Profile user = Profile::fromJson(QJsonDocument(obj));
        QVERIFY(user.isValid());

        const KbResolution res = resolveProfileKb(user);
        QCOMPARE(res.origin, KbResolution::Origin::Title);
        QCOMPARE(res.ids, QStringList{ QStringLiteral("d-flow") });

        const DialInComparison cmp = compareWithBundledBase(user, res);
        QVERIFY(cmp.hasBase());
        QCOMPARE(cmp.baseKbId, QStringLiteral("d-flow"));
        QVERIFY2(!cmp.deltas.isEmpty(),
                 "the D-Flow base really should differ from La Pavoni's values");
    }

    // A profile-level-only edit. Profile::functionallyEqual deliberately ignores
    // the profile-level limits and never compares a frame name, so using it as
    // the self-check told a user who changed ONLY their yield that nothing had
    // changed — in the population this feature exists for.
    void dialInBase_anInPlaceYieldEditIsNotMistakenForTheBuiltIn()
    {
        const Profile user = shippedProfileEdited(
            QStringLiteral("hybrid_pour_over_espresso.json"), [](QJsonObject& o) {
                o[QStringLiteral("target_weight")] = QStringLiteral("42.0");
            });
        QVERIFY(user.isValid());

        const DialInComparison cmp = compareWithBundledBase(user, resolveProfileKb(user));
        QVERIFY(cmp.hasBase());
        QCOMPARE(cmp.deltas.size(), 1);
        QCOMPARE(cmp.deltas.first().kind, QStringLiteral("targetWeight"));
    }

    // Six bundled tea profiles share one shape bucket and one KB entry, so a
    // plain abstain-on-tie would exclude every tea profile from the feature.
    // The tie is answered only when answering it says something true: the tied
    // candidates must agree on the VALUES, not merely on how many fields
    // differ. Two of the six (chinese green, white tea) are identical on every
    // dial-in field, so a copy that is renamed and given ONE dial-in change
    // ties at that one row against both of them, with the same before and
    // after values either way — and the entry can be named without picking
    // one of the two.
    void dialInBase_aTieOnEquivalentValuesNamesTheEntry()
    {
        const Profile user = shippedProfileEdited(
            QStringLiteral("tea_portafilter_white_tea.json"), [](QJsonObject& o) {
                o[QStringLiteral("title")] = QStringLiteral("Zzz My Tea");
                o[QStringLiteral("target_weight")] = QStringLiteral("5.0");
            });
        QVERIFY(user.isValid());

        const DialInComparison cmp = compareWithBundledBase(user, resolveProfileKb(user));

        QVERIFY2(cmp.hasBase(),
                 "a tie whose candidates agree on the values must still produce a block");
        QCOMPARE(cmp.baseKbId, QStringLiteral("tea"));
        QCOMPARE(cmp.baseTitle, ShotSummarizer::canonicalNameForKbId(QStringLiteral("tea")));

        // The LABEL being right is not the same as the numbers being right: an
        // entry-level answer that kept an arbitrary candidate's deltas would
        // pass every assertion above. Both tied candidates ship 0 g, so the row
        // is true of the entry, which is what licenses naming it.
        QCOMPARE(cmp.deltas.size(), 1);
        QCOMPARE(cmp.deltas.first().kind, QStringLiteral("targetWeight"));
        QCOMPARE(cmp.deltas.first().oldValue, 0.0);
        QCOMPARE(cmp.deltas.first().newValue, 5.0);
    }

    // The other half of the rule, and the case that made it necessary. A tea at
    // a temperature none of the six ships differs from every one of them on the
    // same TWO fields, so the count cannot separate them — but each states a
    // different "before" temperature, so there is no single true column to
    // render. Naming the entry here would print one candidate's numbers under a
    // heading claiming they describe all of them.
    void dialInBase_aTieOnDifferentValuesInsideOneEntryStillAbstains()
    {
        const Profile user = shippedProfileEdited(
            QStringLiteral("tea_portafilter_white_tea.json"), [](QJsonObject& o) {
                o[QStringLiteral("title")] = QStringLiteral("Zzz My Tea");
                setOnEveryStep(o, QStringLiteral("temperature"), QStringLiteral("85.00"));
                o[QStringLiteral("espresso_temperature")] = QStringLiteral("85.00");
            });
        QVERIFY(user.isValid());

        // The tie itself, pinned here rather than asserted in a comment. Every
        // bucket member must produce the SAME number of rows (or there is no
        // tie to abstain over) while disagreeing on the values (or abstaining
        // would be wrong). Two comments in this change previously stated that
        // count from memory and both were wrong; this reads it from the code.
        const QVector<ProfileShapeIndex::BundledMatch> bucket =
            ProfileShapeIndex::bundledProfilesForShape(user);
        QCOMPARE(bucket.size(), 6);
        qsizetype tiedCount = -1;
        QSet<QString> distinctBefores;
        for (const ProfileShapeIndex::BundledMatch& m : bucket) {
            const Profile bundled = Profile::loadFromFile(m.resourcePath);
            QVERIFY(bundled.isValid());
            const QVector<ProfileFieldDelta> d = Profile::dialInDeltas(bundled, user);
            if (tiedCount < 0) tiedCount = d.size();
            QCOMPARE(d.size(), tiedCount);
            QVERIFY(!d.isEmpty());
            distinctBefores.insert(QString::number(d.first().oldValue));
        }
        QVERIFY2(distinctBefores.size() > 1,
                 "fixture precondition: the tied candidates must disagree on the values, "
                 "or there is nothing for the equivalent-deltas rule to reject");

        const DialInComparison cmp = compareWithBundledBase(user, resolveProfileKb(user));

        QVERIFY2(!cmp.hasBase(),
                 "candidates that disagree on the values cannot be collapsed into one entry-level answer");
    }

    // The precondition both tests above rest on: the tea bucket really does hold
    // six FILES under one id, and two of them really are indistinguishable. If a
    // bundled profile is retuned later this fails loudly rather than quietly
    // ceasing to test either half of the tie rule — the equivalent-values case
    // needs the indistinguishable pair, the abstain case needs the other four to
    // disagree with them.
    void shapeIndex_theTeaBucketIsSixFilesUnderOneEntry()
    {
        const Profile tea =
            Profile::loadFromFile(QStringLiteral(":/profiles/tea_portafilter_white_tea.json"));
        QVERIFY(tea.isValid());

        const QVector<ProfileShapeIndex::BundledMatch> bucket =
            ProfileShapeIndex::bundledProfilesForShape(tea);
        QCOMPARE(bucket.size(), 6);

        QSet<QString> ids;
        for (const ProfileShapeIndex::BundledMatch& m : bucket) ids.insert(m.kbId);
        QCOMPARE(ids, QSet<QString>{ QStringLiteral("tea") });

        const Profile green =
            Profile::loadFromFile(QStringLiteral(":/profiles/tea_portafilter_chinese_green.json"));
        QVERIFY2(Profile::dialInDeltas(green, tea).isEmpty(),
                 "chinese green and white tea must remain indistinguishable on dial-in values");

        // And the rest of the bucket must NOT be: the abstain half of the tie
        // rule only has something to abstain over while the six disagree on the
        // values they tie on.
        int distinct = 0;
        for (const ProfileShapeIndex::BundledMatch& m : bucket) {
            const Profile other = Profile::loadFromFile(m.resourcePath);
            QVERIFY(other.isValid());
            if (!Profile::dialInDeltas(other, tea).isEmpty()) ++distinct;
        }
        QCOMPARE(distinct, 4);
    }

    // The "Based on X" line on the shot pages, end to end through the path
    // production actually takes. convertShotRecord has a fast path (analysis
    // already cached by loadShotRecordStatic) and a slow one (a fresh
    // ShotRecord, which is how every other test builds one), and the
    // derivation used to be computed ONLY in the slow branch — so every
    // hand-built ShotRecord in the suite showed the line while no real shot
    // ever did. Going through the database is the whole point: a fixture that
    // skips loadShotRecordStatic exercises the branch that was never broken.
    //
    // It lives in this file rather than beside the other DB round-trips in
    // tst_dbmigration because it needs BOTH profiles.qrc (for the shape index
    // to have anything to match against) and ai.qrc (for the entry's canonical
    // name). This binary already links both; tst_dbmigration links neither,
    // and adding them there costs two resource compiles for one test.
    void dialInBase_theDerivedFromNameSurvivesTheCachedAnalysisFastPath()
    {
        const QString profileJson = shippedProfileJson(QStringLiteral("blooming_espresso.json"),
                                                       QStringLiteral("Zzz Unrelated Name"));
        QVERIFY(!profileJson.isEmpty());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/derivedfrom.db");
        {
            ShotHistoryStorage storage;
            QVERIFY(storage.initialize(path));
            storage.close();
            QTRY_VERIFY(storage.isDbWorkIdle());
        }

        qint64 shotId = -1;
        const QString conn = QStringLiteral("derivedfrom_conn");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(path);
            QVERIFY(db.open());

            QSqlQuery ins(db);
            // profile_kb_id deliberately left NULL: a shape-resolved shot never
            // persists one (it is the dial-in grouping key), so this is the
            // state every such row is really in.
            ins.prepare(QStringLiteral(
                "INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, profile_json)"
                " VALUES ('derivedfrom', 1000, 'Zzz Unrelated Name', 30, :pj)"));
            ins.bindValue(QStringLiteral(":pj"), profileJson);
            QVERIFY2(ins.exec(), qPrintable(ins.lastError().text()));
            shotId = ins.lastInsertId().toLongLong();
            QVERIFY(shotId > 0);

            const ShotRecord r = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
            QVERIFY2(r.cachedAnalysis.has_value(),
                     "precondition: the load path must cache, or this test cannot fail");
            QVERIFY2(r.profileKbId.isEmpty(),
                     "a shape match must not have been written to the grouping column");

            const ShotProjection p = ShotHistoryStorage::convertShotRecord(r);
            QCOMPARE(p.profileKbDerivedFrom, QStringLiteral("Blooming Espresso"));
        }
        QSqlDatabase::removeDatabase(conn);
    }

    // === Candidate-set transfer rules (group 4) ===
    //
    // These decide what a look-alike profile inherits. The governing principle
    // is asymmetric: do not tell a user a by-design behaviour is a fault, but
    // still report what genuinely went wrong. That asymmetry is why the rules
    // differ per fact rather than being one policy.
    //
    // The buckets used as fixtures are the real shipped ones, pinned by
    // shippedShapeCollisionsAreExactlyTheKnownThree above.

    // flow_trend_ok is carried by preinfuse-then-45ml-of-water and NOT by
    // gentle-flat-long-preinfusion-family — a real disagreement in a real
    // bucket. Union: the flag applies, so neither profile's look-alike is told
    // its declining flow is fines migration.
    void candidateSet_disputedShapeFlagStillSuppresses()
    {
        const QStringList bucket{ QStringLiteral("gentle-flat-long-preinfusion-family"),
                                  QStringLiteral("preinfuse-then-45ml-of-water") };
        // Precondition: this really is a disagreement, not a coincidence.
        QVERIFY(!ShotSummarizer::getAnalysisFlags(bucket.at(0))
                     .contains(QStringLiteral("flow_trend_ok")));
        QVERIFY(ShotSummarizer::getAnalysisFlags(bucket.at(1))
                    .contains(QStringLiteral("flow_trend_ok")));

        QVERIFY2(ShotSummarizer::getAnalysisFlags(bucket).contains(QStringLiteral("flow_trend_ok")),
                 "a disputed shape-silencing flag must still apply — missing beats wrong");
    }

    // The opposite direction, and the reason the rule is a table rather than a
    // blanket union. grind_check_skip returns EARLY from analyzeFlowVsGoal, so
    // it also silences the choked-puck and yield-overshoot arms — physics
    // signals that hold on any profile. Applying it on one member's say-so
    // would hide a genuinely faulty shot.
    void candidateSet_disputedPhysicsFlagIsWithheld()
    {
        // turbo-shot carries grind_check_skip; blooming-espresso does not.
        const QStringList mixed{ QStringLiteral("turbo-shot"),
                                 QStringLiteral("blooming-espresso") };
        QVERIFY(ShotSummarizer::getAnalysisFlags(mixed.at(0))
                    .contains(QStringLiteral("grind_check_skip")));
        QVERIFY(!ShotSummarizer::getAnalysisFlags(mixed.at(1))
                     .contains(QStringLiteral("grind_check_skip")));

        QVERIFY2(!ShotSummarizer::getAnalysisFlags(mixed).contains(QStringLiteral("grind_check_skip")),
                 "a disputed physics-detector flag must NOT apply — it would hide a real fault");
    }

    // A disputed band is withheld. Two of the three real buckets disagree:
    // d-flow has no band, d-flow-la-pavoni-variant cites 6-9 bar.
    void candidateSet_disputedExpertBandIsWithheld()
    {
        const QStringList bucket{ QStringLiteral("d-flow"),
                                  QStringLiteral("d-flow-la-pavoni-variant") };
        QVERIFY(!ShotSummarizer::expertBandForKbId(bucket.at(0)).has_value());
        QVERIFY(ShotSummarizer::expertBandForKbId(bucket.at(1)).has_value());

        QVERIFY2(!ShotSummarizer::expertBandForKbIds(bucket).has_value(),
                 "a disputed band must be withheld, not guessed in either direction");
    }

    // Agreement TRANSFERS a value — the branch the disputed test above cannot
    // reach, and the one that carries a real number onto a shape-matched shot.
    //
    // Constructed, not a shipped bucket: of the two real multi-entry buckets
    // one disputes (d-flow) and the other has no band on either member, so
    // neither exercises this. `londinium` is paired with itself-by-alias so the
    // band is identical by construction; what is under test is that agreement
    // yields the value rather than nullopt.
    //
    // The `key` lambda in expertBandForKbIds deliberately excludes src and
    // confidence, so two entries citing one band from different sources still
    // agree. That exclusion had no assertion until this one.
    void candidateSet_agreedBandTransfersItsValue()
    {
        const QStringList agreed{ QStringLiteral("londinium"),
                                  QStringLiteral("londinium") };
        const auto single = ShotSummarizer::expertBandForKbId(QStringLiteral("londinium"));
        QVERIFY2(single.has_value(), "fixture precondition: londinium must carry a band");

        const auto set = ShotSummarizer::expertBandForKbIds(agreed);
        QVERIFY2(set.has_value(), "an agreed band must transfer, not be withheld");
        QCOMPARE(set->axis, single->axis);
        QCOMPARE(set->lo, single->lo);
        QCOMPARE(set->hi, single->hi);
    }

    // Absence is also an agreement, and must stay absent rather than becoming
    // a withheld-because-disputed nullopt by a different route. Weak on its
    // own — both the correct rule and an "always withhold" bug return nullopt
    // here — so it is paired with the transfer test above, which that bug
    // would fail.
    void candidateSet_agreedAbsentBandStaysAbsent()
    {
        const QStringList bucket{ QStringLiteral("gentle-flat-long-preinfusion-family"),
                                  QStringLiteral("preinfuse-then-45ml-of-water") };
        QVERIFY(!ShotSummarizer::expertBandForKbId(bucket.at(0)).has_value());
        QVERIFY(!ShotSummarizer::expertBandForKbId(bucket.at(1)).has_value());
        QVERIFY(!ShotSummarizer::expertBandForKbIds(bucket).has_value());
    }

    // UGS is an assertive number on the grind scale: d-flow is 0.5,
    // d-flow-la-pavoni-variant is 1.0. Disputed -> nothing.
    void candidateSet_disputedUgsIsWithheld()
    {
        const QStringList disputed{ QStringLiteral("d-flow"),
                                    QStringLiteral("d-flow-la-pavoni-variant") };
        QVERIFY(!std::isnan(ShotSummarizer::ugsForKbId(disputed.at(0))));
        QVERIFY(!std::isnan(ShotSummarizer::ugsForKbId(disputed.at(1))));
        QVERIFY2(std::isnan(ShotSummarizer::ugsForKbIds(disputed)),
                 "a disputed UGS must be withheld");

        // ...but an AGREED value transfers. damians-lr-v2-v3 and londinium are
        // both 0.0. NOT a shipped shape bucket — LRv3 has eight frames to
        // Londinium's seven — so this set is constructed to exercise the
        // agreement branch, which the two real buckets cannot: d-flow's
        // disputes above, and the gentle-flat pair carries no UGS at all.
        const QStringList agreed{ QStringLiteral("damians-lr-v2-v3"),
                                  QStringLiteral("londinium") };
        QCOMPARE(ShotSummarizer::ugsForKbIds(agreed),
                 ShotSummarizer::ugsForKbId(agreed.at(0)));
    }

    // The degeneracy that makes a unique shape match indistinguishable from a
    // title match for every consumer. If this drifts, shape-resolved profiles
    // quietly become second-class.
    void candidateSet_singleMemberIsIdenticalToTitleResolution()
    {
        const QStringList ids{ QStringLiteral("londinium") };
        QCOMPARE(ShotSummarizer::getAnalysisFlags(ids),
                 ShotSummarizer::getAnalysisFlags(ids.first()));
        QCOMPARE(ShotSummarizer::ugsForKbIds(ids), ShotSummarizer::ugsForKbId(ids.first()));
        QCOMPARE(ShotSummarizer::expertBandForKbIds(ids).has_value(),
                 ShotSummarizer::expertBandForKbId(ids.first()).has_value());
    }

    void candidateSet_emptySetYieldsNothing()
    {
        QVERIFY(ShotSummarizer::getAnalysisFlags(QStringList{}).isEmpty());
        QVERIFY(!ShotSummarizer::expertBandForKbIds(QStringList{}).has_value());
        QVERIFY(std::isnan(ShotSummarizer::ugsForKbIds(QStringList{})));
    }

    // === resolveProfileKb composition (4.1/4.5) ===

    static Profile loadShipped(const QString& file)
    {
        QFile f(shippedProfileDir().filePath(file));
        if (!f.open(QIODevice::ReadOnly)) return {};
        return Profile::fromJson(QJsonDocument::fromJson(f.readAll()));
    }

    // Title resolution wins and the shape step is never consulted — the
    // property that keeps every built-in's resolution byte-identical.
    void resolveProfileKb_titleWinsAndIsUnique()
    {
        const Profile p = loadShipped(QStringLiteral("londonium.json"));
        QVERIFY(!p.title().isEmpty());
        const KbResolution r = resolveProfileKb(p);
        QCOMPARE(r.origin, KbResolution::Origin::Title);
        QCOMPARE(r.ids, QStringList{ QStringLiteral("londinium") });
        QVERIFY(r.hasIdentity());
    }

    // The case the change exists for: same frames, a title no title-step can
    // reach. Must resolve by shape, and must be flagged as an inference.
    void resolveProfileKb_renamedProfileResolvesByShape()
    {
        Profile p = loadShipped(QStringLiteral("blooming_espresso.json"));
        QVERIFY(!p.steps().isEmpty());
        p.setTitle(QStringLiteral("Zzz Unrelated Name"));
        QVERIFY2(ShotSummarizer::computeProfileKbId(p.title(), p.editorType()).isEmpty(),
                 "fixture precondition: the title must not resolve on its own");

        const KbResolution r = resolveProfileKb(p);
        QCOMPARE(r.origin, KbResolution::Origin::Shape);
        QVERIFY2(r.ids.contains(QStringLiteral("blooming-espresso")),
                 qPrintable(QStringLiteral("got [%1]").arg(r.ids.join(QStringLiteral(", ")))));
    }

    // The inverse obligation: a structurally different profile must NOT match.
    // Without this the positive case above proves only that something matched.
    void resolveProfileKb_structurallyDifferentProfileDoesNotMatch()
    {
        Profile p = loadShipped(QStringLiteral("blooming_espresso.json"));
        QVERIFY(p.steps().size() > 1);
        p.setTitle(QStringLiteral("Zzz Unrelated Name"));

        // Remove a frame — a structural edit, not a dial-in change.
        QList<ProfileFrame> fewer = p.steps();
        fewer.removeLast();
        p.setSteps(fewer);

        const KbResolution r = resolveProfileKb(p);
        QVERIFY2(r.origin != KbResolution::Origin::Shape
                     || !r.ids.contains(QStringLiteral("blooming-espresso")),
                 "a profile with a frame removed must not inherit the original's knowledge");
    }

    // An ambiguous shape resolves for ANALYSIS but withholds IDENTITY — you
    // can suppress a false positive without claiming to know which profile the
    // shot's was derived from.
    void resolveProfileKb_ambiguousShapeWithholdsIdentityButNotAnalysis()
    {
        // D-Flow / default shares its shape with D-Flow / La Pavoni, which is
        // a real disagreement between two real profiles: the La Pavoni variant
        // pulls coarser (UGS 1.0 vs 0.5) off a lower pressure target and an
        // 84C fill, and only it carries an author-stated pressure-peak band.
        //
        // This fixture used to be londonium.json. That stopped being ambiguous
        // when LRv2 and Londonium were recognised as one profile and their KB
        // entries merged — see shippedShapeCollisionsAreExactlyTheKnownThree.
        Profile p = loadShipped(QStringLiteral("d_flow_default.json"));
        p.setTitle(QStringLiteral("Zzz Unrelated Name"));
        const KbResolution r = resolveProfileKb(p);

        QCOMPARE(r.origin, KbResolution::Origin::Shape);
        QVERIFY2(r.ids.size() > 1, "fixture precondition: this shape is ambiguous");
        QVERIFY2(!r.hasIdentity(), "an ambiguous set must not claim an identity");

        // Analysis facts still flow: both carry flow_trend_ok.
        QVERIFY(ShotSummarizer::getAnalysisFlags(r.ids).contains(QStringLiteral("flow_trend_ok")));
        // The band only one of them carries does not.
        QVERIFY(!ShotSummarizer::expertBandForKbIds(r.ids).has_value());
    }

    // The payoff of the LRv2/Londonium KB merge, stated as behaviour rather
    // than as a bucket count. A renamed copy of the profile the user actually
    // sees (Londonium is the visible one; LRv2 ships hidden) now resolves to a
    // SINGLE entry, so it gets an identity, the sparkle, a "Based on" name —
    // and the cited pressure-peak band, which the pre-merge two-entry split
    // withheld under unanimity even though both entries described one profile.
    void resolveProfileKb_renamedLondoniumResolvesToOneEntryWithItsBand()
    {
        Profile p = loadShipped(QStringLiteral("londonium.json"));
        p.setTitle(QStringLiteral("Zzz Unrelated Name"));
        const KbResolution r = resolveProfileKb(p);

        QCOMPARE(r.origin, KbResolution::Origin::Shape);
        QVERIFY2(r.hasIdentity(),
                 qPrintable(QStringLiteral("expected one candidate, got: %1")
                                .arg(r.ids.join(QLatin1Char(',')))));
        QCOMPARE(r.ids.first(), QStringLiteral("londinium"));

        const auto band = ShotSummarizer::expertBandForKbIds(r.ids);
        QVERIFY2(band.has_value(), "the merged entry must carry the cited band");
        QCOMPARE(*band->lo, 8.0);
        QCOMPARE(*band->hi, 9.0);
    }

    // Disabling the shape step must leave every shipped profile untouched.
    void resolveProfileKb_shippedProfilesNeverDependOnTheShapeStep_data()
    {
        shapeIndex_shippedProfileFindsItsOwnEntry_data();
    }

    void resolveProfileKb_shippedProfilesNeverDependOnTheShapeStep()
    {
        QFETCH(QString, filePath);
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const Profile p = Profile::fromJson(QJsonDocument::fromJson(f.readAll()));
        if (p.title().isEmpty()) return;

        const KbResolution r = resolveProfileKb(p);
        QVERIFY2(r.origin == KbResolution::Origin::Title,
                 qPrintable(QStringLiteral("%1 reached the shape step; built-ins must resolve by title")
                                .arg(p.title())));
    }

    // === prepareAnalysisInputs wiring (group 5) ===
    //
    // The two tests above prove the RULES; these prove the analysis path
    // actually reaches them. Without these, every rule could be correct and
    // no shot would ever benefit.

    static QString shippedProfileJson(const QString& file, const QString& retitleTo = QString())
    {
        QFile f(shippedProfileDir().filePath(file));
        if (!f.open(QIODevice::ReadOnly)) return {};
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (!retitleTo.isEmpty()) o[QStringLiteral("title")] = retitleTo;
        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }

    // The whole change, end to end at the analysis layer: a renamed copy of a
    // documented profile, with NO persisted kbId, still receives that entry's
    // suppression flags — so its shot is not told a by-design curve is a fault.
    void prepareAnalysisInputs_renamedProfileStillGetsItsSuppressionFlags()
    {
        const QString json = shippedProfileJson(QStringLiteral("blooming_espresso.json"),
                                                QStringLiteral("Zzz Unrelated Name"));
        QVERIFY(!json.isEmpty());

        // Empty persisted id: exactly what a shape-resolved profile carries.
        const auto inputs = decenza::storage::detail::prepareAnalysisInputs(QString(), json);

        QVERIFY2(inputs.analysisFlags.contains(QStringLiteral("channeling_expected")),
                 qPrintable(QStringLiteral("got flags [%1]")
                                .arg(inputs.analysisFlags.join(QStringLiteral(", ")))));
        QVERIFY2(inputs.profileKbResolved,
                 "Arm 1's gate must open for a shape-resolved profile");
        QCOMPARE(inputs.identityKbId, QStringLiteral("blooming-espresso"));
        QVERIFY2(inputs.identityFromShape,
                 "a shape match must be marked as inferred, not presented as the profile's own name");
    }

    // The rule that decides what reaches the `profile_kb_id` COLUMN, which is
    // the dial-in grouping key (`WHERE profile_kb_id = ?`) and not merely an
    // analysis key. A shape match must never be written there: shape ignores
    // temperature and setpoints, so persisting one merges a user's re-tuned
    // variant into the documented profile's dial-in history.
    //
    // Pinned here rather than through saveShot(), which no test can reach
    // without a ~50-field ShotSaveData fixture that does not exist in this
    // tree. That leaves the CALL genuinely uncovered — see the note in the
    // change's tasks.md — but the rule itself cannot drift unnoticed.
    void persistableId_onlyATitleResolutionReachesTheColumn()
    {
        const KbResolution byTitle{ { QStringLiteral("blooming-espresso") },
                                    KbResolution::Origin::Title };
        QCOMPARE(byTitle.persistableId(), QStringLiteral("blooming-espresso"));

        const KbResolution uniqueShape{ { QStringLiteral("blooming-espresso") },
                                        KbResolution::Origin::Shape };
        QVERIFY2(uniqueShape.persistableId().isEmpty(),
                 "a UNIQUE shape match is still an inference, not a recorded identity - "
                 "persisting it would regroup dial-in history");

        const KbResolution ambiguousShape{ { QStringLiteral("d-flow"),
                                             QStringLiteral("d-flow-la-pavoni-variant") },
                                           KbResolution::Origin::Shape };
        QVERIFY(ambiguousShape.persistableId().isEmpty());

        QVERIFY(KbResolution{}.persistableId().isEmpty());
    }

    // The stored-id fallback branch, which every one of the tests around it
    // leaves unexercised by passing an empty persisted id. It is the path for
    // every legacy row whose profile_json is absent or unparseable, and it is
    // what preserves pre-change behaviour for them.
    void prepareAnalysisInputs_unparseableJsonFallsBackToTheStoredId()
    {
        for (const QString& json : { QString(), QStringLiteral("{ not json") }) {
            if (!json.isEmpty())
                QTest::ignoreMessage(QtWarningMsg,
                                     QRegularExpression(QStringLiteral("stored profile JSON unparseable")));
            const auto inputs = decenza::storage::detail::prepareAnalysisInputs(
                QStringLiteral("blooming-espresso"), json);

            QVERIFY2(inputs.profileKbResolved,
                     "a legacy row with a stored id still has profile context");
            QCOMPARE(inputs.analysisFlags,
                     ShotSummarizer::getAnalysisFlags(QStringLiteral("blooming-espresso")));
            QCOMPARE(inputs.identityKbId, QStringLiteral("blooming-espresso"));
            QVERIFY2(!inputs.identityFromShape,
                     "a stored id is a recorded identity, not a shape inference");
        }
    }

    // The "Default" trap, asserted rather than left to the comment that
    // documents it. A default-constructed Profile is titled "Default", which
    // is a REAL shipped profile whose KB entry carries flow_trend_ok — so an
    // unreadable shot with NO stored id must resolve to nothing rather than
    // inherit that entry's suppression.
    void prepareAnalysisInputs_unparseableJsonNeverInheritsTheDefaultEntry()
    {
        QVERIFY2(ShotSummarizer::getAnalysisFlags(QStringLiteral("default"))
                     .contains(QStringLiteral("flow_trend_ok")),
                 "fixture precondition: the 'default' KB entry must carry flow_trend_ok, "
                 "otherwise this test cannot fail");

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("stored profile JSON unparseable")));
        const auto inputs = decenza::storage::detail::prepareAnalysisInputs(
            QString(), QStringLiteral("{ not json"));
        QVERIFY2(inputs.analysisFlags.isEmpty(),
                 qPrintable(QStringLiteral("an unreadable profile inherited flags [%1]")
                                .arg(inputs.analysisFlags.join(QStringLiteral(", ")))));
        QVERIFY(!inputs.profileKbResolved);
        QVERIFY(inputs.identityKbId.isEmpty());
    }

    // Every analysisFlags value in the shipped KB must be one the transfer
    // rules classify. A misspelled flag is otherwise a silent no-op: no
    // consumer matches it, and getAnalysisFlags(QStringList) additionally
    // defaults it to unanimity, so it would neither suppress nor complain.
    void everyShippedAnalysisFlagIsAKnownFlag()
    {
        static const QSet<QString> known{
            QStringLiteral("flow_trend_ok"),        // union
            QStringLiteral("channeling_expected"),  // union
            QStringLiteral("grind_check_skip"),     // unanimity
        };

        QFile f(QStringLiteral(":/ai/profile_knowledge.json"));
        QVERIFY2(f.open(QIODevice::ReadOnly), "KB resource missing");
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonArray entries = root.value(QStringLiteral("profiles")).toArray();
        QVERIFY2(!entries.isEmpty(), "fixture precondition: the KB must have entries");

        int seen = 0;
        for (const QJsonValue& e : entries) {
            const QString id = e.toObject().value(QStringLiteral("id")).toString();
            for (const QJsonValue& fl : e.toObject().value(QStringLiteral("analysisFlags")).toArray()) {
                ++seen;
                QVERIFY2(known.contains(fl.toString()),
                         qPrintable(QStringLiteral("entry '%1' carries unknown analysisFlag '%2' - "
                                                   "add it to the transfer classification in "
                                                   "shotsummarizer_kb.cpp, or fix the typo")
                                        .arg(id, fl.toString())));
            }
        }
        QVERIFY2(seen > 0, "fixture precondition: some entry must carry an analysisFlag");
    }

    // Nothing about a title-resolvable profile may change.
    void prepareAnalysisInputs_titleResolvedProfileIsUnchanged()
    {
        const QString json = shippedProfileJson(QStringLiteral("blooming_espresso.json"));
        const auto inputs = decenza::storage::detail::prepareAnalysisInputs(QString(), json);

        QCOMPARE(inputs.identityKbId, QStringLiteral("blooming-espresso"));
        QVERIFY2(!inputs.identityFromShape, "this resolved by title, not shape");
        QVERIFY(inputs.profileKbResolved);
        QCOMPARE(inputs.analysisFlags,
                 ShotSummarizer::getAnalysisFlags(QStringLiteral("blooming-espresso")));
    }

    // An unrecognisable profile must still gate Arm 1 OFF. If this regresses,
    // the change has quietly turned grind advice on for every profile, which
    // is the false-positive source skip-grind-arm1-when-kb-unresolved removed.
    void prepareAnalysisInputs_unrecognisedProfileStaysUnresolved()
    {
        QJsonObject step{
            {"name", "pour"}, {"pump", "flow"}, {"sensor", "coffee"},
            {"transition", "smooth"}, {"temperature", QStringLiteral("93.0")},
            {"flow", QStringLiteral("2.2")}, {"seconds", QStringLiteral("37.0")},
        };
        const QJsonObject o{
            {"title", "Zzz Nothing Like Anything Shipped"},
            {"beverage_type", "espresso"},
            {"steps", QJsonArray{step}},
        };
        const auto inputs = decenza::storage::detail::prepareAnalysisInputs(
            QString(), QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));

        QVERIFY2(!inputs.profileKbResolved, "an unrecognisable profile must not gate Arm 1 on");
        QVERIFY(inputs.analysisFlags.isEmpty());
        QVERIFY(inputs.identityKbId.isEmpty());
    }

    // A shot with no usable profileJson must resolve to NOTHING — and
    // specifically must not be attributed to the shipped "Default" profile.
    //
    // Profile default-constructs with the title "Default", and "Default" is a
    // real shipped profile with a KB entry carrying flow_trend_ok and a UGS of
    // 0.75. An unguarded resolve therefore hands every profile-less shot that
    // entry's suppression, discarding real flow-trend findings on unrelated
    // shots. This path stepped in exactly that during implementation.
    void prepareAnalysisInputs_absentProfileJsonIsNotAttributedToTheDefaultProfile()
    {
        // Precondition: "Default" really does resolve and really does carry a
        // suppression flag — otherwise this test proves nothing.
        const QString defaultId = ShotSummarizer::computeProfileKbId(QStringLiteral("Default"));
        QVERIFY2(!defaultId.isEmpty(), "fixture precondition: 'Default' is a resolvable profile");
        QVERIFY2(ShotSummarizer::getAnalysisFlags(defaultId).contains(QStringLiteral("flow_trend_ok")),
                 "fixture precondition: the Default entry carries a suppression flag to leak");

        for (const QString& json : { QString(), QStringLiteral("not json at all"),
                                     QStringLiteral("{}") }) {
            // The unparseable case now reports itself (a legacy row whose own
            // profile cannot be read is otherwise uninspectable). Declared
            // rather than tolerated, so the warning is part of the assertion.
            if (json == QStringLiteral("not json at all"))
                QTest::ignoreMessage(QtWarningMsg,
                                     QRegularExpression(QStringLiteral("stored profile JSON unparseable")));
            const auto inputs = decenza::storage::detail::prepareAnalysisInputs(QString(), json);
            QVERIFY2(!inputs.profileKbResolved, qPrintable(QStringLiteral("resolved for %1").arg(json)));
            QVERIFY2(inputs.identityKbId.isEmpty(),
                     qPrintable(QStringLiteral("attributed to '%1'").arg(inputs.identityKbId)));
            QVERIFY2(inputs.analysisFlags.isEmpty(),
                     qPrintable(QStringLiteral("leaked flags [%1]")
                                    .arg(inputs.analysisFlags.join(QStringLiteral(", ")))));
        }
    }

};

QTEST_GUILESS_MAIN(tst_ShotSummarizer)

#include "tst_shotsummarizer.moc"
