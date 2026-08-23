// saw_parity — confirms tools/saw_replay/'s standalone port of the SAW math
// produces the same per-shot predictions as the production code path in
// src/core/settings_calibration.cpp. A passing run lets us trust simulator-driven sweep
// results (e.g. σ=0.25 vs 1.5) as predictive of what production would do
// under the same change.
//
// Architecture:
//   - Loads the same corpus the simulator consumed
//   - Walks shots in chronological order, calling production's
//     SettingsCalibration::getExpectedDripFor(profile, scale, flow, basket) followed by
//     SettingsCalibration::addSawLearningPoint(...) to grow the per-(profile, scale,
//     basket) state
//   - Reads the simulator's TSV output (the `old_pred` column, which is
//     the simulator's OLD-model prediction at the same point)
//   - Reports per-shot abs-deviation and aggregate by the simulator's
//     reported `source` column
//
// What's actually equivalent vs not:
//   - perPair (kSawMinMediansForGraduation committed medians for the pair, 1 today):
//     both code paths run the
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
//   C=tools/saw_replay/data/baseline_extended.json
//
//   saw_parity --corpus $C                    # production MAE per flow bucket
//   saw_parity --corpus $C --ignore-basket    # ... with the basket segment removed
//
//   tools/saw_replay --corpus $C --variant old --mode legacy --sigma 0.25 > sim.tsv
//   saw_parity --corpus $C --sim sim.tsv      # ... plus simulator parity
//
// Only baseline_extended.json carries a basket key. The other two corpora in that
// directory predate it; see the missing-basket warning below.

#include "core/settings.h"
#include "core/settings_calibration.h"

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

// resetSawLearning() wipes every saw/* key — global pool, all per-(profile, scale,
// basket) buckets, all pending batches, all bootstrap values (see its declaration in
// settings_calibration.h). The per-pair loop that used to follow it here split pool
// keys on "::" and acted only on two-segment ones, so once the basket key added a
// third segment it matched nothing; it was redundant before that and dead after.
void wipeAllSawState(Settings& s) {
    s.calibration()->resetSawLearning();
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
    QCommandLineOption ignoreBasketOpt("ignore-basket",
        "Replay with every shot filed under one basket, as a two-segment "
        "(profile, scale) key would. Measures what the basket segment buys.");
    parser.addOption(ignoreBasketOpt);
    QCommandLineOption tolOpt("tolerance",
        "Max allowed |production - simulator| per shot in g (default 0.001).",
        "value", "0.001");
    parser.addOption(tolOpt);
    parser.process(app);

    if (!parser.isSet(corpusOpt)) {
        qWarning() << "--corpus is required.";
        return 2;
    }
    // --sim is optional: the production MAE table below is produced by driving the real
    // getExpectedDripFor/addSawLearningPoint over the corpus and needs no simulator at
    // all. Only the parity comparison does.
    const bool haveSim = parser.isSet(simOpt);
    const double tolerance = parser.value(tolOpt).toDouble();
    const bool ignoreBasket = parser.isSet(ignoreBasketOpt);

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

    QHash<int, SimRow> sim;
    if (haveSim) {
        QString simErr;
        sim = readSimulatorOutput(parser.value(simOpt), &simErr);
        if (!simErr.isEmpty()) {
            qWarning() << simErr;
            return 2;
        }
        if (sim.isEmpty()) {
            qWarning() << "no shot rows parsed from sim file";
            return 2;
        }
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
    int rowsWithoutBasket = 0;
    auto bumpMae = [](MaeBucket& b, double absErr) {
        b.n += 1;
        b.absSum += absErr;
        b.worst = std::max(b.worst, absErr);
    };
    // The archived analyses' "shot 887" is id 10887 in the extended corpus: part 1's ids
    // come from a database that has since been renumbered and are offset by +10000. A
    // plain 887 also exists there and is an unrelated part-2 shot, so matching both would
    // silently report whichever came last in the array.
    constexpr int kArchivedProbeId = 10887;
    double archivedProbeErr = 0.0;
    bool archivedProbeSeen = false;

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
        // baseline_extended.json rows carry the basket key the shot actually trained.
        // Pass it through rather than letting it resolve: an empty key resolves against
        // this process's (empty) settings to "(none)", collapsing every basket into one
        // pool and hiding exactly the separation key-saw-learning-by-basket introduced.
        // baseline.json and baseline_full.json predate the basket key and have no such
        // field, so a run over either is silently that collapsed case — counted here and
        // reported beside the results rather than left to look like a keyed run.
        const QString corpusBasket = o.value(QStringLiteral("basket")).toString();
        if (corpusBasket.isEmpty()) ++rowsWithoutBasket;
        const QString basket = ignoreBasket ? QStringLiteral("all") : corpusBasket;

        const double prodPred =
            settings.calibration()->getExpectedDripFor(profile, scale, flow, basket);
        const double prodErr = prodPred - drip;
        const double prodAbs = std::abs(prodErr);

        // Production-side MAE bookkeeping (independent of simulator parity).
        bumpMae(maeOverall, prodAbs);
        if (flow < 1.5) bumpMae(maeLow, prodAbs);
        else if (flow < 3.0) bumpMae(maeMid, prodAbs);
        else bumpMae(maeHigh, prodAbs);
        if (id == kArchivedProbeId) { archivedProbeErr = prodErr; archivedProbeSeen = true; }

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
        settings.calibration()->addSawLearningPoint(drip, flow, scale, overshoot, profile, basket);
    }

    out << "\n=== Production MAE per flow bucket (the actual answer) ===\n";
    out << "basket_segment=" << (ignoreBasket ? "ignored" : "used");
    if (!ignoreBasket && rowsWithoutBasket > 0) {
        out << "\t(WARNING: " << rowsWithoutBasket << " of " << shotsArr.size()
            << " corpus rows carry no basket key and resolved to the process default, so "
               "they share one pool)";
    }
    out << "\n";
    out << "bucket\tn\tmae\tworst\n";
    auto reportBucket = [&out](const QString& name, const MaeBucket& b) {
        const double mae = b.n ? b.absSum / b.n : 0.0;
        out << name << "\t" << b.n << "\t" << mae << "\t" << b.worst << "\n";
    };
    reportBucket(QStringLiteral("overall"), maeOverall);
    reportBucket(QStringLiteral("low (<1.5)"), maeLow);
    reportBucket(QStringLiteral("mid [1.5,3)"), maeMid);
    reportBucket(QStringLiteral("high (>=3)"), maeHigh);
    if (archivedProbeSeen) {
        out << "archived_shot887_signed_error=" << archivedProbeErr
            << "\t(corpus id " << kArchivedProbeId << ")\n";
    }

    if (!haveSim) {
        wipeAllSawState(settings);
        return 0;
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
