#include "shotcomparison.h"

#include "shotprojection.h"
#include "../ai/dialing_helpers.h"
#include "../ai/shotsummarizer.h"
#include "../core/diagnosticlogging.h"
#include "../core/grinderaliases.h"
#include "../profile/profile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointF>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace ShotComparison {

bool sameGrinderSetting(const QString& aRaw, const QString& bRaw, double tolerance)
{
    const QString a = aRaw.trimmed();
    const QString b = bRaw.trimmed();
    if (a.isEmpty() || b.isEmpty()) return true;
    if (a == b) return true;

    // Compound settings compare by key. Compound against plain ("1+4" against
    // "4") names two different positions on a compound dial, so it is a change.
    const QString aKey = GrinderAliases::compoundKey(a);
    const QString bKey = GrinderAliases::compoundKey(b);
    if (!aKey.isEmpty() || !bKey.isEmpty())
        return aKey == bKey;

    const std::optional<double> an = GrinderAliases::leadingDialNumber(a);
    const std::optional<double> bn = GrinderAliases::leadingDialNumber(b);
    if (!an || !bn) {
        // Lettered dials ("3F" vs "3C") have no number but are still two real
        // settings that differ. Free text is not a setting, so stays "same".
        if (GrinderAliases::looksLikeSetting(a) && GrinderAliases::looksLikeSetting(b))
            return false;
        return true;
    }
    return std::abs(*an - *bn) <= tolerance + 1e-9;
}

double effectiveTargetWeightG(const ShotProjection& shot)
{
    if (shot.targetWeightG > 0)
        return shot.targetWeightG;
    if (shot.profileJson.isEmpty())
        return 0.0;
    // Only shots imported from de1app / visualizer.coffee reach here, the cohort
    // most likely to carry malformed JSON, so a parse failure is reported.
    QJsonParseError err{};
    const QJsonObject profileObj = QJsonDocument::fromJson(shot.profileJson.toUtf8(), &err).object();
    if (err.error != QJsonParseError::NoError) {
        DIAG_WARN(SHOT, "ShotComparison") << "effectiveTargetWeightG: profileJson parse failed for shot"
                                          << shot.id << ":" << err.errorString();
        return 0.0;
    }
    const QJsonValue tw = profileObj["target_weight"];
    const double twVal = tw.isString() ? tw.toString().toDouble() : tw.toDouble();
    return twVal > 0 ? twVal : 0.0;
}

const InputField* InputDiff::field(const QString& key) const
{
    for (const InputField& f : fields)
        if (f.key == key) return &f;
    return nullptr;
}

bool InputDiff::changed(const QString& key) const
{
    const InputField* f = field(key);
    return f && f->state == FieldState::Changed;
}

bool InputDiff::anyDifference() const
{
    return std::any_of(fields.cbegin(), fields.cend(),
                       [](const InputField& f) { return f.state != FieldState::Same; });
}

