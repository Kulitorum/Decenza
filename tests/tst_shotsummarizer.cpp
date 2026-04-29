// tst_shotsummarizer — verifies that ShotSummarizer's AI-prompt path shares
// the same suppression cascade as the in-app Shot Summary dialog. Issue #921
// closed the gap where ShotSummarizer ran its own channeling/temperature
// detectors on the puck-failure population (peak pressure < PRESSURE_FLOOR_BAR
// = 2.5 bar) and produced misleading observations the AI advisor would then
// dial-in against.
//
// The fix delegates detector orchestration to ShotAnalysis::generateSummary
// (the same call ShotHistoryStorage::generateShotSummary makes for the
// dialog), so any drift between the two paths is structural-impossible. These
// tests pin the contract: pourTruncatedDetected fires on low-peak shots,
// channeling/temp lines are suppressed, the "Puck failed" warning + verdict
// reach the prompt, and a healthy shot still surfaces the normal observations.

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
        QVERIFY2(prompt.contains(QStringLiteral("## Dialog Verdict")),
                 "prompt must include the Dialog Verdict section header");
        QVERIFY2(prompt.contains(QStringLiteral("Pour never pressurized")),
                 "prompt must surface the puck-failed warning to the AI");
        QVERIFY2(!prompt.contains(QStringLiteral("Puck integrity")),
                 "old hand-rolled 'Puck integrity' line must be gone");
        QVERIFY2(!prompt.contains(QStringLiteral("Temperature deviation")),
                 "old hand-rolled 'Temperature deviation' line must be gone");
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
        QVERIFY2(prompt.contains(QStringLiteral("## Dialog Verdict")),
                 "verdict section must always render");
    }
};

QTEST_APPLESS_MAIN(tst_ShotSummarizer)

#include "tst_shotsummarizer.moc"
