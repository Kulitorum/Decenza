#pragma once

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Helpers over the compact-JSON linked-bean snapshot ("the blob") — the one
// string carried by SettingsDye::dyeBeanBaseData, preset rows,
// shots.beanbase_json, and the ShotMetadata/ShotSaveData/ShotRecord/
// ShotProjection beanBaseJson fields. Header-only so the uploader, settings,
// and tests share one definition without link-time coupling.
namespace BeanBaseBlob {

// True iff the blob parses to a non-empty JSON object carrying a non-empty
// `id` — the single definition of "this string represents a linked bean".
// "" (unlinked, the common case) is simply not linked, not corrupt.
inline bool isLinked(const QString& blob)
{
    if (blob.isEmpty())
        return false;
    const QJsonObject obj = QJsonDocument::fromJson(blob.toUtf8()).object();
    return !obj.value(QStringLiteral("id")).toVariant().toString().isEmpty();
}

// Visualizer canonical UUID for shot PATCH linkage, or "" when the blob is
// empty, corrupt, or Bean-Base-sourced without a canonical id. The emit-only
// contract lives on this emptiness: callers must NOT write the key (let
// alone null it) when this returns "" — the user may have linked the bag in
// Visualizer's own UI.
inline QString canonicalId(const QString& blob)
{
    if (blob.isEmpty())
        return QString();
    return QJsonDocument::fromJson(blob.toUtf8())
        .object().value(QStringLiteral("visualizerCanonicalId")).toString();
}

// The user-editable working keys (add-bag-detail-editing): identity display
// names, roast degree, and the descriptive detail fields. Everything else in
// the blob (link ids, `canonical` snapshot, description, legacy image) is
// preserved untouched by mergeBeanDetails. farm/qualityScore/placeOfPurchase
// exist only as user input — the canonical DB has no such columns. The tea
// vocabulary (add-recipe-wizard-tea: teaType…steepTime) is user/extraction
// input on tea bags only — coffee edits carry them empty, which is a no-op
// removal of keys coffee blobs never have.
inline const QStringList& editableKeys()
{
    static const QStringList keys{
        QStringLiteral("roasterName"), QStringLiteral("roastName"),
        QStringLiteral("degree"),      QStringLiteral("origin"),
        QStringLiteral("region"),      QStringLiteral("farm"),
        QStringLiteral("producer"),    QStringLiteral("variety"),
        QStringLiteral("elevation"),   QStringLiteral("process"),
        QStringLiteral("harvest"),     QStringLiteral("qualityScore"),
        QStringLiteral("placeOfPurchase"), QStringLiteral("tastingNotes"),
        QStringLiteral("link"),
        QStringLiteral("teaType"),     QStringLiteral("garden"),
        QStringLiteral("cultivar"),    QStringLiteral("flush"),
        QStringLiteral("brewTempC"),   QStringLiteral("leafGramsPer100Ml"),
        QStringLiteral("steepTime"),
    };
    return keys;
}

// Copy the current non-empty editable values into a `canonical` sub-object —
// the pristine snapshot Revert restores. Captured on the FIRST edit-merge of
// a linked blob (values are pristine until then by construction, including
// blobs linked before this feature existed); edits never touch it after.
inline void captureCanonicalIfNeeded(QJsonObject& obj)
{
    if (obj.value(QStringLiteral("id")).toVariant().toString().isEmpty())
        return;  // unlinked: nothing authoritative to snapshot
    if (obj.contains(QStringLiteral("canonical")))
        return;
    QJsonObject snapshot;
    for (const QString& key : editableKeys()) {
        const QString value = obj.value(key).toVariant().toString();
        if (!value.isEmpty())
            snapshot[key] = value;
    }
    obj[QStringLiteral("canonical")] = snapshot;
}

// True when the string is non-empty but does not parse to a JSON object —
// merging into (or reverting) such a blob would silently REBUILD it, throwing
// away the canonical link, snapshot, and description. Corrupt machine-written
// JSON is rare, which is exactly why it must be loud and non-destructive.
inline bool isCorruptBlob(const QString& blob)
{
    if (blob.isEmpty())
        return false;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(blob.toUtf8(), &parseError);
    return parseError.error != QJsonParseError::NoError || !doc.isObject();
}

// True when the canonical record behind this blob names a DIFFERENT coffee than
// the one stored locally (`identity` = the bag's or shot's own names).
//
// This is a guard on EXPORT, not on the link. visualizer.coffee treats a shot's
// canonical_coffee_bag_id as authoritative for identity — Shot#refresh_coffee_
// bag_fields (miharekar/visualizer, app/models/shot.rb:64-75, upstream/main) is a
// before_validation on `coffee_bag_id_changed? || canonical_coffee_bag_id_changed?`,
// and its `elsif canonical_coffee_bag` branch sets bean_brand =
// canonical_roaster.name and bean_type = canonical_coffee_bag.name — so uploading
// the link for a borrowed record silently RENAMES the user's shot on the server.
// That branch is reached only when the shot has no server-side coffee_bag; a
// Coffee Management shot takes the first branch and keeps the user's own bag
// roaster, which is why CM users saw this less than everyone else. Borrowing is legitimate and expected: the bag
// editor keeps identity editable while linked (the canonical link is a badge,
// not a lock), which is exactly how a Stavanger Kaffebrenneri bag ends up
// carrying Coava Coffee Roasters' canonical record for the same coffee.
//
// Compares the PRISTINE canonical names — roasterName/roastName are user-
// editable working keys, so the `canonical` snapshot wins where it exists. An
// empty name on either side proves nothing (legacy blobs stored no names), so
// only a populated disagreement conflicts: unknown must not block a link that
// works today.
// The bag's own identity: what the USER says this coffee is. Bundled rather
// than passed as two loose QStrings because every consumer of this header takes
// both together, and an argument list of interchangeable strings is a silent
// transposition waiting to happen. Brace-init order is the residual hazard —
// `{coffee, roaster}` still compiles — but that swap makes the predicate
// disagree for essentially every real bag, so it over-unlinks rather than
// under-unlinks and the "a consistent bag keeps its link" tests catch it.
struct BagIdentity {
    QString roaster;
    QString coffee;
};

inline bool canonicalIdentityConflicts(const QString& blob, const BagIdentity& identity)
{
    // A blob we cannot read is not a blob we can vouch for. Returning "no
    // conflict" here is the PERMISSIVE answer — it exports the canonical id and
    // lets the server rename the shot, which is the defect this function exists
    // to prevent. Every other mutator in this header refuses a corrupt blob;
    // this one, whose wrong answer reaches the user's cloud account, fails
    // closed instead: withhold the claim we cannot verify.
    if (isCorruptBlob(blob)) {
        qWarning() << "BeanBaseBlob: corrupt blob - treating the canonical link as conflicted";
        return true;
    }
    const QJsonObject obj = QJsonDocument::fromJson(blob.toUtf8()).object();
    if (obj.isEmpty())
        return false;
    const QJsonObject snapshot = obj.value(QStringLiteral("canonical")).toObject();
    auto pristine = [&](const char* key) {
        const QString snap = snapshot.value(QLatin1String(key)).toString().trimmed();
        return snap.isEmpty() ? obj.value(QLatin1String(key)).toString().trimmed() : snap;
    };
    auto disagrees = [](const QString& canonicalName, const QString& localName) {
        return !canonicalName.isEmpty() && !localName.isEmpty()
               && canonicalName.compare(localName, Qt::CaseInsensitive) != 0;
    };
    return disagrees(pristine("roasterName"), identity.roaster.trimmed())
           || disagrees(pristine("roastName"), identity.coffee.trimmed());
}

// A bag's canonical link: the stored id and the blob that carries its keys.
// They are only ever read together and only ever mutated together — dropping
// the link clears the id AND strips the blob, and doing one without the other
// leaves a half-linked bag. That co-mutation used to be a comment; as a struct
// it is the shape of the code. It also removes the transposition that mattered:
// two adjacent `QString*` out-params could be swapped silently, which sent the
// UUID through isCorruptBlob — non-empty and not JSON, so "corrupt" — and the
// function then returned false, i.e. NO conflict, for every bag. The guard
// became a no-op at a call site that discards the return value.
struct CanonicalLink {
    QString id;
    QString blob;
};

// Drop the canonical LINK from a blob, keeping every descriptive value as the
// user's own data. Used when a bag's identity no longer matches the record it
// points at: the record described another roaster's product, so the id, the
// roaster id and the pristine snapshot are claims we can no longer make — but
// origin/process/variety/tasting notes/link are what the user is looking at,
// and deleting those would be a second wrong. Returns "" when nothing is left.
inline QString stripCanonicalLink(const QString& blob)
{
    if (isCorruptBlob(blob)) {
        qWarning() << "BeanBaseBlob: refusing to strip a corrupt blob (kept unchanged)";
        return blob;
    }
    QJsonObject obj = QJsonDocument::fromJson(blob.toUtf8()).object();
    // NOT `source`: that records where the descriptive values came from, and
    // they are being kept. Only the identity claims go.
    for (const char* key : {"id", "visualizerCanonicalId", "canonicalRoasterId",
                            "canonical"})
        obj.remove(QLatin1String(key));
    if (obj.isEmpty())
        return QString();
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// Merge user edits into the blob's working keys. Only editableKeys() entries
// in `edits` apply; an empty value REMOVES the key (absent-not-empty keeps the
// details popup's zero-footprint-per-field rule working). Returns the compact
// blob, or "" when the result carries no keys at all (a manual bag whose last
// detail was cleared goes back to a truly empty blob). A corrupt input blob
// is returned unchanged (edits refused) rather than destructively rebuilt.
// Shared by the bag editor (via the QML bridge) and MCP bag action=update so both
// paths have identical merge semantics (the callers assemble their own edit
// maps: the editor always sends every detail key, MCP only provided params).
inline QString mergeBeanDetails(const QString& blob, const QVariantMap& edits)
{
    if (isCorruptBlob(blob)) {
        qWarning() << "BeanBaseBlob: refusing merge into corrupt blob (kept unchanged)";
        return blob;
    }
    QJsonObject obj = QJsonDocument::fromJson(blob.toUtf8()).object();
    captureCanonicalIfNeeded(obj);
    for (const QString& key : editableKeys()) {
        if (!edits.contains(key))
            continue;
        const QString value = edits.value(key).toString().trimmed();
        if (value.isEmpty())
            obj.remove(key);
        else
            obj[key] = value;
    }
    if (obj.isEmpty())
        return QString();
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// Apply AI-extracted page values to a blob (add-beanbase-archive-link-fallback).
//
// Three-way, and the blob already carries what decides it — no new state and
// no heuristic. `canonical` is the pristine Bean Base snapshot and the flat
// keys are the working copy, so a flat value EQUAL to its canonical
// counterpart came from Bean Base, and one that DIFFERS was typed by the user:
//
//   empty                        -> filled
//   equals its canonical value   -> REPLACED (the roaster's page is the better
//                                   source for the roaster's own coffee), and
//                                   reported
//   differs from canonical, or
//   no canonical at all (manual) -> kept: the user knows something the page
//                                   does not, and discarding that silently is
//                                   worse than leaving a field stale
//
// captureCanonicalIfNeeded runs first, so a linked blob that has never been
// edited gets its snapshot BEFORE anything is corrected — which is what keeps
// "Revert to Bean Base data" able to undo a correction. The snapshot itself is
// never modified here.
//
// Returns {"blob": <merged>, "corrections": [{field, from, to}, ...]}. The
// corrections list is what the caller reports to the user; an empty list means
// nothing was overwritten (fills are not corrections).
struct ExtractionApplication {
    QString blob;          // the merged blob
    QVariantMap applied;   // key -> new value, for a caller holding form fields
    QVariantList corrections;  // {field, from, to} for each value REPLACED
};

// `current` lets a caller whose live values are not (yet) in the blob — the bag
// editor, whose form fields are the working copy — have its own values judged.
// Keys absent from it fall back to the blob's.
inline ExtractionApplication applyExtraction(const QString& blob, const QVariantMap& raw,
                                             const QVariantMap& current = {})
{
    if (isCorruptBlob(blob)) {
        qWarning() << "BeanBaseBlob: refusing extraction into corrupt blob (kept unchanged)";
        return {blob, {}, {}};
    }
    // The extraction prompt's one name that is not a blob key. Aliased HERE, in
    // the shared rule, because every caller would otherwise need its own copy —
    // and the MCP surface, having none, silently dropped the field.
    QVariantMap extracted = raw;
    if (raw.contains(QStringLiteral("roastLevel")))
        extracted.insert(QStringLiteral("degree"), raw.value(QStringLiteral("roastLevel")));

    QJsonObject obj = QJsonDocument::fromJson(blob.toUtf8()).object();
    captureCanonicalIfNeeded(obj);
    const bool hasCanonical = obj.contains(QStringLiteral("canonical"));
    const QJsonObject canonical = obj.value(QStringLiteral("canonical")).toObject();

    QVariantMap applied;
    QVariantList corrections;
    for (const QString& key : editableKeys()) {
        if (key == QLatin1String("link"))
            continue;  // the URL is how the page was reached, never read off it
        if (!extracted.contains(key))
            continue;
        const QString value = extracted.value(key).toString().trimmed();
        if (value.isEmpty())
            continue;  // the page stating nothing never clears a field
        const QString currentValue = current.contains(key)
            ? current.value(key).toString().trimmed()
            : obj.value(key).toVariant().toString().trimmed();
        if (currentValue == value)
            continue;
        if (currentValue.isEmpty()) {
            obj[key] = value;
            applied.insert(key, value);
            continue;
        }
        const QString canonicalValue = canonical.value(key).toVariant().toString().trimmed();
        if (!hasCanonical || canonicalValue != currentValue)
            continue;  // user-entered: untouchable
        obj[key] = value;
        applied.insert(key, value);
        corrections.append(QVariantMap{{QStringLiteral("field"), key},
                                       {QStringLiteral("from"), currentValue},
                                       {QStringLiteral("to"), value}});
    }
    if (obj.isEmpty())
        return {QString(), applied, corrections};
    return {QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)), applied,
            corrections};
}

// Restore the pristine canonical values: every editable key returns to the
// snapshot's value, and working keys the canonical entry lacked (e.g. a
// user-added link) are removed. No-op without a link or snapshot, and on a
// corrupt blob (same non-destructive rule as mergeBeanDetails).
inline QString revertToCanonical(const QString& blob)
{
    if (isCorruptBlob(blob)) {
        qWarning() << "BeanBaseBlob: refusing revert of corrupt blob (kept unchanged)";
        return blob;
    }
    QJsonObject obj = QJsonDocument::fromJson(blob.toUtf8()).object();
    if (obj.value(QStringLiteral("id")).toVariant().toString().isEmpty()
        || !obj.contains(QStringLiteral("canonical")))
        return blob;
    const QJsonObject snapshot = obj.value(QStringLiteral("canonical")).toObject();
    for (const QString& key : editableKeys()) {
        const QString value = snapshot.value(key).toString();
        if (value.isEmpty())
            obj.remove(key);
        else
            obj[key] = value;
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// True when the blob is linked, has a snapshot, and any editable working key
// differs from it — drives the Revert affordance's visibility.
inline bool differsFromCanonical(const QString& blob)
{
    const QJsonObject obj = QJsonDocument::fromJson(blob.toUtf8()).object();
    if (obj.value(QStringLiteral("id")).toVariant().toString().isEmpty()
        || !obj.contains(QStringLiteral("canonical")))
        return false;
    const QJsonObject snapshot = obj.value(QStringLiteral("canonical")).toObject();
    for (const QString& key : editableKeys()) {
        if (obj.value(key).toVariant().toString() != snapshot.value(key).toVariant().toString())
            return true;
    }
    return false;
}

}  // namespace BeanBaseBlob