namespace {

InputField numericField(const QString& key, const QString& unit,
                        double a, double b, double tolerance)
{
    InputField f;
    f.key = key;
    f.unit = unit;
    f.numeric = true;
    f.dialIn = true;
    f.baseValue = a;
    f.shotValue = b;
    const bool hasA = a > 0, hasB = b > 0;
    if (hasA != hasB) {
        f.state = FieldState::OneSided;
    } else if (hasA && std::abs(a - b) > tolerance + 1e-9) {
        f.state = FieldState::Changed;
        f.delta = b - a;
    }
    return f;
}

FieldState textState(const QString& a, const QString& b)
{
    const QString ta = a.trimmed(), tb = b.trimmed();
    if (ta.isEmpty() != tb.isEmpty()) return FieldState::OneSided;
    return ta.compare(tb, Qt::CaseInsensitive) == 0 ? FieldState::Same : FieldState::Changed;
}

FieldState strongest(FieldState a, FieldState b)
{
    auto rank = [](FieldState s) { return s == FieldState::Changed ? 2 : s == FieldState::OneSided ? 1 : 0; };
    return rank(a) >= rank(b) ? a : b;
}

// A displayed input made of one or more shot fields: the grinder is brand and
// model, a bean is brand and type. Identity members are named by their key in
// ShotIdentity::fields(), so that table stays the one list of identity fields.
struct InputGroup {
    const char* key;
    QList<QString ShotProjection::*> members;
};

const QList<InputGroup>& groups()
{
    static const QList<InputGroup> g = {
        { "grinder", { &ShotProjection::grinderBrand, &ShotProjection::grinderModel } },
        { "burrs",   { &ShotProjection::grinderBurrs } },
        { "basket",  { &ShotProjection::basketBrand, &ShotProjection::basketModel } },
        { "puckPrep",{ &ShotProjection::puckPrep } },
        { "bean",    { &ShotProjection::beanBrand, &ShotProjection::beanType } },
        { "roast",   { &ShotProjection::roastLevel, &ShotProjection::roastDate } },
    };
    return g;
}

InputField groupField(const QString& key, const QList<QString ShotProjection::*>& members,
                      const ShotProjection& base, const ShotProjection& shot)
{
    InputField f;
    f.key = key;
    QStringList a, b;
    for (auto m : members) {
        f.state = strongest(f.state, textState(base.*m, shot.*m));
        if (!(base.*m).trimmed().isEmpty()) a << (base.*m).trimmed();
        if (!(shot.*m).trimmed().isEmpty()) b << (shot.*m).trimmed();
    }
    f.baseText = a.join(QLatin1Char(' '));
    f.shotText = b.join(QLatin1Char(' '));
    return f;
}

double roundTo(double v, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    return std::round(v * scale) / scale;
}

// The Δ between the two values as displayed, so 0.8 beside 0.9 always shows 0.1 and
// two equal-looking values never carry a Δ.
double shownDelta(double base, double shot, int decimals)
{
    return roundTo(roundTo(shot, decimals) - roundTo(base, decimals), decimals);
}

} // namespace

InputDiff diffInputs(const ShotProjection& base, const ShotProjection& shot)
{
    InputDiff d;

    InputField profile;
    profile.key = QStringLiteral("profile");
    profile.baseText = base.profileName;
    profile.shotText = shot.profileName;
    profile.state = textState(base.profileName, shot.profileName);
    profile.dialIn = true;
    d.fields << profile;

    d.fields << numericField(QStringLiteral("temperatureOverrideC"), QStringLiteral("celsius"),
                             base.temperatureOverrideC, shot.temperatureOverrideC, kTemperatureToleranceC);
    d.fields << numericField(QStringLiteral("doseG"), QStringLiteral("g"),
                             base.doseWeightG, shot.doseWeightG, kDoseToleranceG);
    d.fields << numericField(QStringLiteral("targetYieldG"), QStringLiteral("g"),
                             effectiveTargetWeightG(base), effectiveTargetWeightG(shot), kYieldToleranceG);

    InputField grind;
    grind.key = QStringLiteral("grinderSetting");
    grind.dialIn = true;
    grind.baseText = base.grinderSetting.trimmed();
    grind.shotText = shot.grinderSetting.trimmed();
    if (grind.baseText.isEmpty() != grind.shotText.isEmpty())
        grind.state = FieldState::OneSided;
    else if (!sameGrinderSetting(grind.baseText, grind.shotText, 0.0))
        grind.state = FieldState::Changed;
    d.fields << grind;

    d.fields << numericField(QStringLiteral("rpm"), QStringLiteral("rpm"),
                             double(base.rpm), double(shot.rpm), kRpmTolerance);

    QList<QString ShotProjection::*> grouped;
    for (const InputGroup& g : groups()) {
        d.fields << groupField(QLatin1String(g.key), g.members, base, shot);
        grouped << g.members;
    }
    // Any identity field no group names still shows, under its own key.
    for (const auto& f : DialingHelpers::ShotIdentity::fields()) {
        if (grouped.contains(f.source)) continue;
        d.fields << groupField(QLatin1String(f.key), { f.source }, base, shot);
    }

    d.fields << groupField(QStringLiteral("barista"), { &ShotProjection::barista }, base, shot);

    // A grind Δ needs one dial: the same grinder and burrs on both shots.
    InputField* g = nullptr;
    for (InputField& f : d.fields)
        if (f.key == QLatin1String("grinderSetting")) g = &f;
    const InputField* grinder = d.field(QStringLiteral("grinder"));
    const InputField* burrs = d.field(QStringLiteral("burrs"));
    // And one notation: a number read from "1+4" and one from "4" are not on one scale.
    if (g && g->state == FieldState::Changed
            && grinder && grinder->state == FieldState::Same
            && burrs && burrs->state == FieldState::Same
            && GrinderAliases::compoundKey(g->baseText).isEmpty()
                   == GrinderAliases::compoundKey(g->shotText).isEmpty()) {
        const auto a = GrinderAliases::leadingDialNumber(g->baseText);
        const auto b = GrinderAliases::leadingDialNumber(g->shotText);
        if (a && b) {
            g->numeric = true;
            g->baseValue = *a;
            g->shotValue = *b;
            g->delta = *b - *a;
        }
    }
    return d;
}

