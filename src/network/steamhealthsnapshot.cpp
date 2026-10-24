#include "steamhealthsnapshot.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QUuid>

#include <cmath>
#include <initializer_list>
#include <utility>

namespace SteamHealthSnapshot {
namespace {
bool exactKeys(const QJsonObject& object, std::initializer_list<QString> keys)
{
    if (object.size() != static_cast<qsizetype>(keys.size()))
        return false;
    for (const QString& key : keys) {
        if (!object.contains(key))
            return false;
    }
    return true;
}

bool canonicalSessionId(const QString& sessionId)
{
    if (sessionId.isEmpty())
        return true;
    const QUuid parsed(sessionId);
    return !parsed.isNull()
        && parsed.toString(QUuid::WithoutBraces).toLower() == sessionId;
}

bool readId(const QJsonValue& value, qint64* id)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < 0.0
        || number > static_cast<double>(MaximumExactJsonInteger)) {
        return false;
    }
    *id = static_cast<qint64>(number);
    return true;
}

QJsonObject warningJson(const Warning& warning, bool includeMessage)
{
    QJsonObject result{
        {QStringLiteral("id"), static_cast<double>(warning.id)},
        {QStringLiteral("sessionId"), warning.sessionId},
        {QStringLiteral("kind"), kindName(warning.kind)}
    };
    if (includeMessage)
        result[QStringLiteral("message")] = warning.message;
    return result;
}

bool parseWarning(const QJsonObject& object, bool includeMessage, Warning* warning)
{
    const auto keys = includeMessage
        ? std::initializer_list<QString>{QStringLiteral("id"), QStringLiteral("sessionId"),
                                         QStringLiteral("kind"), QStringLiteral("message")}
        : std::initializer_list<QString>{QStringLiteral("id"), QStringLiteral("sessionId"),
                                         QStringLiteral("kind")};
    if (!exactKeys(object, keys))
        return false;

    Warning parsed;
    if (!readId(object.value(QStringLiteral("id")), &parsed.id) || parsed.id <= 0)
        return false;
    const QJsonValue sessionValue = object.value(QStringLiteral("sessionId"));
    if (!sessionValue.isString())
        return false;
    parsed.sessionId = sessionValue.toString();
    if (parsed.sessionId.isEmpty() || !canonicalSessionId(parsed.sessionId))
        return false;
    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    if (!kindValue.isString() || !kindFromName(kindValue.toString(), &parsed.kind))
        return false;
    if (includeMessage) {
        const QJsonValue messageValue = object.value(QStringLiteral("message"));
        if (!messageValue.isString())
            return false;
        parsed.message = messageValue.toString();
        if (parsed.message.size() > MaximumMessageLength)
            return false;
        if ((isLiveKind(parsed.kind) || parsed.kind == Kind::Descale) != parsed.message.isEmpty())
            return false;
    }
    *warning = parsed;
    return true;
}
} // namespace

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::PressureTooHigh: return QStringLiteral("pressure-too-high");
    case Kind::TemperatureTooHigh: return QStringLiteral("temperature-too-high");
    case Kind::Descale: return QStringLiteral("descale");
    case Kind::TemperatureWarning: return QStringLiteral("temperature-warning");
    case Kind::ScaleBuildup: return QStringLiteral("scale-buildup");
    }
    return {};
}

bool kindFromName(const QString& name, Kind* kind)
{
    if (!kind)
        return false;
    if (name == QLatin1String("pressure-too-high")) *kind = Kind::PressureTooHigh;
    else if (name == QLatin1String("temperature-too-high")) *kind = Kind::TemperatureTooHigh;
    else if (name == QLatin1String("descale")) *kind = Kind::Descale;
    else if (name == QLatin1String("temperature-warning")) *kind = Kind::TemperatureWarning;
    else if (name == QLatin1String("scale-buildup")) *kind = Kind::ScaleBuildup;
    else return false;
    return true;
}

bool isLiveKind(Kind kind)
{
    return kind == Kind::PressureTooHigh || kind == Kind::TemperatureTooHigh;
}

qsizetype encodedEventArrayBytes(const QList<Warning>& events)
{
    QJsonArray array;
    for (const Warning& warning : events)
        array.append(warningJson(warning, true));
    return QJsonDocument(array).toJson(QJsonDocument::Compact).size();
}

bool appendBoundedEvent(Snapshot* snapshot, const Warning& warning)
{
    if (!snapshot || encodedEventArrayBytes({warning}) > MaximumEventLedgerBytes)
        return false;

    snapshot->events.append(warning);
    while (snapshot->events.size() > MaximumEvents
           || encodedEventArrayBytes(snapshot->events) > MaximumEventLedgerBytes) {
        snapshot->events.removeFirst();
    }
    snapshot->latestEventId = warning.id;
    snapshot->oldestEventId = snapshot->events.isEmpty() ? 0 : snapshot->events.first().id;
    return true;
}

