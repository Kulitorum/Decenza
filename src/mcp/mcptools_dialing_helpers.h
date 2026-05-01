#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

// Helpers extracted from mcptools_dialing.cpp so the pure-logic pieces can be
// unit-tested without spinning up the full MCP / DB / thread stack.

namespace McpDialingHelpers {

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

// Fixed instruction text shipped on every populated `beanFreshness` block.
// Reads as imperative, not advisory — calendar age is a category-mistake
// against actual bean freshness when storage history is unknown (frozen,
// thawed weekly, vacuum-sealed, etc.). The previous advisory note was
// demonstrably skimmed past; this one tells the AI explicitly to ASK
// before quoting age.
inline constexpr const char* kBeanFreshnessInstruction =
    "Calendar age from roastDate is NOT freshness — many users freeze and "
    "thaw weekly. ASK the user about storage before applying any "
    "bean-aging guidance.";

// Build the `currentBean.beanFreshness` block. Replaces the deprecated
// `daysSinceRoast` + `daysSinceRoastNote` fields. Returns an empty object
// (caller suppresses the parent assignment) when `roastDate` is empty.
//
// The block intentionally contains NO precomputed day count under any
// field name — the AI must do the subtraction itself in front of the
// user, which makes the assumption visible. `freshnessKnown` is always
// `false` until a separate change introduces a storage-mode setting.
//
// Pure function: easy to unit-test, no DB / Settings dependency.
inline QJsonObject buildBeanFreshness(const QString& roastDate)
{
    if (roastDate.isEmpty()) return QJsonObject();
    QJsonObject block;
    block["roastDate"] = roastDate;
    block["freshnessKnown"] = false;
    block["instruction"] = QString::fromUtf8(kBeanFreshnessInstruction);
    return block;
}

// `CurrentBeanInputs` and `buildCurrentBean` were retired in
// unify-current-bean-shape (issue #1043). The DYE-with-shot-fallback
// helper produced a `currentBean` whose meaning ("the live setup with
// shot fallback") disagreed with the in-app advisor's `currentBean`
// ("the setup that produced the shot") for the same shot, including
// when DYE drifted from the shot between pulls. Both surfaces now build
// `currentBean` from the resolved shot only, via the shared
// `McpDialingBlocks::buildCurrentBeanBlock(CurrentBeanBlockInputs&)`.

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

} // namespace McpDialingHelpers