const QList<MetricDef>& metricDefs()
{
    static const QList<MetricDef> defs = {
        { "durationSec",           "s",            1, false, 1.0  },
        { "yieldG",                "g",            1, false, 1.0  },
        { "ratio",                 "",             1, false, 0.1  },
        { "firstDropSec",          "s",            1, false, 0.5  },
        { "peakPressureBar",       "bar",          1, false, 0.3  },
        { "meanFlowMlPerSec",      "mlPerSec",     2, false, 0.15 },
        { "peakFlowMlPerSec",      "mlPerSec",     1, true,  0.3  },
        { "weightFlowGPerSec",     "gPerSec",      2, true,  0.15 },
        { "groupTempC",            "celsius",      1, true,  0.5  },
        { "tempSagC",              "celsiusDelta", 1, true,  0.5  },
        { "resistance",            "",             2, true,  0.3  },
        { "preinfusionSec",        "s",            1, true,  0.5  },
        // Shot time minus preinfusion: naming it beside those repeats them.
        { "pourSec",               "s",            1, true,  0.0  },
        { "drinkTdsPct",           "percent",      2, true,  0.1  },
        { "drinkEyPct",            "percent",      1, true,  0.5  },
    };
    return defs;
}

QMap<QString, double> metricsFor(const ShotProjection& shot)
{
    QMap<QString, double> m;
    const double duration = shot.durationSec;
    if (duration > 0) m[QStringLiteral("durationSec")] = duration;
    if (shot.finalWeightG > 0) m[QStringLiteral("yieldG")] = shot.finalWeightG;
    if (shot.finalWeightG > 0 && shot.doseWeightG > 0)
        m[QStringLiteral("ratio")] = shot.finalWeightG / shot.doseWeightG;
    if (shot.drinkTdsPct > 0) m[QStringLiteral("drinkTdsPct")] = shot.drinkTdsPct;
    if (shot.drinkEyPct > 0) m[QStringLiteral("drinkEyPct")] = shot.drinkEyPct;

    const QVector<QPointF> pressure = curveToPoints(shot.pressure);
    const QVector<QPointF> flow = curveToPoints(shot.flow);
    const QVector<QPointF> temperature = curveToPoints(shot.temperature);
    const QVector<QPointF> weight = curveToPoints(shot.weight);
    const QVector<QPointF> resistance = curveToPoints(shot.resistance);

    for (const QPointF& p : weight) {
        if (p.y() >= kFirstDropG) {
            m[QStringLiteral("firstDropSec")] = p.x();
            break;
        }
    }

    // The pour window shot analysis derived from the phase markers, so this and
    // the detectors agree on when the pour began. No marker: the whole shot.
    const double pourStart = shot.detectorResults.value(QStringLiteral("pourStartSec")).toDouble();
    const double pourEnd = shot.detectorResults.value(QStringLiteral("pourEndSec")).toDouble();
    // An imported shot can carry curves but no duration; then the curves say how long it ran.
    const double lastSample = std::max(pressure.isEmpty() ? 0.0 : pressure.last().x(),
                                       flow.isEmpty() ? 0.0 : flow.last().x());
    const double end = duration > 0 ? duration : lastSample;
    const double ws = pourStart > 0 ? pourStart : 0.0;
    const double we = pourEnd > ws ? pourEnd : end;
    if (pourStart > 0) {
        m[QStringLiteral("preinfusionSec")] = pourStart;
        if (we > ws) m[QStringLiteral("pourSec")] = we - ws;
    }

    if (!pressure.isEmpty())
        m[QStringLiteral("peakPressureBar")] = ShotSummarizer::calculateMax(pressure, 0, end);
    if (!flow.isEmpty() && we > ws) {
        m[QStringLiteral("peakFlowMlPerSec")] = ShotSummarizer::calculateMax(flow, ws, we);
        m[QStringLiteral("meanFlowMlPerSec")] = ShotSummarizer::calculateAverage(flow, ws, we);
    }
    if (m.contains(QStringLiteral("firstDropSec")) && shot.finalWeightG > 0) {
        const double pouring = end - m.value(QStringLiteral("firstDropSec"));
        if (pouring > 0.5) m[QStringLiteral("weightFlowGPerSec")] = shot.finalWeightG / pouring;
    }
    if (!temperature.isEmpty() && we > ws) {
        m[QStringLiteral("groupTempC")] = ShotSummarizer::calculateAverage(temperature, ws, we);
        const double atStart = ShotSummarizer::findValueAtTime(temperature, ws);
        const double low = ShotSummarizer::calculateMin(temperature, ws, we);
        m[QStringLiteral("tempSagC")] = std::max(0.0, atStart - low);
    }
    if (!resistance.isEmpty() && !flow.isEmpty() && we > ws) {
        double sum = 0;
        int n = 0;
        for (const QPointF& p : resistance) {
            if (p.x() < ws || p.x() > we) continue;
            if (ShotSummarizer::findValueAtTime(flow, p.x()) < kResistanceMinFlowMlPerSec) continue;
            sum += p.y();
            ++n;
        }
        if (n > 0) m[QStringLiteral("resistance")] = sum / n;
    }
    return m;
}

