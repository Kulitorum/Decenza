#include "steameditrequest.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QStringList>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace SteamEditRequest {
namespace {
struct OperationInfo {
    Operation operation;
    const char* name;
    const char* fields;
};

constexpr OperationInfo kOperations[] = {
    {Operation::SetPresetDuration, "set-preset-duration", "index,expectedName,durationSec"},
    {Operation::SetPresetFlow, "set-preset-flow", "index,expectedName,flow"},
    {Operation::SetPresetTemperature, "set-preset-temperature", "index,expectedName,temperatureC"},
    {Operation::RenamePreset, "rename-preset", "index,expectedName,newName"},
    {Operation::CreatePreset, "create-preset", "name"},
    {Operation::DeletePreset, "delete-preset", "index,expectedName"},
    {Operation::MovePreset, "move-preset", "fromIndex,expectedFromName,toIndex,expectedToName"},
    {Operation::SetPitcherWeight, "set-pitcher-weight", "index,expectedName,weightG"},
    {Operation::StoreCurrentPitcherWeight, "store-current-pitcher-weight", "index,expectedName"},
    {Operation::ClearPitcherWeight, "clear-pitcher-weight", "index,expectedName"},
    {Operation::SetMilkAutoCapture, "set-milk-auto-capture", "enabled"},
    {Operation::SetSteamSecondsPerGram, "set-steam-seconds-per-gram", "value"},
    {Operation::CalibrateFromLastSteam, "calibrate-from-last-steam", ""}
};

void setError(Result* result, const QString& message)
{
    result->valid = false;
    result->httpStatus = 400;
    result->error = message;
}

bool hasExactFields(const QJsonObject& object, const char* commaSeparated)
{
    QStringList expected;
    const QString list = QString::fromLatin1(commaSeparated);
    if (!list.isEmpty())
        expected = list.split(QLatin1Char(','));
    if (object.size() != expected.size())
        return false;
    for (const QString& key : expected) {
        if (!object.contains(key))
            return false;
    }
    return true;
}

bool readInt(const QJsonObject& object, const QString& key, int minimum, int maximum,
             int* output)
{
    const QJsonValue value = object.value(key);
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!value.isDouble() || !std::isfinite(number) || std::trunc(number) != number
        || number < minimum || number > maximum)
        return false;
    *output = static_cast<int>(number);
    return true;
}

bool readFinite(const QJsonObject& object, const QString& key, double minimum,
                double maximum, double* output)
{
    const QJsonValue value = object.value(key);
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!value.isDouble() || !std::isfinite(number)
        || number < minimum || number > maximum)
        return false;
    *output = number;
    return true;
}

bool readNonemptyString(const QJsonObject& object, const QString& key,
                        bool requireTrimmed = false)
{
    const QJsonValue value = object.value(key);
    return value.isString() && !value.toString().trimmed().isEmpty()
        && value.toString().size() <= 128
        && (!requireTrimmed || value.toString() == value.toString().trimmed());
}
} // namespace

QString editorRevision(const SteamPitcherSnapshot& pitchers,
                       const MachineUiSnapshot& machineUi)
{
    if (!std::isfinite(machineUi.brewSteamTemperature)
        || !std::isfinite(machineUi.brewSteamSecondsPerGram))
        return {};
    const QJsonObject ui = machineUi.toJson();
    QJsonObject editorState{
        {QStringLiteral("steamPitchers"), pitchers.toJson()},
        {QStringLiteral("brewSteamTemperature"), ui.value(QStringLiteral("brewSteamTemperature"))},
        {QStringLiteral("brewSteamTimeout"), ui.value(QStringLiteral("brewSteamTimeout"))},
        {QStringLiteral("brewSteamFlow"), ui.value(QStringLiteral("brewSteamFlow"))},
        {QStringLiteral("brewMilkAutoCaptureEnabled"), ui.value(QStringLiteral("brewMilkAutoCaptureEnabled"))},
        {QStringLiteral("brewSteamSecondsPerGram"), ui.value(QStringLiteral("brewSteamSecondsPerGram"))}
    };
    const QByteArray canonical = QJsonDocument(editorState).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

bool isRevision(const QString& revision)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(revision).hasMatch();
}

