#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Pure filtering/sorting rules for the native and paired profile catalogues.
// Rows use ProfileManager::profileInfoToVariantMap()'s public shape.
namespace ProfileCatalogLogic {

bool matches(const QVariantMap& row, const QVariantMap& chips,
             const QString& searchLower, const QStringList& allowedBeverageTypes,
             const QSet<QString>& favoriteFilenames);
QVariantList filter(const QVariantList& entries, const QVariantMap& chips,
                    const QString& search, const QStringList& allowedBeverageTypes,
                    const QSet<QString>& favoriteFilenames);
QVariantMap facetCounts(const QVariantList& entries, const QVariantMap& chips,
                        const QString& search, const QStringList& allowedBeverageTypes,
                        const QSet<QString>& favoriteFilenames);
QVariantList sortedByRecentUse(const QVariantList& entries, const QString& pinnedFilename,
                               const QVariantMap& usage);

} // namespace ProfileCatalogLogic
