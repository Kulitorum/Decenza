#pragma once

#include "../history/shotprojection.h"

#include <QDate>
#include <QDateTime>
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

// The shot-identity fields hoistable to a session-level `context` object.
// These describe the user's *setup* (which equipment package, which bean) plus
// the bean storage lifecycle — shot-INVARIANT across a single dial-in session in the
// typical case. Shot-VARIABLE fields (`grinderSetting`, `doseWeightG`,
// `finalWeightG`, `durationSec`, `enjoyment0to100`, `notes`) are NOT hoisted;
// they are what the user is iterating on.
struct ShotIdentity {
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString beanBrand;
    QString beanType;
    QString roastDate;
    // Bean storage lifecycle (bean-freshness-followup). Hoisted with the same
    // "shared → context, differing → per-shot override" discipline as the
    // identity fields: a session that spans a thaw/open event carries the
    // shared date in `context` and the differing shot overrides it, giving the
    // AI the raw data to notice a best-rated anchor came from a different,
    // longer-rested portion. No precomputed "different portion" flag — the raw
    // dates are the whole signal.
    QString frozenDate;
    QString defrostDate;
    QString storageHint;
    QString openedDate;
    // The rest of the equipment package. The grinder alone used to stand for it,
    // which is what let one bean+profile session mix two baskets: the payload
    // said the gear was identical because the only gear it named was.
    QString basketBrand;
    QString basketModel;
    QString puckPrep;

    // Every identity field, once, with both ends of its mapping: where it lives
    // on the shot and what it is called in the payload. The hoist, the
    // session-context emit, the per-shot override emit and the fill from a shot
    // all walk this list, so tracking a new piece of equipment is ONE row here
    // rather than the same name written out in four places free to fall out of
    // step. That drift is not hypothetical: the payload named the grinder and
    // nothing else, so a session that switched baskets read as one setup.
    struct Field {
        const char* key;
        QString ShotIdentity::* member;
        QString ShotProjection::* source;
        // True for the fields that describe the GEAR rather than the coffee.
        // Two payload blocks name the same equipment package — the hoisted
        // session context and the no-history block — and a second list of what
        // counts as equipment would be free to disagree with this one. The
        // no-history block is exactly where that disagreement would not be
        // caught, because it is emitted when there is no history to check it
        // against. So it is a flag on the one table, not a list beside it.
        bool equipment;
        // True when an empty value is itself a fact: a shot with no thaw date
        // recorded none, and must not inherit another shot's from the context.
        bool emptyIsData = false;
    };
    static const QList<Field>& fields()
    {
        static const QList<Field> f = {
            { "grinderBrand", &ShotIdentity::grinderBrand, &ShotProjection::grinderBrand, true },
            { "grinderModel", &ShotIdentity::grinderModel, &ShotProjection::grinderModel, true },
            { "grinderBurrs", &ShotIdentity::grinderBurrs, &ShotProjection::grinderBurrs, true },
            { "basketBrand",  &ShotIdentity::basketBrand,  &ShotProjection::basketBrand, true },
            { "basketModel",  &ShotIdentity::basketModel,  &ShotProjection::basketModel, true },
            { "puckPrep",     &ShotIdentity::puckPrep,     &ShotProjection::puckPrep, true },
            { "beanBrand",    &ShotIdentity::beanBrand,    &ShotProjection::beanBrand, false },
            { "beanType",     &ShotIdentity::beanType,     &ShotProjection::beanType, false },
            // Two roasts of one coffee are different beans; without this a
            // restock reads as the same bag.
            { "roastDate",    &ShotIdentity::roastDate,    &ShotProjection::roastDate, false },
            // Bean storage lifecycle (bean-freshness-followup): hoisted like any
            // other identity field, so a session spanning a thaw or open event
            // carries the shared date and the differing shot overrides it.
            { "frozenDate",   &ShotIdentity::frozenDate,   &ShotProjection::frozenDate, false, true },
            { "defrostDate",  &ShotIdentity::defrostDate,  &ShotProjection::defrostDate, false, true },
            { "storageHint",  &ShotIdentity::storageHint,  &ShotProjection::storageHint, false },
            { "openedDate",   &ShotIdentity::openedDate,   &ShotProjection::openedDate, false, true },
        };
        return f;
    }
};

// Output of `hoistSessionContext`: the field values that go on the
// session-level `context` object, plus a parallel list of per-shot
// overrides where each override carries ONLY the fields that differ from
// the session context. Index-aligned with the input list.
struct HoistedSession {
    ShotIdentity context;
    QList<ShotIdentity> perShotOverrides;
    // Per shot, the emptyIsData keys it recorded nothing for while the context
    // has a value; emitted as an explicit null.
    QList<QStringList> unrecorded;
};

