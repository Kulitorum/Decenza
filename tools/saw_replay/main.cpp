// saw_replay — Phase 0 simulator for the SAW prediction model proposal.
//
// Replays a chronologically-ordered corpus of historical shots through both
// the OLD weighted-average prediction model and one of three regression-based
// candidate models (linear, mad, lowess). Reports per-shot predictions,
// aggregate MAE per flow bucket, and (in pair-mode) MAE binned by shot-index-
// within-pair so the proposal's speed claim ("pair-specific predictions in 2
// shots, not 10") can be evaluated separately from the model-quality claim.
//
// All math is a stand-alone port — no link against src/core/settings.cpp,
// no QSettings, no BLE/QML. Build target lives in the root CMakeLists.txt.
//
// Usage:
//   ./saw_replay --corpus PATH [--variant {linear,mad,lowess,old}]
//                              [--mode {legacy,warmup}]
//                              [--sigma F]
//                              [--recency-max F]
//                              [--recency-min-converged F]
//                              [--recency-min-unconverged F]
//
// --variant chooses the candidate model to compare against OLD.
// --mode chooses the per-pair source-selection logic:
//   legacy: production behavior — pair must commit ≥2 medians (≥10 shots)
//           before its history is used; before that, predictions come from
//           a smart bootstrap pool over all same-scale pairs.
//   warmup: proposed behavior — when ≥2 entries exist in the pair's pending
//           batch, fit on those; pair-specific predictions begin at shot 2
//           of a brand-new pair instead of shot 11.
//
// Tuning flags override OLD's defaults so a parameter sweep can compare
// {sigma, recency} configurations against the baseline OLD on the same corpus.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace {

struct Entry {
    double drip = 0.0;
    double flow = 0.0;
    QString scale;
    double overshoot = 0.0;
    qint64 ts = 0;  // unused for ordering — pool is appended in chronological order
};

struct Shot {
    int id = 0;
    QString ts;
    QString profile;
    QString scale;
    double flow = 0.0;
    double drip = 0.0;
    double overshoot = 0.0;
    double livePredicted = 0.0;
};

struct Tuning {
    double sigma = 1.5;
    double recencyMax = 10.0;
    double recencyMinConverged = 3.0;
    double recencyMinUnconverged = 1.0;
};

// --- Scale lag table (de1app reference) ---------------------------------
double sensorLag(const QString& scaleType) {
    if (scaleType == "Bookoo") return 0.50;
    if (scaleType == "Acaia" || scaleType == "Acaia Pyxis") return 0.69;
    if (scaleType == "Felicita") return 0.50;
    if (scaleType == "Atomheart Eclair") return 0.50;
    if (scaleType == "Hiroia Jimmy") return 0.25;
    if (scaleType == "Decent Scale" || scaleType == "Skale" || scaleType == "decent") return 0.38;
    return 0.38;
}

// --- Convergence test (mirrors Settings::isSawConverged) ----------------
// Operates on a list of entries assumed already filtered to one scale type.
bool isConvergedEntries(const QVector<Entry>& entries) {
    QVector<double> overshoots;
    for (qsizetype i = entries.size() - 1; i >= 0 && overshoots.size() < 5; --i) {
        overshoots.append(std::abs(entries[i].overshoot));
    }
    if (overshoots.size() < 3) return false;
    double sum = 0.0;
    for (double v : overshoots) sum += v;
    bool converged = (sum / overshoots.size()) < 1.5;
    if (converged) {
        QVector<double> signedOver;
        for (qsizetype i = entries.size() - 1; i >= 0 && signedOver.size() < 3; --i) {
            signedOver.append(entries[i].overshoot);
        }
        if (signedOver.size() >= 3) {
            bool allPos = true, allNeg = true;
            for (double v : signedOver) {
                if (v <= 1.0) allPos = false;
                if (v >= -1.0) allNeg = false;
            }
            if (allPos || allNeg) converged = false;
        }
    }
    return converged;
}

