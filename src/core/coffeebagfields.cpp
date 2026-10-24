#include "coffeebagfields.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <utility>

int CoffeeBagFields::daysSince(const QString& iso, const QDate& today)
{
    const QDate date = QDate::fromString(iso.left(10), QStringLiteral("yyyy-MM-dd"));
    if (!date.isValid() || date > today)
        return -1;
    return static_cast<int>(date.daysTo(today));
}

QVariantList CoffeeBagFields::lifecycleParts(const QVariantMap& bag, const QDate& today)
{
    QVariantList parts;
    // Not `kind`: that names a CoffeeBag field, and the NDK's clang flags the
    // shadow (-Werror) where Apple clang 21 doesn't.
    auto add = [&](const char* part, const QString& date) {
        parts << QVariantMap{{QStringLiteral("kind"), QString::fromLatin1(part)},
                             {QStringLiteral("date"), date},
                             {QStringLiteral("ageDays"), CoffeeBagFields::daysSince(date, today)}};
    };
    const QString roast = bag.value(QStringLiteral("roastDate")).toString();
    const QString frozen = bag.value(QStringLiteral("frozenDate")).toString();
    const QString defrost = bag.value(QStringLiteral("defrostDate")).toString();
    const QString opened = bag.value(QStringLiteral("openedDate")).toString();
    if (!roast.isEmpty())
        add("roasted", roast);
    // The freezer part is this portion's thaw, or "frozen" until one is pulled.
    // A thaw date that can't be aged (future, unparsable) shows nothing rather
    // than a misleading "frozen".
    if (!defrost.isEmpty()) {
        if (CoffeeBagFields::daysSince(defrost, today) >= 0)
            add("thawed", defrost);
    } else if (!frozen.isEmpty()) {
        add("frozen", frozen);
    }
    // Independent of the freezer part: a thawed portion is opened too.
    if (!opened.isEmpty() && CoffeeBagFields::daysSince(opened, today) >= 0)
        add("opened", opened);
    return parts;
}

QVariantMap CoffeeBagFields::restockTemplate(QVariantMap bag, const QDate& today)
{
    const bool wasFrozen = !bag.value(QStringLiteral("frozenDate")).toString().isEmpty();
    for (const char* key : {"id", "roastDate", "frozenDate", "defrostDate", "openedDate", "notes",
                            "startWeightG", "inInventory", "lastUsedEpoch", "shotCount", "actions",
                            "lifecycleParts"})
        bag.remove(QString::fromLatin1(key));
    if (wasFrozen)
        bag.insert(QStringLiteral("frozenDate"), today.toString(Qt::ISODate));
    return bag;
}

QVariantMap CoffeeBagFields::editChanges(const QVariantMap& opened, const QVariantMap& current,
                                         bool replaceBlob)
{
    QVariantMap changed;
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        if (!opened.contains(it.key()) || opened.value(it.key()) != it.value())
            changed.insert(it.key(), it.value());
    }
    const QString blobKey = QStringLiteral("beanBaseData");
    if (replaceBlob || !changed.contains(blobKey))
        return changed;
    const QJsonDocument before = QJsonDocument::fromJson(opened.value(blobKey).toString().toUtf8());
    const QJsonDocument after = QJsonDocument::fromJson(changed.value(blobKey).toString().toUtf8());
    const bool beforeOk = opened.value(blobKey).toString().isEmpty() || before.isObject();
    const bool afterOk = changed.value(blobKey).toString().isEmpty() || after.isObject();
    if (!beforeOk || !afterOk)
        return changed;
    const QJsonObject a = before.object();
    const QJsonObject b = after.object();
    QVariantMap patch;
    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        if (a.value(it.key()) != it.value())
            patch.insert(it.key(), it.value().toVariant());
    for (auto it = a.constBegin(); it != a.constEnd(); ++it)
        if (!b.contains(it.key()))
            patch.insert(it.key(), QVariant());
    changed.remove(blobKey);
    if (!patch.isEmpty())
        changed.insert(QStringLiteral("beanBaseDataPatch"), patch);
    return changed;
}