// The identity a shot carries. One row in ShotIdentity::fields() puts a new
// piece of equipment here, in the hoisted context, and in the per-shot override.
inline ShotIdentity identityFromShot(const ShotProjection& shot)
{
    ShotIdentity id;
    for (const auto& f : ShotIdentity::fields())
        id.*f.member = shot.*f.source;
    return id;
}

// The equipment half of the identity as JSON, selected from the one field
// table rather than re-listed. Sparse like identityToJson: a component with no
// recorded value is omitted, never emitted as an empty string.
inline QJsonObject equipmentSetToJson(const ShotIdentity& identity)
{
    QJsonObject obj;
    for (const auto& f : ShotIdentity::fields()) {
        if (!f.equipment) continue;
        const QString v = identity.*f.member;
        if (!v.isEmpty())
            obj[QLatin1String(f.key)] = v;
    }
    return obj;
}

// The identity fields as JSON, omitting the empty ones. Both callers want that
// same sparse shape: the session context drops a field no shot carried, and a
// per-shot override drops every field that matched the context.
inline QJsonObject identityToJson(const ShotIdentity& identity)
{
    QJsonObject obj;
    for (const auto& f : ShotIdentity::fields()) {
        const QString v = identity.*f.member;
        if (!v.isEmpty())
            obj[QLatin1String(f.key)] = v;
    }
    return obj;
}

// Hoist common shot-identity fields to a session-level context. For each
// field independently:
//   - Set `context.field` to `shots[0].field` when at least one shot has
//     a non-empty value for that field. When `shots[0].field` is empty
//     but a later shot has a value, fall back to the first non-empty.
//     When NO shot has a non-empty value, leave `context.field` empty
//     (the JSON serializer should then omit the field from the context).
//   - For each shot `i`, the per-shot override carries `shot[i].field`
//     iff `shot[i].field != context.field`. Otherwise the override
//     leaves the field empty (the serializer should omit it). An empty
//     emptyIsData field that differs is listed in `unrecorded[i]` instead.
//
// Pure function — no Qt object dependencies, easy to unit-test.
inline HoistedSession hoistSessionContext(const QList<ShotIdentity>& shots)
{
    HoistedSession out;
    if (shots.isEmpty()) return out;

    out.perShotOverrides.resize(shots.size());
    out.unrecorded.resize(shots.size());

    for (const auto& f : ShotIdentity::fields()) {
        QString ctx;
        for (const auto& s : shots) {
            if (!(s.*f.member).isEmpty()) { ctx = s.*f.member; break; }
        }
        out.context.*f.member = ctx;
        for (qsizetype i = 0; i < shots.size(); ++i) {
            if (shots[i].*f.member == ctx)
                continue;
            out.perShotOverrides[i].*f.member = shots[i].*f.member;
            if (f.emptyIsData && (shots[i].*f.member).isEmpty())
                out.unrecorded[i] << QLatin1String(f.key);
        }
    }

    return out;
}

// Instruction shipped when no aging-anchor date (frozen/defrost/opened) is
// recorded. The key teaching is the ASYMMETRY: roastDate is the UPPER BOUND on
// staleness — freezing / airtight / vacuum storage only PAUSE staling, so beans
// are never OLDER than their calendar age since roast, only fresher. That makes
// the ask conditional, not automatic:
//   - Recent roast  → the beans are fresh no matter how they were stored (the
//     ceiling is low). Do NOT ask about storage; there's nothing storage could
//     reveal that changes "fresh."
//   - Old roast     → genuinely ambiguous (frozen-since-roast and still fresh,
//     vs left out and stale). ONLY here is the storage/aging question worth
//     asking. The AI judges "recent vs old" itself — we ship no day count
//     (same no-precompute rule as the rest of the block).
// buildBeanFreshness appends a storageHint clause when the storage TYPE is known
// but no date is: then the AI must not re-ask how the beans are stored (it was
// told), only — and only if the roast is old — when the current portion started
// aging.
inline constexpr const char* kBeanFreshnessInstruction =
    "roastDate is the UPPER BOUND on staleness: freezing pauses staling and "
    "airtight/vacuum storage slows it, so these beans can never be older than their "
    "calendar age since roast — only fresher. So if the roast date is recent, "
    "treat the beans as fresh and do NOT ask about storage; nothing storage "
    "could reveal would make recently-roasted beans stale. ONLY when the roast "
    "date is old is freshness ambiguous (frozen since roast and still fresh, vs "
    "left out and staled) — only then ASK how the beans have been stored and "
    "when this portion started being used, before reasoning about age.";

