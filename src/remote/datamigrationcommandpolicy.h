#pragma once

#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QString>

class DataMigrationCommandPolicy final
{
public:
    static bool validate(const QString& operation, const QVariantMap& arguments,
                         QString* reason = nullptr);
    static QVariantMap safeManifest(const QVariantMap& source);
    static QVariantList safeDevices(const QVariantList& source);
};
