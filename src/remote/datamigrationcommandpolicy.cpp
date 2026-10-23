#include "datamigrationcommandpolicy.h"

#include <QRegularExpression>
#include <QUrl>

bool DataMigrationCommandPolicy::validate(const QString& operation,
                                          const QVariantMap& arguments,
                                          QString* reason)
{
    auto reject = [reason](const QString& why) {
        if (reason) *reason = why;
        return false;
    };
    static const QStringList noArgumentOps{
        QStringLiteral("startDiscovery"), QStringLiteral("stopDiscovery"),
        QStringLiteral("disconnect"), QStringLiteral("importAll"),
        QStringLiteral("importOnlySettings"), QStringLiteral("importOnlyProfiles"),
        QStringLiteral("importOnlyShots"), QStringLiteral("importOnlyMedia"),
        QStringLiteral("importOnlyAIConversations"), QStringLiteral("cancel")};
    if (noArgumentOps.contains(operation))
        return arguments.isEmpty() || reject(QStringLiteral("unexpectedArguments"));
    if (operation == QLatin1String("connectToServer")) {
        const QString address = arguments.value(QStringLiteral("serverUrl")).toString().trimmed();
        if (address.isEmpty() || address.size() > 512)
            return reject(QStringLiteral("invalidAddress"));
        QUrl url(address.contains(QStringLiteral("://")) ? address : QStringLiteral("http://") + address);
        if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
            || url.host().isEmpty() || !url.userInfo().isEmpty() || !url.fragment().isEmpty())
            return reject(QStringLiteral("invalidAddress"));
        return true;
    }
    if (operation == QLatin1String("authenticate")) {
        const QString code = arguments.value(QStringLiteral("totpCode")).toString();
        static const QRegularExpression totp(QStringLiteral("^[0-9]{6}$"));
        if (!totp.match(code).hasMatch())
            return reject(QStringLiteral("invalidCode"));
        return true;
    }
    return reject(QStringLiteral("unknownOperation"));
}

QVariantMap DataMigrationCommandPolicy::safeManifest(const QVariantMap& source)
{
    static const QStringList strings{QStringLiteral("deviceName"), QStringLiteral("platform"),
                                     QStringLiteral("appVersion")};
    static const QStringList integers{QStringLiteral("profileCount"), QStringLiteral("shotCount"),
                                      QStringLiteral("mediaCount"), QStringLiteral("aiConversationCount"),
                                      QStringLiteral("recipeCount"), QStringLiteral("bagCount"),
                                      QStringLiteral("equipmentCount")};
    QVariantMap safe;
    for (const QString& key : strings) {
        QString value = source.value(key).toString().left(128);
        if (!value.isEmpty()) safe.insert(key, value);
    }
    for (const QString& key : integers)
        safe.insert(key, qBound(0, source.value(key).toInt(), 1000000));
    safe.insert(QStringLiteral("hasSettings"), source.value(QStringLiteral("hasSettings")).toBool());
    return safe;
}

QVariantList DataMigrationCommandPolicy::safeDevices(const QVariantList& source)
{
    QVariantList safe;
    for (const QVariant& entry : source) {
        const QVariantMap device = entry.toMap();
        const QString address = device.value(QStringLiteral("serverUrl")).toString().left(512);
        const QUrl url(address);
        if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
            || url.host().isEmpty() || !url.userInfo().isEmpty())
            continue;
        safe.append(QVariantMap{
            {QStringLiteral("deviceName"), device.value(QStringLiteral("deviceName")).toString().left(128)},
            {QStringLiteral("platform"), device.value(QStringLiteral("platform")).toString().left(64)},
            {QStringLiteral("appVersion"), device.value(QStringLiteral("appVersion")).toString().left(64)},
            {QStringLiteral("serverUrl"), address},
            {QStringLiteral("ipAddress"), device.value(QStringLiteral("ipAddress")).toString().left(64)}});
    }
    return safe;
}