// Appended to kBeanFreshnessInstruction (the no-date case) when a storageHint
// IS known: the storage TYPE is no longer a missing variable, so the AI must
// not re-ask it. %1 is the storage hint (e.g. "vacuum-sealed").
inline constexpr const char* kBeanFreshnessStorageHintClause =
    " The user already told you the storage type (%1) — "
    "do NOT ask how they store the beans. If the roast is recent, they are "
    "fresh; only if the roast is old, ask solely when this portion started "
    "being used (its aging-start date), nothing else.";

// Instruction shipped when the bag DOES carry storage history (a frozenDate,
// defrostDate, and/or openedDate is present). Only freezing pauses aging:
// openedDate is air exposure, not a reset, and the days on the counter before
// freezing still count. Gassy/under-rested follows from low age, so a portion
// frozen soon after roast and just thawed is under-rested too.
inline constexpr const char* kBeanFreshnessKnownInstruction =
    "Storage history is known from the dates below — do NOT ask the user "
    "about storage. ";
// Followed by one of these two, depending on whether restAgeDays was computed.
inline constexpr const char* kBeanFreshnessRestAgeClause =
    "restAgeDays is how long these beans have aged by referenceDate (the day this "
    "shot was pulled), computed for you with frozen time removed: only freezing "
    "pauses aging, so it counts roastDate to frozenDate plus defrostDate to "
    "referenceDate; with frozenDate and no defrostDate the beans are ground "
    "straight from the freezer. Quote it rather than recomputing. ";
inline constexpr const char* kBeanFreshnessNoRestAgeClause =
    "No rest age could be computed from these dates (no usable YYYY-MM-DD roast "
    "date), so do not state one; only freezing pauses aging, so ask for the roast "
    "date if age matters to your advice. ";
// Then this.
inline constexpr const char* kBeanFreshnessKnownTail =
    "openedDate is when this portion was first used: it does NOT reset age, it "
    "only tells you how long these beans have been exposed to air. Low age (about "
    "a week or less, longer for light roasts) means under-rested and gassy — such "
    "beans choke the puck, run long, over-extract, and usually want a COARSER "
    "grind that settles back over the next few days — so a recent roast, or a "
    "portion frozen soon after roast and just thawed, is not simply 'fresher is "
    "better.'";

// The local date a shot was pulled: what its freshness dates are measured to.
inline QString shotLocalDate(const ShotProjection& shot)
{
    return shot.timestamp > 0
        ? QDateTime::fromSecsSinceEpoch(shot.timestamp).date().toString(Qt::ISODate) : QString();
}

// Days the beans have aged by `referenceDate`, with frozen time removed:
// roast→freeze plus thaw→reference, roast→freeze when ground straight from the
// freezer, roast→reference when never frozen. -1 when the dates can't say
// (unparsable, out of order, or a thaw with no freeze date).
inline int restAgeDays(const QString& roastDate, const QString& frozenDate,
                       const QString& defrostDate, const QString& referenceDate)
{
    // The whole string, not a prefix: "2026-04-05T10:00" is not a stored date.
    auto iso = [](const QString& s) {
        return s.size() == 10 ? QDate::fromString(s, QStringLiteral("yyyy-MM-dd")) : QDate();
    };
    const QDate roast = iso(roastDate), frozen = iso(frozenDate), defrost = iso(defrostDate), ref = iso(referenceDate);
    if (!roast.isValid() || !ref.isValid() || ref < roast)
        return -1;
    if (frozenDate.isEmpty())
        return defrostDate.isEmpty() ? static_cast<int>(roast.daysTo(ref)) : -1;
    if (!frozen.isValid() || frozen < roast || frozen > ref)
        return -1;
    const qint64 beforeFreezing = roast.daysTo(frozen);
    if (defrostDate.isEmpty())
        return static_cast<int>(beforeFreezing);
    if (!defrost.isValid() || defrost < frozen || defrost > ref)
        return -1;
    return static_cast<int>(beforeFreezing + defrost.daysTo(ref));
}

// Whether the storage history is recorded. A shot stamps openedDate on every
// bag, so on its own it says nothing about storage; with a storageHint the user
// has told us how it is kept.
inline bool storageKnown(const QString& frozenDate, const QString& defrostDate,
                         const QString& storageHint, const QString& openedDate)
{
    return !frozenDate.isEmpty() || !defrostDate.isEmpty()
           || (!openedDate.isEmpty() && !storageHint.isEmpty());
}