// --- OLD model: recency × Gaussian-flow-similarity weighted average -----
// Caller passes the entries list already filtered to one scale, ordered
// newest-first. Sigma controls the Gaussian width; default 1.5 ml/s
// matches production. Returns 0 to signal "no usable data" (caller falls back).
double predictOldOnEntries(const QVector<Entry>& entries, double flow,
                           double sigma, double recencyMax, double recencyMin) {
    if (entries.isEmpty()) return 0.0;
    const qsizetype n = entries.size();
    const double divisor = 2.0 * sigma * sigma;  // = 4.5 when σ=1.5
    double weightedSum = 0.0;
    double totalWeight = 0.0;
    for (qsizetype i = 0; i < n; ++i) {
        const Entry& e = entries[i];
        const double recency = (n == 1)
            ? recencyMax
            : recencyMax - i * (recencyMax - recencyMin) / (n - 1);
        const double flowDiff = e.flow - flow;
        const double flowWeight = std::exp(-(flowDiff * flowDiff) / divisor);
        const double w = recency * flowWeight;
        weightedSum += e.drip * w;
        totalWeight += w;
    }
    if (totalWeight < 0.01) return 0.0;
    return std::clamp(weightedSum / totalWeight, 0.5, 20.0);
}

// --- Regression helpers (used by linear/mad/lowess variants) ------------
struct RegressionFit {
    bool ok = false;
    double a = 0.0;
    double b = 0.0;
};

RegressionFit fitWeightedRegression(const QVector<Entry>& entries,
                                    const QVector<double>& weights) {
    RegressionFit fit;
    if (entries.size() < 2 || entries.size() != weights.size()) return fit;
    double Sw = 0, Swf = 0, Swd = 0, Swff = 0, Swfd = 0;
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const double w = weights[i];
        const double f = entries[i].flow;
        const double d = entries[i].drip;
        Sw   += w;
        Swf  += w * f;
        Swd  += w * d;
        Swff += w * f * f;
        Swfd += w * f * d;
    }
    if (Sw < 0.01) return fit;
    const double denom = Sw * Swff - Swf * Swf;
    if (std::abs(denom) < 1e-3) return fit;
    fit.a = (Sw * Swfd - Swf * Swd) / denom;
    fit.b = (Swd - fit.a * Swf) / Sw;
    fit.a = std::clamp(fit.a, 0.0, 5.0);
    fit.b = std::clamp(fit.b, -2.0, 2.0);
    fit.ok = true;
    return fit;
}

QVector<double> linearRecencyWeights(qsizetype n, double recencyMax, double recencyMin) {
    QVector<double> w(n);
    for (qsizetype i = 0; i < n; ++i) {
        w[i] = (n == 1) ? recencyMax
                        : recencyMax - i * (recencyMax - recencyMin) / (n - 1);
    }
    return w;
}

QVector<double> lowessWeights(const QVector<Entry>& entries, double currentFlow,
                              double sigma, double recencyMax, double recencyMin) {
    const qsizetype n = entries.size();
    QVector<double> w(n);
    const double divisor = 2.0 * sigma * sigma;
    for (qsizetype i = 0; i < n; ++i) {
        const double recency = (n == 1) ? recencyMax
                                        : recencyMax - i * (recencyMax - recencyMin) / (n - 1);
        const double diff = entries[i].flow - currentFlow;
        const double gauss = std::exp(-(diff * diff) / divisor);
        w[i] = recency * gauss;
    }
    return w;
}

QVector<Entry> rejectOutliersMad(const QVector<Entry>& entries, double k = 2.5) {
    if (entries.size() < 5) return entries;
    QVector<double> drips;
    drips.reserve(entries.size());
    for (const auto& e : entries) drips.append(e.drip);
    std::sort(drips.begin(), drips.end());
    const double medianDrip = drips[drips.size() / 2];
    QVector<double> deviations;
    deviations.reserve(drips.size());
    for (double d : drips) deviations.append(std::abs(d - medianDrip));
    std::sort(deviations.begin(), deviations.end());
    const double mad = deviations[deviations.size() / 2];
    if (mad < 0.01) return entries;
    const double threshold = k * mad * 1.4826;
    QVector<Entry> filtered;
    filtered.reserve(entries.size());
    for (const auto& e : entries) {
        if (std::abs(e.drip - medianDrip) <= threshold) filtered.append(e);
    }
    if (filtered.size() < 4) return entries;
    return filtered;
}