QStringList badgesFor(const ShotProjection& shot)
{
    QStringList b;
    if (shot.pourTruncatedDetected) b << QStringLiteral("pourTruncated");
    if (shot.channelingDetected) b << QStringLiteral("channeling");
    if (shot.grindIssueDetected) b << QStringLiteral("grindIssue");
    if (shot.skipFirstFrameDetected) b << QStringLiteral("skipFirstFrame");
    return b;
}

namespace {

QString stateName(FieldState s)
{
    switch (s) {
    case FieldState::Same: return QStringLiteral("same");
    case FieldState::Changed: return QStringLiteral("changed");
    case FieldState::OneSided: return QStringLiteral("oneSided");
    }
    return QString();
}

QJsonValue inputValue(const InputField& f, bool base)
{
    if (f.numeric) {
        const double v = base ? f.baseValue : f.shotValue;
        return v > 0 || f.key == QLatin1String("grinderSetting") ? QJsonValue(v) : QJsonValue();
    }
    const QString t = base ? f.baseText : f.shotText;
    return t.isEmpty() ? QJsonValue() : QJsonValue(t);
}

// Text alongside the number, so a grind of "1+4" or "9.5 1400rpm" displays as
// recorded while its Δ still uses the dial number.
QJsonValue inputText(const InputField& f, bool base)
{
    const QString t = base ? f.baseText : f.shotText;
    return t.isEmpty() ? QJsonValue() : QJsonValue(t);
}

std::optional<Profile> parsedProfile(const QString& json)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    Profile p = Profile::fromJson(doc);
    if (!p.isValid()) return std::nullopt;
    return p;
}

