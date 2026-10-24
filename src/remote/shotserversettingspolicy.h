#pragma once

#include <QRegularExpression>
#include <QHash>
#include <QMetaType>
#include <QString>
#include <QVariantMap>
#include <QUuid>

namespace ShotServerSettingsPolicy {
inline bool validRequestId(const QString& value)
{
    const QUuid parsed = QUuid::fromString(value);
    return !parsed.isNull() && value == parsed.toString(QUuid::WithoutBraces).toLower();
}

inline bool validCommand(const QString& operation, const QVariantMap& args)
{
    if (operation == QLatin1String("generateTotpSetup")
        || operation == QLatin1String("resetTotpSecret")
        || operation == QLatin1String("factoryResetAndQuit"))
        return args.isEmpty();
    if (operation != QLatin1String("completeTotpSetup") || args.size() != 2
        || !args.contains(QStringLiteral("secret")) || !args.contains(QStringLiteral("code")))
        return false;
    if (args.value(QStringLiteral("secret")).metaType().id() != QMetaType::QString
        || args.value(QStringLiteral("code")).metaType().id() != QMetaType::QString)
        return false;
    static const QRegularExpression secret(QStringLiteral("^[A-Z2-7]{16,64}$"));
    static const QRegularExpression code(QStringLiteral("^[0-9]{6}$"));
    return secret.match(args.value(QStringLiteral("secret")).toString()).hasMatch()
        && code.match(args.value(QStringLiteral("code")).toString()).hasMatch();
}

inline bool matchesPending(const QHash<QString, QString>& pending,
                           const QString& requestId, const QString& operation)
{
    return pending.contains(requestId) && pending.value(requestId) == operation;
}
} // namespace ShotServerSettingsPolicy
