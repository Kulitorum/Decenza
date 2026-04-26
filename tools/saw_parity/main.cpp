// saw_parity — confirms tools/saw_replay/'s standalone port of the SAW math
// produces the same per-shot predictions as the production code path in
// src/core/settings.cpp. A passing run lets us trust simulator-driven sweep
// results (e.g. σ=0.25 vs 1.5) as predictive of what production would do
// under the same change.
//
// Architecture:
//   - Loads the same corpus the simulator consumed
//   - Walks shots in chronological order, calling production's
//     Settings::getExpectedDripFor(profile, scale, flow) followed by
//     Settings::addSawLearningPoint(...) to grow the per-pair state
//   - Reads the simulator's TSV output (the `old_pred` column, which is
//     the simulator's OLD-model prediction at the same point)
//   - Reports per-shot abs-deviation and aggregate by the simulator's
//     reported `source` column
//
// What's actually equivalent vs not:
//   - perPair (≥2 committed medians for the pair): both code paths run the
//     same Gaussian-weighted-average algorithm. EXACT match expected.
//   - globalBootstrap: simulator aggregates a per-scale pool across pairs
//     (the proposal's smart-bootstrap concept). Production uses a single
//     scalar `globalSawBootstrapLag(scale) * flow`. NOT equivalent — these
//     deviations are expected and don't indicate a porting bug.
//   - scaleDefault: both fall back to `flow * (sensorLag + 0.1)`. EXACT
//     match expected.
//
// The pass/fail signal is therefore the max abs-deviation among shots whose
// simulator source is `perPair` or `scaleDefault`. globalBootstrap deviations
// are reported informatively but excluded from the gate.
//
// Usage:
//   tools/saw_replay --corpus baseline_full.json --variant old --mode legacy --sigma 0.25 > sim.tsv
//   saw_parity --corpus baseline_full.json --sim sim.tsv

#include "core/settings.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {

struct SimRow {
    double oldPred = 0.0;
    QString source;
};

QHash<int, SimRow> readSimulatorOutput(const QString& path, QString* errOut) {
    QHash<int, SimRow> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errOut) *errOut = QStringLiteral("cannot open sim file: ") + path;
        return out;
    }
    QTextStream ss(&f);
    while (!ss.atEnd()) {
        const QString line = ss.readLine();
        if (line.isEmpty() || line.startsWith('#')
            || line.startsWith("===") || line.startsWith("shot_id")
            || line.startsWith("bucket") || line.startsWith("source")
            || line.startsWith("clamp_hits") || line.startsWith("perPair")
            || line.startsWith("pendingBatch") || line.startsWith("globalBootstrap")
            || line.startsWith("scaleDefault") || line.startsWith("overall")
            || line.startsWith("low ") || line.startsWith("mid ")
            || line.startsWith("high ") || line.startsWith("shot1")
            || line.startsWith("shots")) {
            continue;
        }
        const QStringList cols = line.split('\t');
        // simulator schema: shot_id, pair_idx, flow, actual, old_pred, new_pred,
        //                   old_err, new_err, source, bucket
        if (cols.size() < 9) continue;
        bool ok = false;
        const int id = cols[0].toInt(&ok);
        if (!ok) continue;
        SimRow r;
        r.oldPred = cols[4].toDouble(&ok);
        if (!ok) continue;
        r.source = cols[8];
        out.insert(id, r);
    }
    return out;
}