QJsonObject profileDiff(const ShotProjection& base, const ShotProjection& shot)
{
    QJsonObject out;
    const bool sameTitle = textState(base.profileName, shot.profileName) == FieldState::Same;
    out[QStringLiteral("sameTitle")] = sameTitle;
    out[QStringLiteral("baseTitle")] = base.profileName;
    out[QStringLiteral("shotTitle")] = shot.profileName;
    QJsonArray rows;
    QJsonValue sameVersion;   // null: unknown
    if (sameTitle) {
        const auto a = parsedProfile(base.profileJson);
        const auto b = parsedProfile(shot.profileJson);
        if (a && b) {
            for (const ProfileFieldDelta& d : Profile::dialInDeltas(*a, *b))
                rows.append(QJsonObject::fromVariantMap(d.toVariantMap()));
            sameVersion = rows.isEmpty();
        }
    }
    out[QStringLiteral("sameVersion")] = sameVersion;
    out[QStringLiteral("rows")] = rows;
    return out;
}

QJsonObject shotJson(const ShotProjection& s, bool isBase)
{
    QJsonObject o;
    o[QStringLiteral("shotId")] = s.id;
    o[QStringLiteral("isBase")] = isBase;
    o[QStringLiteral("timestamp")] = s.timestampIso;
    o[QStringLiteral("profileName")] = s.profileName;
    const double target = effectiveTargetWeightG(s);
    o[QStringLiteral("targetYieldG")] = target > 0 ? QJsonValue(target) : QJsonValue();
    o[QStringLiteral("stoppedBy")] = s.stoppedBy.isEmpty() ? QJsonValue() : QJsonValue(s.stoppedBy);
    o[QStringLiteral("badges")] = QJsonArray::fromStringList(badgesFor(s));
    o[QStringLiteral("rating0to100")] = s.enjoyment0to100 > 0 ? QJsonValue(s.enjoyment0to100) : QJsonValue();
    o[QStringLiteral("tasteBalance")] = s.tasteBalance.isEmpty() ? QJsonValue() : QJsonValue(s.tasteBalance);
    o[QStringLiteral("tasteBody")] = s.tasteBody.isEmpty() ? QJsonValue() : QJsonValue(s.tasteBody);
    o[QStringLiteral("notes")] = s.espressoNotes.isEmpty() ? QJsonValue() : QJsonValue(s.espressoNotes);
    return o;
}

QJsonObject fromTo(const QJsonValue& a, const QJsonValue& b, std::optional<double> delta)
{
    QJsonObject o{ { QStringLiteral("from"), a }, { QStringLiteral("to"), b } };
    if (delta) o[QStringLiteral("delta")] = *delta;
    return o;
}

QJsonObject buildPairChanges(const ShotProjection& from, const ShotProjection& to,
                             const InputDiff& d, const QMap<QString, double>& ma,
                             const QMap<QString, double>& mb, PairInputs which)
{
    QJsonObject inputs;
    for (const InputField& f : d.fields) {
        if (f.state == FieldState::Same) continue;
        if (which == PairInputs::DialIn && !f.dialIn) continue;
        const QJsonValue a = f.numeric && f.key != QLatin1String("grinderSetting") ? inputValue(f, true) : inputText(f, true);
        const QJsonValue b = f.numeric && f.key != QLatin1String("grinderSetting") ? inputValue(f, false) : inputText(f, false);
        inputs[f.key] = fromTo(a, b, f.delta);
    }

    QJsonObject outcomes;
    for (const MetricDef& def : metricDefs()) {
        if (def.more) continue;
        const QString key = QLatin1String(def.key);
        if (!ma.contains(key) || !mb.contains(key)) continue;
        const double delta = shownDelta(ma.value(key), mb.value(key), def.decimals);
        if (delta == 0.0) continue;
        outcomes[key] = fromTo(roundTo(ma.value(key), def.decimals), roundTo(mb.value(key), def.decimals), delta);
    }
    if (from.enjoyment0to100 > 0 && to.enjoyment0to100 > 0 && from.enjoyment0to100 != to.enjoyment0to100)
        outcomes[QStringLiteral("rating0to100")] = fromTo(from.enjoyment0to100, to.enjoyment0to100,
                                                          double(to.enjoyment0to100 - from.enjoyment0to100));

    QJsonObject out;
    if (!inputs.isEmpty()) out[QStringLiteral("inputs")] = inputs;
    if (!outcomes.isEmpty()) out[QStringLiteral("outcomes")] = outcomes;
    return out;
}

} // namespace

