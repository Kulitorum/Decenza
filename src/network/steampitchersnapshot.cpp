#include "steampitchersnapshot.h"

#include "core/steampitcherselection.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QVariantMap>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr auto kPresetFields = std::array<const char*, 8>{
    "name", "duration", "flow", "temperature", "pitcherWeightG",
    "calibMilkG", "disabled", "builtin"
};

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

bool readPresetField(const QJsonObject& object, const QString& key,
                     QVariant* value, QString* error)
{
    const QJsonValue jsonValue = object.value(key);
    if (key == QLatin1String("name")) {
        if (!jsonValue.isString()) {
            setError(error, QStringLiteral("steamPitchers.presets[].name must be a string"));
            return false;
        }
        *value = jsonValue.toString();
        return true;
    }

    if (key == QLatin1String("disabled") || key == QLatin1String("builtin")) {
        if (!jsonValue.isBool()) {
            setError(error, QStringLiteral("steamPitchers.presets[].%1 must be a boolean").arg(key));
            return false;
        }
        *value = jsonValue.toBool();
        return true;
    }

    if (!jsonValue.isDouble() || !std::isfinite(jsonValue.toDouble())) {
        setError(error, QStringLiteral("steamPitchers.presets[].%1 must be a finite number").arg(key));
        return false;
    }
    *value = jsonValue.toDouble();
    return true;
}

QJsonObject presetToJson(const QVariantMap& preset)
{
    QJsonObject object;
    for (const char* field : kPresetFields) {
        const QString key = QString::fromLatin1(field);
        const auto it = preset.constFind(key);
        if (it == preset.cend())
            continue;

        object.insert(key, it.value().isValid()
            ? QJsonValue::fromVariant(it.value()) : QJsonValue(QJsonValue::Null));
    }
    return object;
}

QJsonArray presetsToJson(const QVariantList& presets)
{
    QJsonArray array;
    for (const QVariant& value : presets)
        array.append(presetToJson(value.toMap()));
    return array;
}
} // namespace

QString SteamPitcherSnapshot::revision() const
{
    const QByteArray canonical = QJsonDocument(presetsToJson(presets))
                                     .toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

QJsonObject SteamPitcherSnapshot::toJson() const
{
    return QJsonObject{
        {QStringLiteral("presets"), presetsToJson(presets)},
        {QStringLiteral("selectedIndex"), selectedIndex},
        {QStringLiteral("revision"), revision()}
    };
}

bool SteamPitcherSnapshot::parse(const QJsonObject& object,
                                 SteamPitcherSnapshot* snapshot,
                                 QString* error)
{
    if (!snapshot) {
        setError(error, QStringLiteral("steamPitchers output is null"));
        return false;
    }
    if (object.size() != 3 || !object.contains(QStringLiteral("presets"))
        || !object.contains(QStringLiteral("selectedIndex"))
        || !object.contains(QStringLiteral("revision"))) {
        setError(error, QStringLiteral("steamPitchers must contain exactly presets, selectedIndex and revision"));
        return false;
    }

    const QJsonValue revisionValue = object.value(QStringLiteral("revision"));
    static const QRegularExpression revisionPattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!revisionValue.isString() || !revisionPattern.match(revisionValue.toString()).hasMatch()) {
        setError(error, QStringLiteral("steamPitchers.revision must be a lowercase SHA-256 digest"));
        return false;
    }

    const QJsonValue presetsValue = object.value(QStringLiteral("presets"));
    if (!presetsValue.isArray()) {
        setError(error, QStringLiteral("steamPitchers.presets must be an array"));
        return false;
    }
    const QJsonValue selectedValue = object.value(QStringLiteral("selectedIndex"));
    if (!selectedValue.isDouble()) {
        setError(error, QStringLiteral("steamPitchers.selectedIndex must be an integer"));
        return false;
    }
    const double selectedNumber = selectedValue.toDouble();
    if (!std::isfinite(selectedNumber) || std::trunc(selectedNumber) != selectedNumber
        || selectedNumber < std::numeric_limits<int>::min()
        || selectedNumber > std::numeric_limits<int>::max()) {
        setError(error, QStringLiteral("steamPitchers.selectedIndex must be an integer"));
        return false;
    }

    QVariantList parsedPresets;
    const QJsonArray sourcePresets = presetsValue.toArray();
    parsedPresets.reserve(sourcePresets.size());
    for (qsizetype index = 0; index < sourcePresets.size(); ++index) {
        const QJsonValue presetValue = sourcePresets.at(index);
        if (!presetValue.isObject()) {
            setError(error, QStringLiteral("steamPitchers.presets[%1] must be an object").arg(index));
            return false;
        }

        const QJsonObject presetObject = presetValue.toObject();
        QVariantMap preset;
        for (auto it = presetObject.constBegin(); it != presetObject.constEnd(); ++it) {
            const QString key = it.key();
            bool known = false;
            for (const char* field : kPresetFields) {
                if (key == QLatin1String(field)) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                setError(error, QStringLiteral("steamPitchers.presets[%1] has unknown field %2")
                                    .arg(index).arg(key));
                return false;
            }

            QVariant value;
            if (!readPresetField(presetObject, key, &value, error))
                return false;
            preset.insert(key, value);
        }
        parsedPresets.append(preset);
    }

    if (parsedPresets.isEmpty()) {
        setError(error, QStringLiteral("steamPitchers.presets must include the synthetic Heater off row"));
        return false;
    }
    const QVariantMap heaterOff = parsedPresets.constLast().toMap();
    if (heaterOff.value(QStringLiteral("disabled")).metaType().id() != QMetaType::Bool
        || !heaterOff.value(QStringLiteral("disabled")).toBool()
        || heaterOff.value(QStringLiteral("builtin")).metaType().id() != QMetaType::Bool
        || !heaterOff.value(QStringLiteral("builtin")).toBool()) {
        setError(error, QStringLiteral("steamPitchers.presets must end with the synthetic Heater off row"));
        return false;
    }

    const int selectedIndex = static_cast<int>(selectedNumber);
    SteamPitcherSnapshot parsed;
    parsed.presets = std::move(parsedPresets);
    parsed.selectedIndex = selectedIndex;
    if (parsed.revision() != revisionValue.toString()) {
        setError(error, QStringLiteral("steamPitchers.revision does not match the ordered presets"));
        return false;
    }
    *snapshot = std::move(parsed);
    if (error)
        error->clear();
    return true;
}

SteamPitcherSnapshot SteamPitcherSnapshot::project(const QVariantList& sourcePresets,
                                                   int sourceSelectedIndex)
{
    SteamPitcherSnapshot projected;
    projected.selectedIndex = sourceSelectedIndex;
    projected.presets.reserve(sourcePresets.size());

    for (const QVariant& value : sourcePresets) {
        const QVariantMap source = value.toMap();
        QVariantMap preset;
        for (const char* field : kPresetFields) {
            const QString key = QString::fromLatin1(field);
            const auto it = source.constFind(key);
            if (it != source.cend())
                preset.insert(key, it.value());
        }
        projected.presets.append(preset);
    }
    return projected;
}

QVariantMap SteamPitcherSnapshot::presetAt(int index) const
{
    return SteamPitcherSelection::presetAt(presets, index);
}

int SteamPitcherSnapshot::displayIndex() const
{
    return SteamPitcherSelection::displayIndex(presets, selectedIndex);
}