QString operationName(Operation operation)
{
    for (const OperationInfo& info : kOperations) {
        if (info.operation == operation)
            return QString::fromLatin1(info.name);
    }
    return {};
}

bool operationForName(const QString& name, Operation* operation)
{
    if (!operation)
        return false;
    for (const OperationInfo& info : kOperations) {
        if (name == QLatin1String(info.name)) {
            *operation = info.operation;
            return true;
        }
    }
    return false;
}

Result validateFields(const QString& name, const QJsonObject& fields)
{
    Result result;
    if (!operationForName(name, &result.operation)) {
        setError(&result, QStringLiteral("Unknown Steam editor operation"));
        return result;
    }
    result.operationName = name;
    result.command.operation = result.operation;

    const OperationInfo* info = nullptr;
    for (const OperationInfo& candidate : kOperations) {
        if (candidate.operation == result.operation) {
            info = &candidate;
            break;
        }
    }
    if (!info || !hasExactFields(fields, info->fields)) {
        setError(&result, QStringLiteral("Steam editor operation has invalid fields"));
        return result;
    }

    int index = -1;
    int integer = 0;
    double number = 0.0;
    switch (result.operation) {
    case Operation::SetPresetDuration:
        if (!readInt(fields, QStringLiteral("index"), 0, 10000, &index)
            || !readNonemptyString(fields, QStringLiteral("expectedName"))
            || !readInt(fields, QStringLiteral("durationSec"), 1, 120, &integer))
            break;
        result.valid = true;
        break;
    case Operation::SetPresetFlow:
        if (!readInt(fields, QStringLiteral("index"), 0, 10000, &index)
            || !readNonemptyString(fields, QStringLiteral("expectedName"))
            || !readInt(fields, QStringLiteral("flow"), 40, 250, &integer)
            || integer % 5 != 0)
            break;
        result.valid = true;
        break;
    case Operation::SetPresetTemperature:
        if (!readInt(fields, QStringLiteral("index"), 0, 10000, &index)
            || !readNonemptyString(fields, QStringLiteral("expectedName"))
            || !readFinite(fields, QStringLiteral("temperatureC"), 120.0, 170.0, &number))
            break;
        result.valid = true;
        break;
    case Operation::RenamePreset:
        if (!readInt(fields, QStringLiteral("index"), 0, 10000, &index)
            || !readNonemptyString(fields, QStringLiteral("expectedName"))
            || !readNonemptyString(fields, QStringLiteral("newName"), true))
            break;
        result.valid = true;
        break;
    case Operation::CreatePreset:
        result.valid = readNonemptyString(fields, QStringLiteral("name"), true);
        break;
    case Operation::DeletePreset:
        result.valid = readInt(fields, QStringLiteral("index"), 0, 10000, &index)
            && readNonemptyString(fields, QStringLiteral("expectedName"));
        break;
    case Operation::MovePreset: {
        int from = -1;
        int to = -1;
        result.valid = readInt(fields, QStringLiteral("fromIndex"), 0, 10000, &from)
            && readNonemptyString(fields, QStringLiteral("expectedFromName"))
            && readInt(fields, QStringLiteral("toIndex"), 0, 10000, &to)
            && readNonemptyString(fields, QStringLiteral("expectedToName"));
        break;
    }
    case Operation::SetPitcherWeight:
        result.valid = readInt(fields, QStringLiteral("index"), 0, 10000, &index)
            && readNonemptyString(fields, QStringLiteral("expectedName"))
            && readFinite(fields, QStringLiteral("weightG"), 0.0, 1000.0, &number);
        break;
    case Operation::StoreCurrentPitcherWeight:
    case Operation::ClearPitcherWeight:
        result.valid = readInt(fields, QStringLiteral("index"), 0, 10000, &index)
            && readNonemptyString(fields, QStringLiteral("expectedName"));
        break;
    case Operation::SetMilkAutoCapture:
        result.valid = fields.value(QStringLiteral("enabled")).isBool();
        break;
    case Operation::SetSteamSecondsPerGram:
        result.valid = readFinite(fields, QStringLiteral("value"), 0.0, 2.0, &number);
        break;
    case Operation::CalibrateFromLastSteam:
        result.valid = true;
        break;
    }

    if (!result.valid)
        setError(&result, QStringLiteral("Steam editor operation contains an invalid value"));
    else {
        result.fields = fields;
        switch (result.operation) {
        case Operation::SetPresetDuration:
            readInt(fields, QStringLiteral("index"), 0, 10000, &result.command.index);
            result.command.expectedName = fields.value(QStringLiteral("expectedName")).toString();
            readInt(fields, QStringLiteral("durationSec"), 1, 120, &result.command.durationSec);
            break;
        case Operation::SetPresetFlow:
            readInt(fields, QStringLiteral("index"), 0, 10000, &result.command.index);
            result.command.expectedName = fields.value(QStringLiteral("expectedName")).toString();
            readInt(fields, QStringLiteral("flow"), 40, 250, &result.command.flow);
            break;
        case Operation::SetPresetTemperature:
            readInt(fields, QStringLiteral("index"), 0, 10000, &result.command.index);
            result.command.expectedName = fields.value(QStringLiteral("expectedName")).toString();
            readFinite(fields, QStringLiteral("temperatureC"), 120.0, 170.0,
                       &result.command.temperatureC);
            break;
        case Operation::RenamePreset:
            readInt(fields, QStringLiteral("index"), 0, 10000, &result.command.index);
            result.command.expectedName = fields.value(QStringLiteral("expectedName")).toString();
            result.command.newName = fields.value(QStringLiteral("newName")).toString();
            break;
        case Operation::CreatePreset:
            result.command.newName = fields.value(QStringLiteral("name")).toString();
            break;
        case Operation::DeletePreset:
            readInt(fields, QStringLiteral("index"), 0, 10000, &result.command.index);
            result.command.expectedName = fields.value(QStringLiteral("expectedName")).toString();
            break;
        case Operation::MovePreset:
            readInt(fields, QStringLiteral("fromIndex"), 0, 10000, &result.command.fromIndex);
            result.command.expectedFromName = fields.value(QStringLiteral("expectedFromName")).toString();
            readInt(fields, QStringLiteral("toIndex"), 0, 10000, &result.command.toIndex);
            result.command.expectedToName = fields.value(QStringLiteral("expectedToName")).toString();
            break;
        case Operation::SetPitcherWeight:
            readInt(fields, QStringLiteral("index"), 0, 10000, &result.command.index);
            result.command.expectedName = fields.value(QStringLiteral("expectedName")).toString();
            readFinite(fields, QStringLiteral("weightG"), 0.0, 1000.0, &result.command.weightG);
            break;
        case Operation::StoreCurrentPitcherWeight:
        case Operation::ClearPitcherWeight:
            readInt(fields, QStringLiteral("index"), 0, 10000, &result.command.index);
            result.command.expectedName = fields.value(QStringLiteral("expectedName")).toString();
            break;
        case Operation::SetMilkAutoCapture:
            result.command.enabled = fields.value(QStringLiteral("enabled")).toBool();
            break;
        case Operation::SetSteamSecondsPerGram:
            readFinite(fields, QStringLiteral("value"), 0.0, 2.0, &result.command.value);
            break;
        case Operation::CalibrateFromLastSteam:
            break;
        }
        result.httpStatus = 200;
        result.error.clear();
    }
    return result;
}

} // namespace SteamEditRequest
