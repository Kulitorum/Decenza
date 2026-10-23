#include "profilecataloglogic.h"

#include "profile/profile.h"

#include <algorithm>

namespace ProfileCatalogLogic {
namespace {
QString sourceKey(int source)
{
    switch (source) {
    case 0: return QStringLiteral("builtin");
    case 1: return QStringLiteral("downloaded");
    case 2: return QStringLiteral("mine");
    default: return {};
    }
}

bool rowMatches(const QVariantMap& row, const QVariantMap& chips,
                const QString& searchLower, const QStringList& allowedBeverageTypes,
                const QSet<QString>& favoriteFilenames)
{
    const QString filename = row.value(QStringLiteral("name")).toString();
    const QString title = row.value(QStringLiteral("title")).toString();
    const QString beverageType = row.value(QStringLiteral("beverageType")).toString();

    if (!allowedBeverageTypes.isEmpty()) {
        const QString effective = beverageType.trimmed().toLower().isEmpty()
            ? QStringLiteral("espresso") : beverageType.trimmed().toLower();
        if (!allowedBeverageTypes.contains(effective))
            return false;
    }
    if (!searchLower.isEmpty() && !title.toLower().contains(searchLower))
        return false;
    if (chips.value(QStringLiteral("favorites")).toBool()
        && !favoriteFilenames.contains(filename))
        return false;

    const QStringList sources = chips.value(QStringLiteral("sources")).toStringList();
    if (!sources.isEmpty() && !sources.contains(sourceKey(row.value(QStringLiteral("source")).toInt())))
        return false;

    const QStringList beverages = chips.value(QStringLiteral("beverages")).toStringList();
    if (!beverages.isEmpty()
        && !beverages.contains(Profile::beverageBucket(beverageType)))
        return false;
    return true;
}
} // namespace

bool matches(const QVariantMap& row, const QVariantMap& chips,
             const QString& searchLower, const QStringList& allowedBeverageTypes,
             const QSet<QString>& favoriteFilenames)
{
    return rowMatches(row, chips, searchLower, allowedBeverageTypes, favoriteFilenames);
}

QVariantList filter(const QVariantList& entries, const QVariantMap& chips,
                    const QString& search, const QStringList& allowedBeverageTypes,
                    const QSet<QString>& favoriteFilenames)
{
    QVariantList result;
    const QString searchLower = search.trimmed().toLower();
    for (const QVariant& entry : entries) {
        const QVariantMap row = entry.toMap();
        if (rowMatches(row, chips, searchLower, allowedBeverageTypes, favoriteFilenames))
            result.append(row);
    }
    return result;
}

QVariantMap facetCounts(const QVariantList& entries, const QVariantMap& chips,
                        const QString& search, const QStringList& allowedBeverageTypes,
                        const QSet<QString>& favoriteFilenames)
{
    QVariantMap counts;
    const QString searchLower = search.trimmed().toLower();
    const auto countWith = [&](const QVariantMap& withChip) {
        int count = 0;
        for (const QVariant& entry : entries) {
            if (rowMatches(entry.toMap(), withChip, searchLower,
                           allowedBeverageTypes, favoriteFilenames))
                ++count;
        }
        return count;
    };

    QVariantMap withFavorites = chips;
    withFavorites.insert(QStringLiteral("favorites"), true);
    counts.insert(QStringLiteral("favorites"), countWith(withFavorites));

    const QStringList existingSources = chips.value(QStringLiteral("sources")).toStringList();
    for (const QString& id : {QStringLiteral("builtin"), QStringLiteral("downloaded"),
                               QStringLiteral("mine")}) {
        QVariantMap withChip = chips;
        QStringList sources = existingSources;
        if (!sources.contains(id)) sources.append(id);
        withChip.insert(QStringLiteral("sources"), sources);
        counts.insert(id, countWith(withChip));
    }
    const QStringList existingBeverages = chips.value(QStringLiteral("beverages")).toStringList();
    for (const QString& id : {QStringLiteral("espresso"), QStringLiteral("filter"),
                               QStringLiteral("tea"), QStringLiteral("maintenance")}) {
        QVariantMap withChip = chips;
        QStringList beverages = existingBeverages;
        if (!beverages.contains(id)) beverages.append(id);
        withChip.insert(QStringLiteral("beverages"), beverages);
        counts.insert(id, countWith(withChip));
    }
    return counts;
}

QVariantList sortedByRecentUse(const QVariantList& entries, const QString& pinnedFilename,
                               const QVariantMap& usage)
{
    QVariantList sorted = entries;
    const auto lastUse = [&usage](const QString& title) {
        return usage.value(title).toMap().value(QStringLiteral("lastTimestamp")).toLongLong();
    };
    std::stable_sort(sorted.begin(), sorted.end(), [&](const QVariant& av, const QVariant& bv) {
        const QVariantMap a = av.toMap();
        const QVariantMap b = bv.toMap();
        const bool aPinned = a.value(QStringLiteral("name")).toString() == pinnedFilename;
        const bool bPinned = b.value(QStringLiteral("name")).toString() == pinnedFilename;
        if (aPinned != bPinned) return aPinned;
        const QString aTitle = a.value(QStringLiteral("title")).toString();
        const QString bTitle = b.value(QStringLiteral("title")).toString();
        const qint64 ta = lastUse(aTitle);
        const qint64 tb = lastUse(bTitle);
        if ((ta != 0) != (tb != 0)) return ta != 0;
        if (ta != tb) return ta > tb;
        return QString::localeAwareCompare(aTitle, bTitle) < 0;
    });
    return sorted;
}

} // namespace ProfileCatalogLogic
