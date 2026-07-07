#pragma once

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QStringList>
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
// exist only as user input — the canonical DB has no such columns.
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

// Merge user edits into the blob's working keys. Only editableKeys() entries
// in `edits` apply; an empty value REMOVES the key (absent-not-empty keeps the
// details popup's zero-footprint-per-field rule working). Returns the compact
// blob, or "" when the result carries no keys at all (a manual bag whose last
// detail was cleared goes back to a truly empty blob). A corrupt input blob
// is returned unchanged (edits refused) rather than destructively rebuilt.
// Shared by the bag editor (via the QML bridge) and MCP bag_update so both
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