void wipeAllSawState(Settings& s) {
    s.resetSawLearning();
    const QJsonObject pairs = s.allPerProfileSawHistory();
    for (auto it = pairs.constBegin(); it != pairs.constEnd(); ++it) {
        const QStringList parts = it.key().split(QStringLiteral("::"));
        if (parts.size() == 2) {
            s.resetSawLearningForProfile(parts[0], parts[1]);
        }
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    // Use a dedicated QSettings org so we don't touch the user's real Decenza state.
    QCoreApplication::setOrganizationName(QStringLiteral("DecenzaParityTest"));
    QCoreApplication::setApplicationName(QStringLiteral("saw_parity"));
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption corpusOpt({"c", "corpus"}, "Corpus JSON path.", "path");
    parser.addOption(corpusOpt);
    QCommandLineOption simOpt({"s", "sim"}, "Simulator TSV output.", "path");
    parser.addOption(simOpt);
    QCommandLineOption tolOpt("tolerance",
        "Max allowed |production - simulator| per shot in g (default 0.001).",
        "value", "0.001");
    parser.addOption(tolOpt);
    parser.process(app);

    if (!parser.isSet(corpusOpt) || !parser.isSet(simOpt)) {
        qWarning() << "Both --corpus and --sim are required.";
        return 2;
    }
    const double tolerance = parser.value(tolOpt).toDouble();

    QFile cf(parser.value(corpusOpt));
    if (!cf.open(QIODevice::ReadOnly)) {
        qWarning() << "cannot open corpus" << cf.fileName();
        return 2;
    }
    QJsonParseError jerr;
    auto doc = QJsonDocument::fromJson(cf.readAll(), &jerr);
    if (jerr.error != QJsonParseError::NoError) {
        qWarning() << "corpus parse error:" << jerr.errorString();
        return 2;
    }
    const QJsonArray shotsArr = doc.object().value(QStringLiteral("shots")).toArray();
    if (shotsArr.isEmpty()) {
        qWarning() << "no shots in corpus";
        return 2;
    }

    QString simErr;
    const QHash<int, SimRow> sim = readSimulatorOutput(parser.value(simOpt), &simErr);
    if (!simErr.isEmpty()) {
        qWarning() << simErr;
        return 2;
    }
    if (sim.isEmpty()) {
        qWarning() << "no shot rows parsed from sim file";
        return 2;
    }

    Settings settings;
    wipeAllSawState(settings);

    QTextStream out(stdout);
    out.setRealNumberPrecision(5);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out << "shot_id\tflow\tactual\tprod_pred\tsim_pred\tabs_dev\tprod_err\tsim_source\n";

    struct Stat {
        int n = 0;
        double maxDev = 0.0;
        double sumDev = 0.0;
    };
    QHash<QString, Stat> bySource;
    int missingFromSim = 0;

    // Production-side MAE accumulators — these answer the actual question of
    // whether the current production σ value is good or bad on this corpus,
    // independent of any simulator validation.
    struct MaeBucket {
        int n = 0;
        double absSum = 0.0;
        double worst = 0.0;
    };
    MaeBucket maeOverall, maeLow, maeMid, maeHigh;
    auto bumpMae = [](MaeBucket& b, double absErr) {
        b.n += 1;
        b.absSum += absErr;
        b.worst = std::max(b.worst, absErr);
    };
    double shot887Err = 0.0;
    bool shot887Seen = false;

    // Production MAE bucketed by the simulator-reported source. Lets us
    // compare "production scalar bootstrap" performance against "simulator
    // smart-pool bootstrap" on the same set of shots — the core motivation
    // for the smart-saw-bootstrap proposal.
    QHash<QString, MaeBucket> prodMaeBySource;

    for (const auto& v : shotsArr) {
        const QJsonObject o = v.toObject();
        const int id = o.value(QStringLiteral("id")).toInt();
        const QString profile = o.value(QStringLiteral("profile")).toString();
        const QString scale = o.value(QStringLiteral("scale")).toString();
        const double flow = o.value(QStringLiteral("flow")).toDouble();
        const double drip = o.value(QStringLiteral("drip")).toDouble();
        const double overshoot = o.value(QStringLiteral("overshoot")).toDouble();

        const double prodPred = settings.getExpectedDripFor(profile, scale, flow);
        const double prodErr = prodPred - drip;
        const double prodAbs = std::abs(prodErr);

        // Production-side MAE bookkeeping (independent of simulator parity).
        bumpMae(maeOverall, prodAbs);
        if (flow < 1.5) bumpMae(maeLow, prodAbs);
        else if (flow < 3.0) bumpMae(maeMid, prodAbs);
        else bumpMae(maeHigh, prodAbs);
        if (id == 887) { shot887Err = prodErr; shot887Seen = true; }

        double simPred = 0.0;
        QString simSource = QStringLiteral("(missing)");
        if (sim.contains(id)) {
            const SimRow& r = sim.value(id);
            simPred = r.oldPred;
            simSource = r.source;
            const double dev = std::abs(prodPred - simPred);
            Stat& s = bySource[simSource];
            s.n += 1;
            s.maxDev = std::max(s.maxDev, dev);
            s.sumDev += dev;
            // Also accumulate production MAE bucketed by sim source for the
            // smart-pool vs scalar-bootstrap comparison.
            bumpMae(prodMaeBySource[simSource], prodAbs);
        } else {
            ++missingFromSim;
        }
        const double dev = sim.contains(id) ? std::abs(prodPred - simPred) : 0.0;
        out << id << "\t" << flow << "\t" << drip << "\t"
            << prodPred << "\t" << simPred << "\t" << dev
            << "\t" << prodErr << "\t" << simSource << "\n";

        // Grow the production-side pool for the next shot.
        settings.addSawLearningPoint(drip, flow, scale, overshoot, profile);
    }

    out << "\n=== Production MAE per flow bucket (the actual answer) ===\n";
    out << "bucket\tn\tmae\tworst\n";
    auto reportBucket = [&out](const QString& name, const MaeBucket& b) {
        const double mae = b.n ? b.absSum / b.n : 0.0;
        out << name << "\t" << b.n << "\t" << mae << "\t" << b.worst << "\n";
    };
    reportBucket(QStringLiteral("overall"), maeOverall);
    reportBucket(QStringLiteral("low (<1.5)"), maeLow);
    reportBucket(QStringLiteral("mid [1.5,3)"), maeMid);
    reportBucket(QStringLiteral("high (>=3)"), maeHigh);
    if (shot887Seen) {
        out << "shot887_signed_error=" << shot887Err << "\n";
    }

    out << "\n=== Simulator parity by source ===\n";
    out << "source\tn\tmax_dev\tmean_dev\n";
    for (const auto& key : {QStringLiteral("perPair"), QStringLiteral("pendingBatch"),
                             QStringLiteral("globalBootstrap"), QStringLiteral("scaleDefault")}) {
        const Stat& s = bySource.value(key);
        const double mean = s.n ? s.sumDev / s.n : 0.0;
        out << key << "\t" << s.n << "\t" << s.maxDev << "\t" << mean << "\n";
    }
    out << "missing_from_sim=" << missingFromSim << "\n";

    // Production MAE bucketed by simulator source — enables direct
    // comparison vs simulator's smart-pool bootstrap MAE for the same shots.
    out << "\n=== Production MAE by simulator source ===\n";
    out << "source\tn\tprod_mae\tprod_worst\n";
    for (const auto& key : {QStringLiteral("perPair"), QStringLiteral("globalBootstrap"),
                             QStringLiteral("scaleDefault")}) {
        const MaeBucket& m = prodMaeBySource.value(key);
        const double mae = m.n ? m.absSum / m.n : 0.0;
        out << key << "\t" << m.n << "\t" << mae << "\t" << m.worst << "\n";
    }

    // Pass/fail gate: only assert equivalence on the paths that ARE structurally
    // equivalent (perPair, scaleDefault). globalBootstrap is expected to deviate
    // because the simulator implements the proposal's smart-bootstrap pool and
    // production uses a single scalar globalSawBootstrapLag.
    const double gateMax = std::max(bySource.value(QStringLiteral("perPair")).maxDev,
                                    bySource.value(QStringLiteral("scaleDefault")).maxDev);
    out << "\nGate: max_dev across {perPair, scaleDefault} = " << gateMax
        << " (tolerance=" << tolerance << ") → "
        << (gateMax <= tolerance ? "PASS" : "FAIL") << "\n";

    wipeAllSawState(settings);
    return (gateMax <= tolerance) ? 0 : 1;
}
