#include "shotuploadcommandpolicy.h"

#include <QMetaType>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
#include <limits>

namespace {
constexpr qsizetype kIdentityLimit = 320;
constexpr qsizetype kPasswordLimit = 512;
constexpr qint64 kMaxRecoverySeconds = 366LL * 100 * 24 * 60 * 60;
constexpr qint64 kEarliestEpoch = 946684800; // 2000-01-01 UTC
constexpr qint64 kLatestEpoch = 4102444800LL; // 2100-01-01 UTC

bool exactKeys(const QVariantMap& args, const QStringList& keys)
{
    if (args.size() != keys.size())
        return false;
    for (const QString& key : keys) {
        if (!args.contains(key))
            return false;
    }
    return true;
}

bool accountArgs(const QVariantMap& args, const QString& identityKey)
{
    if (!exactKeys(args, {identityKey, QStringLiteral("password")}))
        return false;
    const QVariant identityValue = args.value(identityKey);
    const QVariant passwordValue = args.value(QStringLiteral("password"));
    if (identityValue.metaType().id() != QMetaType::QString
        || passwordValue.metaType().id() != QMetaType::QString)
        return false;
    const QString identity = identityValue.toString().trimmed();
    const QString password = passwordValue.toString();
    return !identity.isEmpty() && identity.size() <= kIdentityLimit
        && !password.isEmpty() && password.size() <= kPasswordLimit;
}

bool exactEpoch(const QVariant& value, qint64* result)
{
    if (value.metaType().id() == QMetaType::Int || value.metaType().id() == QMetaType::LongLong) {
        *result = value.toLongLong();
        return true;
    }
    if (value.metaType().id() == QMetaType::UInt || value.metaType().id() == QMetaType::ULongLong) {
        const qulonglong n = value.toULongLong();
        if (n > static_cast<qulonglong>(std::numeric_limits<qint64>::max()))
            return false;
        *result = static_cast<qint64>(n);
        return true;
    }
    if (value.metaType().id() != QMetaType::Double)
        return false;
    const double n = value.toDouble();
    if (!std::isfinite(n) || std::floor(n) != n
        || n < static_cast<double>(kEarliestEpoch)
        || n > static_cast<double>(kLatestEpoch))
        return false;
    *result = static_cast<qint64>(n);
    return true;
}

void fail(QString* reason, const QString& value)
{
    if (reason)
        *reason = value;
}
} // namespace

bool ShotUploadCommandPolicy::decode(const QString& operation, const QVariantMap& arguments,
                                     Command* command, QString* reason)
{
    if (!command) {
        fail(reason, QStringLiteral("invalidRequest"));
        return false;
    }
    if (operation == QLatin1String("connectVisualizer")) {
        if (!accountArgs(arguments, QStringLiteral("identity"))) {
            fail(reason, QStringLiteral("invalidArguments"));
            return false;
        }
    } else if (operation == QLatin1String("linkDecent")) {
        if (!accountArgs(arguments, QStringLiteral("email"))) {
            fail(reason, QStringLiteral("invalidArguments"));
            return false;
        }
    } else if (operation == QLatin1String("recoverVisualizer")) {
        if (!exactKeys(arguments, {QStringLiteral("fromEpoch"), QStringLiteral("toEpoch")})) {
            fail(reason, QStringLiteral("invalidArguments"));
            return false;
        }
        qint64 from = 0;
        qint64 to = 0;
        if (!exactEpoch(arguments.value(QStringLiteral("fromEpoch")), &from)
            || !exactEpoch(arguments.value(QStringLiteral("toEpoch")), &to)
            || from < kEarliestEpoch || to > kLatestEpoch || to < from
            || to - from > kMaxRecoverySeconds) {
            fail(reason, QStringLiteral("invalidRange"));
            return false;
        }
    } else if (operation == QLatin1String("chooseDecentMachine")) {
        if (!exactKeys(arguments, {QStringLiteral("serial")})) {
            fail(reason, QStringLiteral("invalidArguments"));
            return false;
        }
        const QVariant serial = arguments.value(QStringLiteral("serial"));
        if (serial.metaType().id() != QMetaType::QString || serial.toString().isEmpty()
            || serial.toString().size() > 64
            || serial.toString().contains(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")))) {
            fail(reason, QStringLiteral("invalidSerial"));
            return false;
        }
    } else if (operation == QLatin1String("uploadMissing")) {
        if (!exactKeys(arguments, {QStringLiteral("destination")})) {
            fail(reason, QStringLiteral("invalidArguments"));
            return false;
        }
        const QVariant destination = arguments.value(QStringLiteral("destination"));
        if (destination.metaType().id() != QMetaType::QString
            || (destination.toString() != QLatin1String("visualizer")
                && destination.toString() != QLatin1String("decent"))) {
            fail(reason, QStringLiteral("invalidDestination"));
            return false;
        }
    } else if (operation == QLatin1String("openDecentAccount")) {
        if (!exactKeys(arguments, {QStringLiteral("receipt")})) {
            fail(reason, QStringLiteral("invalidArguments"));
            return false;
        }
        const QVariant receipt = arguments.value(QStringLiteral("receipt"));
        const QString receiptText = receipt.toString();
        const QUuid parsed = QUuid::fromString(receiptText);
        if (receipt.metaType().id() != QMetaType::QString || parsed.isNull()
            || receiptText != parsed.toString(QUuid::WithoutBraces).toLower()) {
            fail(reason, QStringLiteral("invalidReceipt"));
            return false;
        }
    } else if (operation == QLatin1String("disconnectVisualizer")
               || operation == QLatin1String("unlinkDecent")) {
        if (!arguments.isEmpty()) {
            fail(reason, QStringLiteral("invalidArguments"));
            return false;
        }
    } else {
        fail(reason, QStringLiteral("unknownOperation"));
        return false;
    }

    command->operation = operation;
    command->arguments = arguments;
    if (reason)
        reason->clear();
    return true;
}

bool ShotUploadCommandPolicy::validCompletionOwner(const QString& requestId,
                                                   const QString& operation,
                                                   const QString& expectedId,
                                                   const QString& expectedOperation)
{
    return !expectedId.isEmpty() && requestId == expectedId
        && !expectedOperation.isEmpty() && operation == expectedOperation;
}