// --- Per-pair state ------------------------------------------------------
// Mirrors Settings::addSawPerPairEntry's pending-batch accumulator and
// committed-median history. We skip the IQR-rejection step since the corpus
// is already-observed real shots and we want to use all of them.
struct PairState {
    QVector<Entry> pendingBatch;     // capped at 5 (commits a median when full)
    QVector<Entry> committedMedians; // capped at 10
    int totalShots = 0;              // shot index within this pair, 1-based
};

constexpr int kBatchSize = 5;
constexpr int kMaxPairHistory = 10;
constexpr int kSawMinMediansForGraduation = 2;
constexpr int kBootstrapCap = 12;

// Append a shot's entry to its pair's state. When the pending batch fills,
// commit a median to the per-pair history. Mirrors the production logic.
void updatePairState(QHash<QString, PairState>& pairStates, const QString& key,
                     const Entry& e) {
    PairState& ps = pairStates[key];
    ps.pendingBatch.append(e);
    ps.totalShots += 1;
    if (ps.pendingBatch.size() >= kBatchSize) {
        QVector<double> drips, flows, overs;
        for (const auto& x : ps.pendingBatch) {
            drips.append(x.drip);
            flows.append(x.flow);
            overs.append(x.overshoot);
        }
        std::sort(drips.begin(), drips.end());
        std::sort(flows.begin(), flows.end());
        std::sort(overs.begin(), overs.end());
        Entry medianEntry;
        medianEntry.drip = drips[drips.size() / 2];
        medianEntry.flow = flows[flows.size() / 2];
        medianEntry.overshoot = overs[overs.size() / 2];
        medianEntry.scale = e.scale;
        medianEntry.ts = e.ts;
        ps.committedMedians.append(medianEntry);
        if (ps.committedMedians.size() > kMaxPairHistory) {
            ps.committedMedians.removeFirst();
        }
        ps.pendingBatch.clear();
    }
}

// Build the smart-bootstrap pool: for each pair sharing the requested scale,
// take the most recent committed median plus all pending-batch entries.
// Mirrors the proposal's smart-bootstrap concept (also includes legacy global
// pool entries — the current production behavior aggregates `saw/learningHistory`).
QVector<Entry> buildBootstrapPool(const QHash<QString, PairState>& pairStates,
                                  const QVector<Entry>& legacyGlobalPool,
                                  const QString& scale) {
    QVector<Entry> pool;
    for (auto it = pairStates.constBegin(); it != pairStates.constEnd(); ++it) {
        const PairState& ps = it.value();
        if (!ps.committedMedians.isEmpty()
            && ps.committedMedians.last().scale == scale) {
            pool.append(ps.committedMedians.last());
        }
        for (const auto& e : ps.pendingBatch) {
            if (e.scale == scale) pool.append(e);
        }
    }
    for (const auto& e : legacyGlobalPool) {
        if (e.scale == scale) pool.append(e);
    }
    if (pool.size() > kBootstrapCap) {
        pool = pool.mid(pool.size() - kBootstrapCap);
    }
    std::reverse(pool.begin(), pool.end());  // newest-first for predictOldOnEntries
    return pool;
}

// --- Per-pair source selection ------------------------------------------
struct PredictResult {
    double drip = 0.0;
    QString source;  // "perPair" | "pendingBatch" | "globalBootstrap" | "scaleDefault"
    int nUsed = 0;
};