QJsonObject Snapshot::toJson() const
{
    QJsonArray active;
    for (const Warning& warning : activeWarnings)
        active.append(warningJson(warning, false));
    QJsonArray eventArray;
    for (const Warning& warning : events)
        eventArray.append(warningJson(warning, true));
    return {
        {QStringLiteral("type"), QStringLiteral("steamHealthSnapshot")},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("sessionActive"), sessionActive},
        {QStringLiteral("oldestEventId"), static_cast<double>(oldestEventId)},
        {QStringLiteral("latestEventId"), static_cast<double>(latestEventId)},
        {QStringLiteral("activeWarnings"), active},
        {QStringLiteral("events"), eventArray}
    };
}

bool Snapshot::parse(const QJsonObject& object, Snapshot* snapshot, QString* error)
{
    if (!snapshot) {
        if (error) *error = QStringLiteral("Missing steam health snapshot output");
        return false;
    }
    if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("protocolVersion"),
                            QStringLiteral("sessionId"), QStringLiteral("sessionActive"),
                            QStringLiteral("oldestEventId"), QStringLiteral("latestEventId"),
                            QStringLiteral("activeWarnings"), QStringLiteral("events")})) {
        if (error) *error = QStringLiteral("Steam health snapshot has invalid fields");
        return false;
    }

    Snapshot parsed;
    const QJsonValue protocolVersion = object.value(QStringLiteral("protocolVersion"));
    if (object.value(QStringLiteral("type")).toString() != QStringLiteral("steamHealthSnapshot")
        || !protocolVersion.isDouble() || protocolVersion.toDouble() != 1.0
        || !object.value(QStringLiteral("sessionId")).isString()
        || !object.value(QStringLiteral("sessionActive")).isBool()
        || !readId(object.value(QStringLiteral("oldestEventId")), &parsed.oldestEventId)
        || !readId(object.value(QStringLiteral("latestEventId")), &parsed.latestEventId)
        || !object.value(QStringLiteral("activeWarnings")).isArray()
        || !object.value(QStringLiteral("events")).isArray()) {
        if (error) *error = QStringLiteral("Steam health snapshot has invalid values");
        return false;
    }
    parsed.sessionId = object.value(QStringLiteral("sessionId")).toString();
    parsed.sessionActive = object.value(QStringLiteral("sessionActive")).toBool();
    if (!canonicalSessionId(parsed.sessionId)
        || (parsed.sessionActive && parsed.sessionId.isEmpty())) {
        if (error) *error = QStringLiteral("Steam health session identity is invalid");
        return false;
    }

    const QJsonArray active = object.value(QStringLiteral("activeWarnings")).toArray();
    const QJsonArray events = object.value(QStringLiteral("events")).toArray();
    if (active.size() > MaximumActiveWarnings || events.size() > MaximumEvents) {
        if (error) *error = QStringLiteral("Steam health snapshot exceeds its limits");
        return false;
    }

    for (const QJsonValue& item : active) {
        Warning warning;
        if (!item.isObject() || !parseWarning(item.toObject(), false, &warning)
            || !isLiveKind(warning.kind) || warning.sessionId != parsed.sessionId) {
            if (error) *error = QStringLiteral("Steam active warning is invalid");
            return false;
        }
        for (const Warning& existing : std::as_const(parsed.activeWarnings)) {
            if (existing.id == warning.id || existing.kind == warning.kind) {
                if (error) *error = QStringLiteral("Steam active warnings are duplicated");
                return false;
            }
        }
        parsed.activeWarnings.append(warning);
    }

    qint64 previousId = 0;
    for (const QJsonValue& item : events) {
        Warning warning;
        if (!item.isObject() || !parseWarning(item.toObject(), true, &warning)
            || warning.id <= previousId) {
            if (error) *error = QStringLiteral("Steam warning event is invalid");
            return false;
        }
        previousId = warning.id;
        parsed.events.append(warning);
    }

    if (encodedEventArrayBytes(parsed.events) > MaximumEventLedgerBytes) {
        if (error) *error = QStringLiteral("Steam warning event ledger exceeds its byte limit");
        return false;
    }

    if (parsed.events.isEmpty()) {
        if (parsed.oldestEventId != 0 || parsed.latestEventId != 0) {
            if (error) *error = QStringLiteral("Steam warning event range is invalid");
            return false;
        }
    } else if (parsed.oldestEventId != parsed.events.first().id
               || parsed.latestEventId != parsed.events.last().id) {
        if (error) *error = QStringLiteral("Steam warning event range does not match its events");
        return false;
    }
    for (const Warning& activeWarning : std::as_const(parsed.activeWarnings)) {
        if (activeWarning.id > parsed.latestEventId) {
            if (error) *error = QStringLiteral("Steam active warning is newer than the event ledger");
            return false;
        }
    }

    *snapshot = parsed;
    if (error) error->clear();
    return true;
}

} // namespace SteamHealthSnapshot
