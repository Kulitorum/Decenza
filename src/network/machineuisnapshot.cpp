#include "machineuisnapshot.h"

#include "../core/portalstate.h"

#include <QJsonValue>
#include <QUuid>

#include <cmath>
#include <limits>
#include <utility>

namespace {
struct PhaseName {
    MachinePhase phase;
    const char* name;
};

const PhaseName kPhaseNames[] = {
#define DECENZA_PHASE_NAME(name) {MachinePhase::name, #name},
    DECENZA_MACHINE_PHASES(DECENZA_PHASE_NAME)
#undef DECENZA_PHASE_NAME
};

QString phaseName(MachinePhase phase)
{
    for (const PhaseName& entry : kPhaseNames) {
        if (entry.phase == phase)
            return QString::fromLatin1(entry.name);
    }
    return {};
}

bool parsePhase(const QJsonValue& value, MachinePhase* phase)
{
    if (!value.isString() || !phase)
        return false;
    const QString name = value.toString();
    for (const PhaseName& entry : kPhaseNames) {
        if (name == QLatin1String(entry.name)) {
            *phase = entry.phase;
            return true;
        }
    }
    return false;
}

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

bool readBool(const QJsonObject& object, const QString& key, bool* value, QString* error)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isBool()) {
        setError(error, QStringLiteral("machineUi.%1 must be a boolean").arg(key));
        return false;
    }
    *value = jsonValue.toBool();
    return true;
}

bool readString(const QJsonObject& object, const QString& key, QString* value, QString* error)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isString()) {
        setError(error, QStringLiteral("machineUi.%1 must be a string").arg(key));
        return false;
    }
    *value = jsonValue.toString();
    return true;
}

bool readDouble(const QJsonObject& object, const QString& key, double* value, QString* error)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble() || !std::isfinite(jsonValue.toDouble())) {
        setError(error, QStringLiteral("machineUi.%1 must be a finite number").arg(key));
        return false;
    }
    *value = jsonValue.toDouble();
    return true;
}

bool readInt(const QJsonObject& object, const QString& key, int* value, QString* error)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble()) {
        setError(error, QStringLiteral("machineUi.%1 must be an integer").arg(key));
        return false;
    }
    const double number = jsonValue.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max()) {
        setError(error, QStringLiteral("machineUi.%1 must be an integer").arg(key));
        return false;
    }
    *value = static_cast<int>(number);
    return true;
}

bool readPhase(const QJsonObject& object, const QString& key,
               MachinePhase* value, QString* error)
{
    if (!parsePhase(object.value(key), value)) {
        setError(error, QStringLiteral("machineUi.%1 is an unknown phase").arg(key));
        return false;
    }
    return true;
}
} // namespace

QJsonObject MachineUiSnapshot::toJson() const
{
    QJsonObject object;
#define DECENZA_ENCODE_BOOL_FIELD(name) object.insert(QStringLiteral(#name), name);
#define DECENZA_ENCODE_STRING_FIELD(name, defaultValue) object.insert(QStringLiteral(#name), name);
#define DECENZA_ENCODE_DOUBLE_FIELD(name) object.insert(QStringLiteral(#name), name);
#define DECENZA_ENCODE_INT_FIELD(name, defaultValue) object.insert(QStringLiteral(#name), name);
#define DECENZA_ENCODE_PHASE_FIELD(name) object.insert(QStringLiteral(#name), phaseName(name));
    DECENZA_MACHINE_UI_SNAPSHOT_FIELDS(
        DECENZA_ENCODE_BOOL_FIELD,
        DECENZA_ENCODE_STRING_FIELD,
        DECENZA_ENCODE_DOUBLE_FIELD,
        DECENZA_ENCODE_INT_FIELD,
        DECENZA_ENCODE_PHASE_FIELD)
#undef DECENZA_ENCODE_BOOL_FIELD
#undef DECENZA_ENCODE_STRING_FIELD
#undef DECENZA_ENCODE_DOUBLE_FIELD
#undef DECENZA_ENCODE_INT_FIELD
#undef DECENZA_ENCODE_PHASE_FIELD
    return object;
}