PredictResult predictWithMode(const QString& pairKey, const QString& scale, double flow,
                              const QHash<QString, PairState>& pairStates,
                              const QVector<Entry>& legacyGlobalPool,
                              const QString& mode, const Tuning& t,
                              int warmupThreshold) {
    PredictResult r;

    auto it = pairStates.find(pairKey);

    // 1. Per-pair graduated path
    // NOTE: production's Settings::getExpectedDripFor hardcodes recencyMin=3.0
    // in the perPair branch (no convergence check). Match that exactly here so
    // saw_parity passes — applying isConvergedEntries here would diverge from
    // production. The convergence-aware recency only applies to the global-pool
    // path in Settings::getExpectedDrip.
    if (it != pairStates.end()
        && it->committedMedians.size() >= kSawMinMediansForGraduation) {
        QVector<Entry> entries;
        for (qsizetype i = it->committedMedians.size() - 1; i >= 0 && entries.size() < 12; --i) {
            entries.append(it->committedMedians[i]);
        }
        const double pred = predictOldOnEntries(entries, flow, t.sigma, t.recencyMax,
                                                t.recencyMinConverged);
        if (pred > 0.0) {
            r.drip = pred;
            r.source = "perPair";
            r.nUsed = static_cast<int>(entries.size());
            return r;
        }
    }

    // 2. Pending-batch warm-up (only in warmup mode)
    if (mode == "warmup" && it != pairStates.end()
        && static_cast<int>(it->pendingBatch.size()) >= warmupThreshold) {
        QVector<Entry> entries;
        for (qsizetype i = it->pendingBatch.size() - 1; i >= 0; --i) {
            entries.append(it->pendingBatch[i]);
        }
        const double pred = predictOldOnEntries(entries, flow, t.sigma, t.recencyMax,
                                                t.recencyMinUnconverged);
        if (pred > 0.0) {
            r.drip = pred;
            r.source = "pendingBatch";
            r.nUsed = static_cast<int>(entries.size());
            return r;
        }
    }

    // 3. Smart bootstrap pool
    QVector<Entry> bootstrap = buildBootstrapPool(pairStates, legacyGlobalPool, scale);
    if (bootstrap.size() >= 2) {
        const double pred = predictOldOnEntries(bootstrap, flow, t.sigma, t.recencyMax,
                                                t.recencyMinUnconverged);
        if (pred > 0.0) {
            r.drip = pred;
            r.source = "globalBootstrap";
            r.nUsed = static_cast<int>(bootstrap.size());
            return r;
        }
    }

    // 4. Scale sensor-lag fallback
    r.drip = std::min(flow * (sensorLag(scale) + 0.1), 8.0);
    r.source = "scaleDefault";
    r.nUsed = 0;
    return r;
}

// Variant prediction for the same pair-aware entry set (used when --variant
// is one of linear/mad/lowess to compare a regression model against OLD on
// the same entries OLD would see).
struct VariantResult {
    double drip = 0.0;
    double a = 0.0;
    double b = 0.0;
    int rejected = 0;
    QString source;  // "regression" | "lagFallback"
};

VariantResult predictVariantOnEntries(QVector<Entry> entries, const QString& scale,
                                      double flow, const QString& variant, const Tuning& t,
                                      bool converged) {
    VariantResult v;
    if (variant == "old") {
        v.drip = predictOldOnEntries(entries, flow, t.sigma, t.recencyMax,
                                     converged ? t.recencyMinConverged : t.recencyMinUnconverged);
        if (v.drip == 0.0) {
            v.drip = std::min(flow * (sensorLag(scale) + 0.1), 8.0);
            v.source = "lagFallback";
        } else {
            v.source = "regression";  // misnomer here; means "model fired"
        }
        return v;
    }

    if (variant == "mad") {
        const qsizetype before = entries.size();
        entries = rejectOutliersMad(entries);
        v.rejected = static_cast<int>(before - entries.size());
    }
    const double rmin = converged ? t.recencyMinConverged : t.recencyMinUnconverged;
    QVector<double> weights = (variant == "lowess")
        ? lowessWeights(entries, flow, t.sigma, t.recencyMax, rmin)
        : linearRecencyWeights(entries.size(), t.recencyMax, rmin);
    const auto fit = fitWeightedRegression(entries, weights);
    if (!fit.ok) {
        v.drip = std::min(flow * (sensorLag(scale) + 0.1), 8.0);
        v.source = "lagFallback";
        return v;
    }
    v.a = fit.a;
    v.b = fit.b;
    v.drip = std::clamp(fit.a * flow + fit.b, 0.5, 20.0);
    v.source = "regression";
    return v;
}

