#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
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

// Per-field inputs the currentBean block synthesizes from. The caller fills
// the `dye*` half from `Settings::dye()` (live, user-entered) and the
// `fallback*` half from the resolved shot's grinder/dose metadata. The
// helper picks per field: DYE if non-empty, otherwise the fallback shot's
// value, and tracks which fields ended up inferred.
//
// Bean fields (brand/type/roastLevel) are intentionally not inferred —
// those rotate per hopper and a stale-shot value could mislead the AI
// into reasoning about the wrong bean. roastDate is also un-inferred and
// surfaced separately via `buildBeanFreshness` — calendar age without
// storage context is misleading regardless of source. Grinder + dose are
// stable across shots within an iteration session, so falling back is
// safe.
struct CurrentBeanInputs {
    // Live DYE values from Settings::dye() (the "currently loaded" snapshot).
    QString dyeBeanBrand;
    QString dyeBeanType;
    QString dyeRoastLevel;
    QString dyeGrinderBrand;
    QString dyeGrinderModel;
    QString dyeGrinderBurrs;
    QString dyeGrinderSetting;
    double dyeDoseWeightG = 0;

    // Resolved shot's grinder/dose snapshot (used as fallback when the DYE
    // counterpart is blank/zero). Bean fields aren't carried here — see
    // rationale above.
    QString fallbackGrinderBrand;
    QString fallbackGrinderModel;
    QString fallbackGrinderBurrs;
    QString fallbackGrinderSetting;
    double fallbackDoseWeightG = 0;
    qint64 fallbackShotId = 0;
};

// Build the `currentBean` JSON object with grinder/dose fallback to the
// resolved shot's values when the DYE field is empty/zero. Inferred fields
// are listed in `inferredFields`, with `inferredFromShotId` and an
// inferredNote pointing the AI to confirm before recommending a change.
// roastDate / beanFreshness are *not* set here — the caller composes
// `bean["beanFreshness"]` via `buildBeanFreshness(...)` so the freshness
// surface stays in one place.
//
// Pure function: no Qt object dependencies beyond JSON value types, easy to
// unit-test.
inline QJsonObject buildCurrentBean(const CurrentBeanInputs& in)
{
    QJsonObject bean;
    bean["brand"] = in.dyeBeanBrand;
    bean["type"] = in.dyeBeanType;
    bean["roastLevel"] = in.dyeRoastLevel;

    QJsonArray inferredFields;
    auto pickStr = [&](const QString& dye, const QString& fallback,
                        const QString& key) -> QString {
        if (dye.isEmpty() && !fallback.isEmpty()) {
            inferredFields.append(key);
            return fallback;
        }
        return dye;
    };
    bean["grinderBrand"] = pickStr(in.dyeGrinderBrand, in.fallbackGrinderBrand,
        QStringLiteral("grinderBrand"));
    bean["grinderModel"] = pickStr(in.dyeGrinderModel, in.fallbackGrinderModel,
        QStringLiteral("grinderModel"));
    bean["grinderBurrs"] = pickStr(in.dyeGrinderBurrs, in.fallbackGrinderBurrs,
        QStringLiteral("grinderBurrs"));
    bean["grinderSetting"] = pickStr(in.dyeGrinderSetting, in.fallbackGrinderSetting,
        QStringLiteral("grinderSetting"));

    if (in.dyeDoseWeightG <= 0 && in.fallbackDoseWeightG > 0) {
        bean["doseWeightG"] = in.fallbackDoseWeightG;
        inferredFields.append(QStringLiteral("doseWeightG"));
    } else {
        bean["doseWeightG"] = in.dyeDoseWeightG;
    }

    if (!inferredFields.isEmpty() && in.fallbackShotId > 0) {
        bean["inferredFromShotId"] = in.fallbackShotId;
        bean["inferredFields"] = inferredFields;
        bean["inferredNote"] = QStringLiteral(
            "Listed fields are inferred from the resolved shot (id "
            "above), not entered by the user. Confirm before recommending "
            "a change.");
    }

    return bean;
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

} // namespace McpDialingHelpers
