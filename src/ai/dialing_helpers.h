#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

// Pure-logic helpers shared by both the MCP `dialing_get_context` tool and
// the in-app advisor enrichment path. Lives under src/ai/ (the consumer
// that owns the dialing-context concept) rather than src/mcp/, so the AI
// subsystem does not reverse-include from MCP. Issue #1040.

namespace DialingHelpers {

// A run of consecutive shots on the same profile counts as one dial-in
// "session" when the gap between adjacent shots is small enough that the
// user is plausibly still iterating. 60 minutes covers the realistic case
// (pull, taste, adjust grinder, re-dose, pull again) without merging
// unrelated morning/afternoon attempts.
constexpr qint64 kDialInSessionGapSec = 60 * 60;

// Group an ordered (DESC by timestamp) list of timestamps into sessions.
// Two adjacent timestamps belong to the same session iff their gap is
// <= thresholdSec. Returns each session as a list of indices into the input
// list, preserving the input's DESC order within each session. Sessions
// themselves are emitted in input order (= newest session first).
//
// Pure function: no Qt object dependencies, easy to unit-test.
inline QList<QList<qsizetype>> groupSessions(const QList<qint64>& timestampsDesc,
                                              qint64 thresholdSec = kDialInSessionGapSec)
{
    QList<QList<qsizetype>> sessions;
    if (timestampsDesc.isEmpty())
        return sessions;

    QList<qsizetype> current;
    for (qsizetype i = 0; i < timestampsDesc.size(); ++i) {
        current.append(i);
        const bool isLast = (i == timestampsDesc.size() - 1);
        const bool gapTooLarge = !isLast &&
            qAbs(timestampsDesc[i] - timestampsDesc[i + 1]) > thresholdSec;
        if (isLast || gapTooLarge) {
            sessions.append(current);
            current.clear();
        }
    }
    return sessions;
}

// The five shot-identity fields hoistable to a session-level `context`
// object. These describe the user's *setup* (which grinder, which bean) —
// shot-INVARIANT across a single dial-in session in the typical case.
// Shot-VARIABLE fields (`grinderSetting`, `doseWeightG`, `finalWeightG`,
// `durationSec`, `enjoyment0to100`, `notes`) are NOT hoisted; they are
// what the user is iterating on.
struct ShotIdentity {
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString beanBrand;
    QString beanType;
};

// Output of `hoistSessionContext`: the field values that go on the
// session-level `context` object, plus a parallel list of per-shot
// overrides where each override carries ONLY the fields that differ from
// the session context. Index-aligned with the input list.
struct HoistedSession {
    ShotIdentity context;
    QList<ShotIdentity> perShotOverrides;
};

// Hoist common shot-identity fields to a session-level context. For each
// field independently:
//   - Set `context.field` to `shots[0].field` when at least one shot has
//     a non-empty value for that field. When `shots[0].field` is empty
//     but a later shot has a value, fall back to the first non-empty.
//     When NO shot has a non-empty value, leave `context.field` empty
//     (the JSON serializer should then omit the field from the context).
//   - For each shot `i`, the per-shot override carries `shot[i].field`
//     iff `shot[i].field != context.field`. Otherwise the override
//     leaves the field empty (the serializer should omit it).
//
// Pure function — no Qt object dependencies, easy to unit-test.
inline HoistedSession hoistSessionContext(const QList<ShotIdentity>& shots)
{
    HoistedSession out;
    if (shots.isEmpty()) return out;

    out.perShotOverrides.resize(shots.size());

    auto firstNonEmpty = [&](auto getter) -> QString {
        for (const auto& s : shots) {
            const QString v = getter(s);
            if (!v.isEmpty()) return v;
        }
        return QString();
    };

    auto fillField = [&](auto getter, auto setterCtx, auto setterOverride) {
        const QString ctx = firstNonEmpty(getter);
        setterCtx(out.context, ctx);
        for (qsizetype i = 0; i < shots.size(); ++i) {
            const QString v = getter(shots[i]);
            if (v != ctx) {
                setterOverride(out.perShotOverrides[i], v);
            }
        }
    };

    fillField([](const ShotIdentity& s) { return s.grinderBrand; },
              [](ShotIdentity& c, const QString& v) { c.grinderBrand = v; },
              [](ShotIdentity& o, const QString& v) { o.grinderBrand = v; });
    fillField([](const ShotIdentity& s) { return s.grinderModel; },
              [](ShotIdentity& c, const QString& v) { c.grinderModel = v; },
              [](ShotIdentity& o, const QString& v) { o.grinderModel = v; });
    fillField([](const ShotIdentity& s) { return s.grinderBurrs; },
              [](ShotIdentity& c, const QString& v) { c.grinderBurrs = v; },
              [](ShotIdentity& o, const QString& v) { o.grinderBurrs = v; });
    fillField([](const ShotIdentity& s) { return s.beanBrand; },
              [](ShotIdentity& c, const QString& v) { c.beanBrand = v; },
              [](ShotIdentity& o, const QString& v) { o.beanBrand = v; });
    fillField([](const ShotIdentity& s) { return s.beanType; },
              [](ShotIdentity& c, const QString& v) { c.beanType = v; },
              [](ShotIdentity& o, const QString& v) { o.beanType = v; });

    return out;
}

// Instruction shipped when the bag's storage history is UNKNOWN (no freeze
// or thaw dates recorded). Reads as imperative, not advisory — calendar age
// is a category-mistake against actual bean freshness when storage history is
// unknown (frozen, thawed weekly, vacuum-sealed, etc.). The previous advisory
// note was demonstrably skimmed past; this one tells the AI explicitly to ASK
// before quoting age.
inline constexpr const char* kBeanFreshnessInstruction =
    "Calendar age from roastDate is NOT freshness — many users freeze and "
    "thaw weekly. ASK the user about storage before applying any "
    "bean-aging guidance.";

// Instruction shipped when the bag DOES carry storage history (a frozenDate
// and/or defrostDate is present). Storage is no longer a missing variable, so
// the AI must NOT ask about it — and must not treat calendar days from
// roastDate as staleness. Freezing pauses staling, so the aging clock runs
// from defrostDate (the thaw), not roastDate: beans frozen since roast and
// recently thawed are fresh regardless of calendar age.
inline constexpr const char* kBeanFreshnessKnownInstruction =
    "Storage history is known from the dates below — do NOT ask the user "
    "about storage. Freezing pauses staling: count bean age from defrostDate "
    "(the thaw), not roastDate. Beans frozen since roast and recently thawed "
    "are fresh regardless of how many calendar days have passed since roast.";

// Build the `currentBean.beanFreshness` block. Replaces the deprecated
// `daysSinceRoast` + `daysSinceRoastNote` fields. Returns an empty object
// (caller suppresses the parent assignment) only when there is nothing to say
// — no `roastDate` AND no freeze/thaw dates.
//
// `freshnessKnown` is `true` when the bag carries a `frozenDate` and/or
// `defrostDate`: that storage history is the variable whose absence the
// unknown-case instruction asks the user to supply, so its presence flips the
// guidance from "ASK about storage" to "age from the thaw date." The block
// still contains NO precomputed day count under any field name — the AI does
// the subtraction itself (from `defrostDate` when freshness is known).
//
// Pure function: easy to unit-test, no DB / Settings dependency.
inline QJsonObject buildBeanFreshness(const QString& roastDate,
                                      const QString& frozenDate = QString(),
                                      const QString& defrostDate = QString())
{
    const bool known = !frozenDate.isEmpty() || !defrostDate.isEmpty();
    if (roastDate.isEmpty() && !known) return QJsonObject();
    QJsonObject block;
    if (!roastDate.isEmpty()) block["roastDate"] = roastDate;
    if (!frozenDate.isEmpty()) block["frozenDate"] = frozenDate;
    if (!defrostDate.isEmpty()) block["defrostDate"] = defrostDate;
    block["freshnessKnown"] = known;
    block["instruction"] = QString::fromUtf8(
        known ? kBeanFreshnessKnownInstruction : kBeanFreshnessInstruction);
    return block;
}

// Inputs for the per-shot diff that drives both `changeFromPrev` (within a
// dial-in session) and `changeFromBest` (current vs best-recent). Carries
// just the fields that change between adjacent shots in a dial-in flow —
// grind setting, bean brand, dose, yield, duration, enjoyment. Anything
// stable across an iteration session (grinder model, burrs, profile) is
// out of scope for the diff.
struct ShotDiffInputs {
    QString grinderSetting;
    QString beanBrand;
    double doseWeightG = 0;
    double finalWeightG = 0;
    double durationSec = 0;
    int enjoyment0to100 = 0;
};

// Build a "what moved between two shots" JSON diff. Direction is
// `from -> to`: each non-empty field comparison emits "<from> -> <to>" for
// strings, "<from> -> <to> <unit> (<+/-><delta>)" for numerics. Empty or
// zero fields on either side are skipped — there's nothing to diff. Same
// shape `shots_compare` produces; reused here so the AI sees a consistent
// "what moved" envelope across changeFromPrev and changeFromBest.
//
// Pure function; safe to test without DB or settings.
inline QJsonObject buildShotChangeDiff(const ShotDiffInputs& from,
                                        const ShotDiffInputs& to)
{
    QJsonObject diff;
    auto diffStr = [&](const QString& a, const QString& b, const QString& key) {
        if (!a.isEmpty() && !b.isEmpty() && a != b)
            diff[key] = QString("%1 -> %2").arg(a, b);
    };
    auto diffNum = [&](double a, double b, const QString& key, const QString& unit) {
        if (a != 0 && b != 0 && qAbs(a - b) > 0.01)
            diff[key] = QString("%1 -> %2 %3 (%4%5)")
                .arg(a, 0, 'f', 1).arg(b, 0, 'f', 1).arg(unit)
                .arg(b > a ? "+" : "").arg(b - a, 0, 'f', 1);
    };
    diffStr(from.grinderSetting, to.grinderSetting, QStringLiteral("grinderSetting"));
    diffStr(from.beanBrand, to.beanBrand, QStringLiteral("beanBrand"));
    diffNum(from.doseWeightG, to.doseWeightG, QStringLiteral("doseG"), QStringLiteral("g"));
    diffNum(from.finalWeightG, to.finalWeightG, QStringLiteral("yieldG"), QStringLiteral("g"));
    diffNum(from.durationSec, to.durationSec, QStringLiteral("durationSec"), QStringLiteral("s"));
    diffNum(from.enjoyment0to100, to.enjoyment0to100,
            QStringLiteral("enjoyment0to100"), QStringLiteral(""));
    return diff;
}

// Estimate the average pour flow rate over the last `windowSec` seconds of
// the shot. Drives the sawPrediction block: SAW math needs a representative
// flow at cutoff, and the tail of the recorded flow curve tracks the same
// physical flow regime that was active when stop-at-weight engaged.
//
// `flowSamples` is the QVariantList shape ShotProjection ships
// (`{"x": <time>, "y": <flow>}` per entry). `durationSec` is the shot's
// total duration; the window is `[durationSec - windowSec, durationSec]`,
// clamped to t >= 0. Samples with y <= 0 (drip, scale noise) are skipped.
// Returns 0.0 when no usable samples land in the window — caller decides
// the fallback (typically "default to typical espresso pour rate").
//
// Pure function: the only Qt dep is QVariant unboxing.
inline double estimateFlowAtCutoff(const QVariantList& flowSamples,
                                    double durationSec,
                                    double windowSec = 2.0)
{
    if (flowSamples.isEmpty() || durationSec <= 0) return 0.0;
    const double windowStart = qMax(0.0, durationSec - windowSec);
    double sum = 0.0;
    int count = 0;
    for (qsizetype i = flowSamples.size() - 1; i >= 0; --i) {
        const QVariantMap pt = flowSamples[i].toMap();
        const double t = pt.value(QStringLiteral("x")).toDouble();
        if (t < windowStart) break;
        const double y = pt.value(QStringLiteral("y")).toDouble();
        if (y > 0) {
            sum += y;
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

} // namespace DialingHelpers