QString flowBucket(double flow) {
    if (flow < 1.5) return "low";
    if (flow < 3.0) return "mid";
    return "high";
}

QString withinPairBucket(int idx) {
    if (idx == 1) return "shot1";
    if (idx <= 5) return "shots2-5";
    if (idx <= 10) return "shots6-10";
    return "shots11+";
}

// Returns the pair-aware entry set the given mode would use for OLD, so the
// variant comparison can be apples-to-apples against the same data.
QVector<Entry> entriesForVariant(const QString& pairKey, const QString& scale,
                                 const QHash<QString, PairState>& pairStates,
                                 const QVector<Entry>& legacyGlobalPool,
                                 const QString& mode, int warmupThreshold) {
    auto it = pairStates.find(pairKey);
    if (it != pairStates.end()
        && it->committedMedians.size() >= kSawMinMediansForGraduation) {
        QVector<Entry> entries;
        for (qsizetype i = it->committedMedians.size() - 1; i >= 0 && entries.size() < 12; --i) {
            entries.append(it->committedMedians[i]);
        }
        return entries;
    }
    if (mode == "warmup" && it != pairStates.end()
        && static_cast<int>(it->pendingBatch.size()) >= warmupThreshold) {
        QVector<Entry> entries;
        for (qsizetype i = it->pendingBatch.size() - 1; i >= 0; --i) {
            entries.append(it->pendingBatch[i]);
        }
        return entries;
    }
    return buildBootstrapPool(pairStates, legacyGlobalPool, scale);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("saw_replay");

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption corpusOpt({"c", "corpus"}, "Path to corpus JSON file.", "path");
    parser.addOption(corpusOpt);
    QCommandLineOption variantOpt({"v", "variant"},
        "Variant to compare against OLD: 'linear', 'mad', 'lowess', or 'old' (sanity-check).",
        "name", "old");
    parser.addOption(variantOpt);
    QCommandLineOption modeOpt({"m", "mode"},
        "Source-selection mode: 'legacy' (production) or 'warmup' (pending-batch ≥2 path).",
        "name", "legacy");
    parser.addOption(modeOpt);
    QCommandLineOption sigmaOpt("sigma", "Gaussian flow-similarity σ (ml/s, default 1.5).",
                                "value", "1.5");
    parser.addOption(sigmaOpt);
    QCommandLineOption rmaxOpt("recency-max", "Recency weight for newest entry (default 10).",
                               "value", "10.0");
    parser.addOption(rmaxOpt);
    QCommandLineOption rminCOpt("recency-min-converged",
        "Recency weight for oldest entry when converged (default 3).", "value", "3.0");
    parser.addOption(rminCOpt);
    QCommandLineOption rminUOpt("recency-min-unconverged",
        "Recency weight for oldest entry when unconverged (default 1).", "value", "1.0");
    parser.addOption(rminUOpt);
    QCommandLineOption tagOpt("tag", "Optional run tag (printed in summary header).", "string", "");
    parser.addOption(tagOpt);
    QCommandLineOption warmupThreshOpt("warmup-threshold",
        "Min pending-batch entries before warmup fires (default 2).", "value", "2");
    parser.addOption(warmupThreshOpt);
    parser.process(app);

    if (!parser.isSet(corpusOpt)) {
        qWarning() << "--corpus <path> is required";
        return 2;
    }
    const QString variant = parser.value(variantOpt);
    if (variant != "linear" && variant != "mad" && variant != "lowess" && variant != "old") {
        qWarning() << "--variant must be linear|mad|lowess|old, got" << variant;
        return 2;
    }
    const QString mode = parser.value(modeOpt);
    if (mode != "legacy" && mode != "warmup") {
        qWarning() << "--mode must be legacy|warmup, got" << mode;
        return 2;
    }

    Tuning t;
    t.sigma = parser.value(sigmaOpt).toDouble();
    t.recencyMax = parser.value(rmaxOpt).toDouble();
    t.recencyMinConverged = parser.value(rminCOpt).toDouble();
    t.recencyMinUnconverged = parser.value(rminUOpt).toDouble();
    const QString tag = parser.value(tagOpt);
    const int warmupThreshold = parser.value(warmupThreshOpt).toInt();
    if (warmupThreshold < 2 || warmupThreshold > kBatchSize) {
        qWarning() << "--warmup-threshold must be in [2," << kBatchSize << "], got" << warmupThreshold;
        return 2;
    }

    // --- Load corpus ---------------------------------------------------
    QFile f(parser.value(corpusOpt));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "cannot open" << f.fileName();
        return 2;
    }
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "json parse error:" << err.errorString();
        return 2;
    }
    auto shotsArr = doc.object().value("shots").toArray();
    if (shotsArr.isEmpty()) {
        qWarning() << "no shots in corpus";
        return 2;
    }
    QVector<Shot> shots;
    shots.reserve(shotsArr.size());
    for (const auto& v : shotsArr) {
        auto o = v.toObject();
        Shot s;
        s.id = o.value("id").toInt();
        s.ts = o.value("ts").toString();
        s.profile = o.value("profile").toString();
        s.scale = o.value("scale").toString();
        s.flow = o.value("flow").toDouble();
        s.drip = o.value("drip").toDouble();
        s.overshoot = o.value("overshoot").toDouble();
        s.livePredicted = o.value("live_predicted").toDouble();
        shots.append(s);
    }

    QTextStream out(stdout);
    out.setRealNumberPrecision(4);
    out.setRealNumberNotation(QTextStream::FixedNotation);

    out << "# tag=" << (tag.isEmpty() ? "(none)" : tag)
        << " variant=" << variant << " mode=" << mode
        << " sigma=" << t.sigma
        << " recency=" << t.recencyMax << "→[" << t.recencyMinConverged << "/" << t.recencyMinUnconverged << "]"
        << "\n";
    out << "shot_id\tpair_idx\tflow\tactual\told_pred\tnew_pred\told_err\tnew_err\tsource\tbucket\n";

    // --- Aggregate accumulators ----------------------------------------
    struct Bucket {
        double oldAbsSum = 0, newAbsSum = 0;
        double oldWorst = 0, newWorst = 0;
        int n = 0;
    };
    Bucket overall, low, mid, high;
    QHash<QString, Bucket> withinPair;     // shot1 / shots2-5 / shots6-10 / shots11+
    QHash<QString, Bucket> bySource;       // perPair / pendingBatch / globalBootstrap / scaleDefault

    auto upd = [&](Bucket& b, double oldAbs, double newAbs) {
        b.oldAbsSum += oldAbs;
        b.newAbsSum += newAbs;
        b.oldWorst = std::max(b.oldWorst, oldAbs);
        b.newWorst = std::max(b.newWorst, newAbs);
        b.n += 1;
    };

    // --- Per-pair simulation -------------------------------------------
    QHash<QString, PairState> pairStates;
    QVector<Entry> legacyGlobalPool;  // mirrors saw/learningHistory; appended every shot, capped at 50

    int clampHits = 0;
    int newBeatsOld = 0;

    for (const auto& s : shots) {
        const QString pairKey = s.profile + QStringLiteral("::") + s.scale;
        const int pairIdxBefore = pairStates.value(pairKey).totalShots + 1;

        // OLD prediction (always the OLD model, regardless of --variant)
        const PredictResult oldR = predictWithMode(pairKey, s.scale, s.flow,
                                                   pairStates, legacyGlobalPool, mode, t,
                                                   warmupThreshold);

        // NEW prediction: same entry set OLD would use, but run through the chosen variant
        const QVector<Entry> entries = entriesForVariant(pairKey, s.scale,
                                                          pairStates, legacyGlobalPool, mode,
                                                          warmupThreshold);
        const bool conv = !entries.isEmpty() && isConvergedEntries(entries);
        VariantResult newR;
        if (entries.isEmpty()) {
            newR.drip = std::min(s.flow * (sensorLag(s.scale) + 0.1), 8.0);
            newR.source = "lagFallback";
        } else {
            newR = predictVariantOnEntries(entries, s.scale, s.flow, variant, t, conv);
        }

        const double oldErr = oldR.drip - s.drip;
        const double newErr = newR.drip - s.drip;
        const double oldAbs = std::abs(oldErr);
        const double newAbs = std::abs(newErr);

        out << s.id << "\t" << pairIdxBefore << "\t" << s.flow << "\t" << s.drip << "\t"
            << oldR.drip << "\t" << newR.drip << "\t"
            << oldErr << "\t" << newErr << "\t"
            << oldR.source << "\t" << flowBucket(s.flow) << "\n";

        upd(overall, oldAbs, newAbs);
        const QString fbk = flowBucket(s.flow);
        if (fbk == "low") upd(low, oldAbs, newAbs);
        else if (fbk == "mid") upd(mid, oldAbs, newAbs);
        else upd(high, oldAbs, newAbs);

        upd(withinPair[withinPairBucket(pairIdxBefore)], oldAbs, newAbs);
        upd(bySource[oldR.source], oldAbs, newAbs);

        if (newR.source == "regression"
            && (newR.a == 0.0 || newR.a == 5.0 || newR.b == -2.0 || newR.b == 2.0)) {
            ++clampHits;
        }
        if (newAbs < oldAbs) ++newBeatsOld;

        // Update state for next shot
        Entry e;
        e.drip = s.drip;
        e.flow = s.flow;
        e.scale = s.scale;
        e.overshoot = s.overshoot;
        updatePairState(pairStates, pairKey, e);
        legacyGlobalPool.append(e);
        if (legacyGlobalPool.size() > 50) legacyGlobalPool.removeFirst();
    }

    auto maeOld = [](const Bucket& b) { return b.n ? b.oldAbsSum / b.n : 0.0; };
    auto maeNew = [](const Bucket& b) { return b.n ? b.newAbsSum / b.n : 0.0; };

    out << "\n=== Aggregate by flow bucket ===\n";
    out << "bucket\tn\told_mae\tnew_mae\told_worst\tnew_worst\tdelta_mae\n";
    auto row = [&](const QString& name, const Bucket& b) {
        out << name << "\t" << b.n << "\t" << maeOld(b) << "\t" << maeNew(b)
            << "\t" << b.oldWorst << "\t" << b.newWorst
            << "\t" << (maeNew(b) - maeOld(b)) << "\n";
    };
    row("overall", overall);
    row("low (<1.5)", low);
    row("mid [1.5,3)", mid);
    row("high (>=3)", high);

    out << "\n=== Aggregate by within-pair shot index ===\n";
    out << "bucket\tn\told_mae\tnew_mae\n";
    for (const auto& key : {QStringLiteral("shot1"), QStringLiteral("shots2-5"),
                            QStringLiteral("shots6-10"), QStringLiteral("shots11+")}) {
        const Bucket& b = withinPair[key];
        out << key << "\t" << b.n << "\t" << maeOld(b) << "\t" << maeNew(b) << "\n";
    }

    out << "\n=== Aggregate by source (selected by --mode) ===\n";
    out << "source\tn\told_mae\n";
    for (const auto& key : {QStringLiteral("perPair"), QStringLiteral("pendingBatch"),
                            QStringLiteral("globalBootstrap"), QStringLiteral("scaleDefault")}) {
        const Bucket& b = bySource[key];
        out << key << "\t" << b.n << "\t" << maeOld(b) << "\n";
    }

    out << "\nclamp_hits=" << clampHits << " of " << overall.n
        << " | shots_where_new_beats_old=" << newBeatsOld << " of " << overall.n << "\n";
    out.flush();
    return 0;
}