QJsonObject pairChanges(const ShotProjection& from, const ShotProjection& to, PairInputs inputs)
{
    return buildPairChanges(from, to, diffInputs(from, to), metricsFor(from), metricsFor(to), inputs);
}

QJsonObject pairChanges(const ShotProjection& from, const ShotProjection& to,
                        const QMap<QString, double>& fromMetrics,
                        const QMap<QString, double>& toMetrics, PairInputs inputs)
{
    return buildPairChanges(from, to, diffInputs(from, to), fromMetrics, toMetrics, inputs);
}

QJsonObject compare(const QList<ShotProjection>& input, qsizetype baseIndex)
{
    return compare(input, baseIndex, MetricRows::Supported);
}

QJsonObject compare(const QList<ShotProjection>& input, qsizetype baseIndex, MetricRows rows)
{
    QJsonObject out;
    if (input.isEmpty() || baseIndex < 0 || baseIndex >= input.size()) return out;

    QList<const ShotProjection*> shots;
    shots << &input[baseIndex];
    for (qsizetype i = 0; i < input.size(); ++i)
        if (i != baseIndex) shots << &input[i];
    const ShotProjection& base = *shots.first();

    QList<InputDiff> diffs;
    QList<QMap<QString, double>> metrics;
    for (const ShotProjection* s : shots) metrics << metricsFor(*s);
    for (qsizetype i = 1; i < shots.size(); ++i) diffs << diffInputs(base, *shots[i]);

    QJsonArray shotsJson;
    for (qsizetype i = 0; i < shots.size(); ++i) shotsJson.append(shotJson(*shots[i], i == 0));
    out[QStringLiteral("baseShotId")] = base.id;
    out[QStringLiteral("shots")] = shotsJson;

    // Inputs: a row where any shot differs from the base; the rest, when
    // recorded, form the single "unchanged" line.
    QJsonArray inputs, unchanged;
    const qsizetype fieldCount = diffs.isEmpty() ? 0 : diffs.first().fields.size();
    for (qsizetype f = 0; f < fieldCount; ++f) {
        bool differs = false;
        for (const InputDiff& d : diffs) differs |= d.fields[f].state != FieldState::Same;
        const InputField& first = diffs.first().fields[f];
        if (!differs) {
            if (!first.baseText.isEmpty() || (first.numeric && first.baseValue > 0)) {
                unchanged.append(QJsonObject{
                    { QStringLiteral("key"), first.key },
                    { QStringLiteral("unit"), first.unit },
                    { QStringLiteral("value"), inputValue(first, true) },
                    { QStringLiteral("text"), inputText(first, true) },
                });
            }
            continue;
        }
        QJsonArray cells;
        cells.append(QJsonObject{
            { QStringLiteral("state"), QStringLiteral("base") },
            { QStringLiteral("value"), inputValue(first, true) },
            { QStringLiteral("text"), inputText(first, true) },
        });
        for (const InputDiff& d : diffs) {
            const InputField& c = d.fields[f];
            cells.append(QJsonObject{
                { QStringLiteral("state"), stateName(c.state) },
                { QStringLiteral("value"), c.numeric ? inputValue(c, false) : QJsonValue() },
                { QStringLiteral("text"), inputText(c, false) },
                { QStringLiteral("delta"), c.delta ? QJsonValue(*c.delta) : QJsonValue() },
            });
        }
        inputs.append(QJsonObject{
            { QStringLiteral("key"), first.key },
            { QStringLiteral("unit"), first.unit },
            { QStringLiteral("cells"), cells },
        });
    }
    out[QStringLiteral("inputs")] = inputs;
    out[QStringLiteral("unchanged")] = unchanged;

    // Metrics: a row for every metric at least one shot supports.
    QJsonArray metricRows;
    for (const MetricDef& def : metricDefs()) {
        const QString key = QLatin1String(def.key);
        bool any = false;
        for (const auto& m : metrics) any |= m.contains(key);
        if (!any && !(rows == MetricRows::Defaults && !def.more)) continue;
        QJsonArray cells;
        for (qsizetype i = 0; i < metrics.size(); ++i) {
            QJsonObject cell;
            cell[QStringLiteral("value")] = metrics[i].contains(key) ? QJsonValue(metrics[i].value(key)) : QJsonValue();
            QJsonValue delta;
            if (i > 0 && metrics[0].contains(key) && metrics[i].contains(key)) {
                const double d = shownDelta(metrics[0].value(key), metrics[i].value(key), def.decimals);
                if (d != 0.0) delta = d;
            }
            cell[QStringLiteral("delta")] = delta;
            cells.append(cell);
        }
        metricRows.append(QJsonObject{
            { QStringLiteral("key"), key },
            { QStringLiteral("unit"), QLatin1String(def.unit) },
            { QStringLiteral("decimals"), def.decimals },
            { QStringLiteral("more"), def.more },
            { QStringLiteral("cells"), cells },
        });
    }
    out[QStringLiteral("metrics")] = metricRows;

    // One entry per non-base shot: what it changed, and the facts its summary
    // sentence is built from. Only the ranking is decided here, so the app and
    // the web page name the same differences.
    QJsonArray comparisons;
    const QStringList baseBadges = badgesFor(base);
    for (qsizetype i = 1; i < shots.size(); ++i) {
        const ShotProjection& shot = *shots[i];
        const InputDiff& d = diffs[i - 1];
        QJsonArray changedInputs;
        for (const InputField& f : d.fields)
            if (f.state != FieldState::Same) changedInputs.append(f.key);
        const QJsonObject profile = profileDiff(base, shot);
        if (!profile.value(QStringLiteral("rows")).toArray().isEmpty())
            changedInputs.append(QStringLiteral("profileSettings"));

        // The differences furthest past their noise floor, rows on screen before rows
        // behind "Show more": a summary should point at what the reader can see. Ratio
        // follows yield unless the dose moved, so it is only a candidate when the dose did.
        QList<QPair<double, QString>> ranked, hidden;
        for (const MetricDef& def : metricDefs()) {
            const QString key = QLatin1String(def.key);
            if (def.notable <= 0) continue;
            if (key == QLatin1String("ratio") && !d.changed(QStringLiteral("doseG"))) continue;
            if (!metrics[0].contains(key) || !metrics[i].contains(key)) continue;
            const double score = std::abs(metrics[i].value(key) - metrics[0].value(key)) / def.notable;
            if (score >= 1.0) (def.more ? hidden : ranked) << qMakePair(score, key);
        }
        const auto byScore = [](const auto& x, const auto& y) { return x.first > y.first; };
        std::stable_sort(ranked.begin(), ranked.end(), byScore);
        std::stable_sort(hidden.begin(), hidden.end(), byScore);
        ranked << hidden;
        // The summary as ordered facts, each naming the words it is told in, so the
        // app and the web page say the same thing and only format the numbers.
        // What the user changed comes first (at most two, then a count, or "same
        // setup" when nothing did), then what the shot did differently, then how it
        // stopped and which badges moved, or "no notable difference" when none did.
        QJsonArray facts;
        if (changedInputs.isEmpty())
            facts.append(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("sameSetup") } });
        int namedInputs = 0, moreInputs = 0;
        auto addInput = [&](QJsonObject fact) {
            if (namedInputs == 2) { ++moreInputs; return; }
            ++namedInputs;
            facts.append(fact);
        };
        for (const InputField& f : d.fields) {
            if (f.state == FieldState::Same) continue;
            const bool numericValue = f.numeric && f.key != QLatin1String("grinderSetting");
            if (f.dialIn && f.state == FieldState::Changed)
                addInput(QJsonObject{
                    { QStringLiteral("kind"), QStringLiteral("input") },
                    { QStringLiteral("key"), f.key },
                    { QStringLiteral("unit"), f.unit },
                    { QStringLiteral("from"), numericValue ? inputValue(f, true) : inputText(f, true) },
                    { QStringLiteral("to"), numericValue ? inputValue(f, false) : inputText(f, false) },
                });
            else
                addInput(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("inputChanged") },
                                      { QStringLiteral("key"), f.key } });
        }
        if (!profile.value(QStringLiteral("rows")).toArray().isEmpty())
            addInput(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("inputChanged") },
                                  { QStringLiteral("key"), QStringLiteral("profileSettings") } });
        if (moreInputs > 0)
            facts.append(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("moreInputs") },
                                      { QStringLiteral("count"), moreInputs } });
        const qsizetype outcomeStart = facts.size();

        for (qsizetype r = 0; r < std::min<qsizetype>(3, ranked.size()); ++r) {
            const QString key = ranked[r].second;
            const auto def = std::find_if(metricDefs().cbegin(), metricDefs().cend(),
                                          [&](const MetricDef& m) { return key == QLatin1String(m.key); });
            const double delta = shownDelta(metrics[0].value(key), metrics[i].value(key), def->decimals);
            if (delta == 0.0) continue;
            const QString unit = QLatin1String(def->unit);
            // Times read longer/shorter (the first drop, earlier/later), grams more/less,
            // everything else higher/lower: a direction, never a verdict.
            const bool up = delta > 0;
            const char* phrase = key == QLatin1String("firstDropSec") ? (up ? "phrase.later" : "phrase.earlier")
                                 : unit == QLatin1String("s") ? (up ? "phrase.longer" : "phrase.shorter")
                                 : unit == QLatin1String("g") ? (up ? "phrase.more" : "phrase.less")
                                 : (up ? "phrase.higher" : "phrase.lower");
            facts.append(QJsonObject{
                { QStringLiteral("kind"), QStringLiteral("metric") },
                { QStringLiteral("key"), key },
                { QStringLiteral("unit"), unit },
                { QStringLiteral("decimals"), def->decimals },
                { QStringLiteral("delta"), delta },
                { QStringLiteral("phrase"), QLatin1String(phrase) },
            });
        }

        // Only between two recorded reasons: against an unrecorded one there is no
        // difference to state.
        if (!shot.stoppedBy.isEmpty() && !base.stoppedBy.isEmpty() && shot.stoppedBy != base.stoppedBy)
            facts.append(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("stopped") },
                                      { QStringLiteral("stoppedBy"), shot.stoppedBy } });

        const QStringList shotBadges = badgesFor(shot);
        for (const QString& b : shotBadges)
            if (!baseBadges.contains(b))
                facts.append(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("badgeAppeared") },
                                          { QStringLiteral("badge"), b } });
        for (const QString& b : baseBadges)
            if (!shotBadges.contains(b))
                facts.append(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("badgeGone") },
                                          { QStringLiteral("badge"), b } });

        if (facts.size() == outcomeStart)
            facts.append(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("noNotable") } });

        comparisons.append(QJsonObject{
            { QStringLiteral("shotId"), shot.id },
            { QStringLiteral("nothingChanged"), changedInputs.isEmpty() },
            { QStringLiteral("changedInputs"), changedInputs },
            { QStringLiteral("profile"), profile },
            { QStringLiteral("changes"), buildPairChanges(base, shot, d, metrics[0], metrics[i], PairInputs::All) },
            { QStringLiteral("summary"), facts },
        });
    }
    out[QStringLiteral("comparisons")] = comparisons;
    return out;
}

} // namespace ShotComparison
