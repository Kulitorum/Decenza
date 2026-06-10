#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

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

}  // namespace BeanBaseBlob