// A shot's own rest age when its storage was recorded, else -1.
inline int shotRestAgeDays(const ShotProjection& shot)
{
    if (!storageKnown(shot.frozenDate, shot.defrostDate, shot.storageHint, shot.openedDate))
        return -1;
    return restAgeDays(shot.roastDate, shot.frozenDate, shot.defrostDate, shotLocalDate(shot));
}

// Build the `currentBean.beanFreshness` block. Replaces the deprecated
// `daysSinceRoast` + `daysSinceRoastNote` fields. Returns an empty object
// (caller suppresses the parent assignment) only when there is nothing to say
// — no `roastDate`, no freeze/thaw/open date, AND no `storageHint` (a lone
// `storageHint` still emits the block; see state 2 below).
//
// `freshnessKnown` is `true` when the bag carries a `frozenDate`, `defrostDate`,
// and/or `openedDate`: the storage history is recorded, so the AI computes age
// from it (kBeanFreshnessKnownInstruction) rather than asking. `referenceDate`
// is the date the ages are measured to — the shot's date, or today for a live
// snapshot.
//
// The instruction has THREE states, not two:
//   1. No date, no storageHint → upper-bound instruction: roastDate caps
//      staleness (storage only preserves), so ask about storage ONLY when the
//      roast is old; recent roast = fresh, no question needed.
//   2. No date, storageHint set → (1) plus a clause telling the AI the storage
//      TYPE is already known (don't re-ask it) — at most ask for the aging-start
//      date, and only if the roast is old. `freshnessKnown` stays false: a hint
//      without a date is not a precise anchor.
//   3. A date is set → the known-storage instruction (age with frozen time
//      removed, with the under-rested/gassy reverse-direction guidance).
// `storageHint` is surfaced verbatim whenever set. Only the known case carries a
// day count (restAgeDays): without storage history a calendar age would mislead.
//
// Pure function: easy to unit-test, no DB / Settings dependency.
inline QJsonObject buildBeanFreshness(const QString& roastDate,
                                      const QString& frozenDate = QString(),
                                      const QString& defrostDate = QString(),
                                      const QString& storageHint = QString(),
                                      const QString& openedDate = QString(),
                                      const QString& referenceDate = QString())
{
    const bool known = storageKnown(frozenDate, defrostDate, storageHint, openedDate);
    if (roastDate.isEmpty() && !known && storageHint.isEmpty() && openedDate.isEmpty())
        return QJsonObject();
    QJsonObject block;
    // Legacy roast dates can be free text ("04/05/2026" is ambiguous): pass it
    // on as text, never as a date the AI would compute with.
    const bool roastIsIso = roastDate.size() == 10
        && QDate::fromString(roastDate, QStringLiteral("yyyy-MM-dd")).isValid();
    if (!roastDate.isEmpty())
        block[roastIsIso ? "roastDate" : "roastDateText"] = roastDate;
    if (!frozenDate.isEmpty()) block["frozenDate"] = frozenDate;
    if (!defrostDate.isEmpty()) block["defrostDate"] = defrostDate;
    if (!storageHint.isEmpty()) block["storageHint"] = storageHint;
    // An opened date older than the latest thaw belongs to the previous portion.
    if (!openedDate.isEmpty() && !(!defrostDate.isEmpty() && openedDate < defrostDate))
        block["openedDate"] = openedDate;
    if (!referenceDate.isEmpty()) block["referenceDate"] = referenceDate;
    block["freshnessKnown"] = known;
    const int age = known && roastIsIso ? restAgeDays(roastDate, frozenDate, defrostDate, referenceDate) : -1;
    if (age >= 0) block["restAgeDays"] = age;
    if (known) {
        block["instruction"] = QString::fromUtf8(kBeanFreshnessKnownInstruction)
            + QString::fromUtf8(age >= 0 ? kBeanFreshnessRestAgeClause : kBeanFreshnessNoRestAgeClause)
            + QString::fromUtf8(kBeanFreshnessKnownTail);
    } else {
        // No aging-anchor date. Teach the upper-bound asymmetry (ask only when
        // the roast is old); when the storage TYPE is known but the date isn't,
        // append the clause that tells the AI not to re-ask the storage method.
        QString instruction = QString::fromUtf8(kBeanFreshnessInstruction);
        if (!storageHint.isEmpty())
            instruction += QString::fromUtf8(kBeanFreshnessStorageHintClause).arg(storageHint);
        block["instruction"] = instruction;
    }
    return block;
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
