#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

// Shared strict comparisons for optimistic storage writes. QVariant's loose
// numeric conversions can make values with different wire types compare equal;
// snapshots must retain the storage map's native type as well as its value.
namespace EditablePrecondition {

inline bool hasExactStringFields(const QVariantMap& snapshot, const QStringList& keys)
{
    if (snapshot.size() != keys.size())
        return false;
    for (const QString& key : keys) {
        const auto it = snapshot.constFind(key);
        if (it == snapshot.cend() || it->metaType().id() != QMetaType::QString)
            return false;
    }
    return true;
}

inline bool valueMatches(const QVariant& expected, const QVariant& current)
{
    return expected.metaType().id() == current.metaType().id() && expected == current;
}

inline bool rawStringMatches(const QString& expected, const QString& current)
{
    return expected == current;
}

inline bool mapMatches(const QVariantMap& expected, const QVariantMap& current)
{
    if (expected.isEmpty())
        return false;
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        const auto currentIt = current.constFind(it.key());
        if (currentIt == current.cend() || !valueMatches(it.value(), currentIt.value()))
            return false;
    }
    return true;
}

} // namespace EditablePrecondition