bool MachineUiSnapshot::parse(const QJsonObject& object, MachineUiSnapshot* snapshot,
                              QString* error)
{
    if (!snapshot) {
        setError(error, QStringLiteral("machineUi destination is null"));
        return false;
    }

    MachineUiSnapshot parsed;
#define DECENZA_PARSE_BOOL_FIELD(name) \
    if (!readBool(object, QStringLiteral(#name), &parsed.name, error)) return false;
#define DECENZA_PARSE_STRING_FIELD(name, defaultValue) \
    if (!readString(object, QStringLiteral(#name), &parsed.name, error)) return false;
#define DECENZA_PARSE_DOUBLE_FIELD(name) \
    if (!readDouble(object, QStringLiteral(#name), &parsed.name, error)) return false;
#define DECENZA_PARSE_INT_FIELD(name, defaultValue) \
    if (!readInt(object, QStringLiteral(#name), &parsed.name, error)) return false;
#define DECENZA_PARSE_PHASE_FIELD(name) \
    if (!readPhase(object, QStringLiteral(#name), &parsed.name, error)) return false;
    DECENZA_MACHINE_UI_SNAPSHOT_FIELDS(
        DECENZA_PARSE_BOOL_FIELD,
        DECENZA_PARSE_STRING_FIELD,
        DECENZA_PARSE_DOUBLE_FIELD,
        DECENZA_PARSE_INT_FIELD,
        DECENZA_PARSE_PHASE_FIELD)
#undef DECENZA_PARSE_BOOL_FIELD
#undef DECENZA_PARSE_STRING_FIELD
#undef DECENZA_PARSE_DOUBLE_FIELD
#undef DECENZA_PARSE_INT_FIELD
#undef DECENZA_PARSE_PHASE_FIELD

    // STATE_INFO carries each code in one byte, including firmware error codes.
    if (parsed.de1State < 0 || parsed.de1State > 255
        || parsed.de1SubState < 0 || parsed.de1SubState > 255) {
        setError(error, QStringLiteral("machineUi DE1 state codes must fit an unsigned byte"));
        return false;
    }

    if (parsed.steamAutoFlushCountdown < 0.0) {
        setError(error, QStringLiteral("machineUi.steamAutoFlushCountdown cannot be negative"));
        return false;
    }

    if (parsed.refillKitDetected < -1 || parsed.refillKitDetected > 1) {
        setError(error, QStringLiteral("machineUi.refillKitDetected must be -1, 0, or 1"));
        return false;
    }
    if (parsed.scaleBatteryLevel < -1 || parsed.scaleBatteryLevel > 100) {
        setError(error, QStringLiteral("machineUi.scaleBatteryLevel must be -1 or 0..100"));
        return false;
    }
    if (parsed.appWaterLevelDisplayUnit != QStringLiteral("percent")
        && parsed.appWaterLevelDisplayUnit != QStringLiteral("ml")) {
        setError(error, QStringLiteral("machineUi.appWaterLevelDisplayUnit must be percent or ml"));
        return false;
    }
    if (parsed.appRefillKitOverride < 0 || parsed.appRefillKitOverride > 2) {
        setError(error, QStringLiteral("machineUi.appRefillKitOverride must be 0, 1, or 2"));
        return false;
    }
    if (!isPortalStateName(parsed.portalState)) {
        setError(error, QStringLiteral("machineUi.portalState is unknown"));
        return false;
    }
    if (!parsed.controlContextId.isEmpty()) {
        const QUuid controlContext(parsed.controlContextId);
        if (controlContext.isNull()
            || controlContext.toString(QUuid::WithoutBraces) != parsed.controlContextId) {
            setError(error, QStringLiteral("machineUi.controlContextId must be a canonical UUID or empty"));
            return false;
        }
    }

    *snapshot = std::move(parsed);
    if (error)
        error->clear();
    return true;
}
