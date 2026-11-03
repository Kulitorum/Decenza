#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>
#include <cmath>
#include <optional>

namespace WaterAmount {

inline QString validationFailure(const QString& message)
{
    return QStringLiteral("invalidWaterAmount:") + message;
}

inline QJsonObject validationResponse(const QString& reason)
{
    const QString prefix = validationFailure({});
    if (!reason.startsWith(prefix)) return {};
    return {{QStringLiteral("code"), QStringLiteral("invalidWaterAmount")},
            {QStringLiteral("error"), reason.mid(prefix.size())}};
}

inline bool isRatio(const QJsonObject& water)
{
    return water.value(QStringLiteral("amountMode")).toString() == QLatin1String("ratio");
}

inline QJsonObject withPresetFallback(const QJsonObject& water, const QJsonObject& preset)
{
    if (water.contains(QStringLiteral("amountMode")) || water.value(QStringLiteral("volume")).toInt() > 0)
        return water;
    auto result = water;
    for (const auto* key : {"volume", "mode", "flowRate"}) {
        const auto name = QLatin1String(key);
        if (preset.contains(name)) result.insert(name, preset.value(name));
    }
    if (preset.contains(QStringLiteral("temperature")))
        result.insert(QStringLiteral("temperatureC"), preset.value(QStringLiteral("temperature")));
    return result;
}

inline QString definitionError(const QJsonObject& water, bool hasProfile, bool requireVessel = true)
{
    if (!water.value(QStringLiteral("hasWater")).toBool())
        return {};
    const QString amountMode = water.value(QStringLiteral("amountMode")).toString();
    if (water.contains(QStringLiteral("amountMode"))
        && (!water.value(QStringLiteral("amountMode")).isString()
            || (amountMode != QLatin1String("fixed") && amountMode != QLatin1String("ratio"))))
        return QStringLiteral("Unknown water amount mode.");
    if (requireVessel && water.value(QStringLiteral("vesselName")).toString().trimmed().isEmpty())
        return QStringLiteral("Choose a vessel for added water.");
    if (!isRatio(water)) {
        if (water.contains(QStringLiteral("waterToEspressoRatio")))
            return QStringLiteral("Choose either a fixed water amount or a water ratio.");
        if (amountMode == QLatin1String("fixed")) {
            const auto amount = water.value(QStringLiteral("volume"));
            if (!amount.isDouble() || !std::isfinite(amount.toDouble())
                || amount.toDouble() <= 0 || amount.toDouble() != std::floor(amount.toDouble()))
                return QStringLiteral("Fixed water amount must be a positive whole number.");
        }
        return {};
    }
    if (!hasProfile)
        return QStringLiteral("Ratio water requires an espresso target yield.");
    const auto ratio = water.value(QStringLiteral("waterToEspressoRatio"));
    if (!ratio.isDouble() || !std::isfinite(ratio.toDouble()) || ratio.toDouble() <= 0)
        return QStringLiteral("Water ratio must be a positive number.");
    if (water.contains(QStringLiteral("volume")))
        return QStringLiteral("Choose either a fixed water amount or a water ratio.");
    if (water.value(QStringLiteral("mode")).toString(QStringLiteral("weight")) != QLatin1String("weight"))
        return QStringLiteral("Ratio water uses weight in grams.");
    return {};
}

struct ProfileDefaults {
    double targetG = 0;
    double doseG = 0; // Zero when the profile's dose recommendation is disabled.
};

struct ValidationContext {
    bool catalogAvailable = false;
    std::optional<double> installedTargetG;
    std::optional<double> installedDoseG = std::nullopt;
};

enum class Kind { Disabled, LegacyPreset, FixedWeight, FixedVolume, Ratio };

struct Specification {
    Kind kind = Kind::Disabled;
    QJsonObject snapshot;
    QString error;
    bool ratio() const { return kind == Kind::Ratio; }
};

inline QJsonObject writableSnapshot(QJsonObject water)
{
    water.remove(QStringLiteral("resolvedTargetG"));
    if (water.value(QStringLiteral("vesselName")).isString())
        water.insert(QStringLiteral("vesselName"), water.value(QStringLiteral("vesselName")).toString().trimmed());
    return water;
}

inline Specification parse(const QJsonObject& water, bool hasProfile = true, bool requireVessel = false)
{
    Specification spec;
    spec.snapshot = writableSnapshot(water);
    spec.error = definitionError(water, hasProfile, requireVessel);
    if (!spec.error.isEmpty() || !water.value(QStringLiteral("hasWater")).toBool()) return spec;
    if (isRatio(water)) spec.kind = Kind::Ratio;
    else if (!water.contains(QStringLiteral("amountMode"))
             && water.value(QStringLiteral("volume")).toInt() <= 0) spec.kind = Kind::LegacyPreset;
    else spec.kind = water.value(QStringLiteral("mode")).toString() == QLatin1String("volume")
        ? Kind::FixedVolume : Kind::FixedWeight;
    return spec;
}

struct Result {
    int amount = 0;
    QString error;
    bool valid() const { return error.isEmpty() && amount > 0; }
};

inline Result resolve(const Specification& spec, double espressoTargetG)
{
    if (!spec.error.isEmpty()) return {0, spec.error};
    if (spec.kind == Kind::Disabled) return {};
    if (spec.kind == Kind::LegacyPreset)
        return {0, QStringLiteral("Choose a vessel with a supported fixed water amount.")};
    if (!spec.ratio()) {
        const int amount = spec.snapshot.value(QStringLiteral("volume")).toInt();
        const int maximum = spec.kind == Kind::FixedVolume ? 255 : 500;
        if (amount < 10 || amount > maximum)
            return {0, QStringLiteral("Fixed water amount is outside the supported range.")};
        return {amount, {}};
    }
    if (!std::isfinite(espressoTargetG) || espressoTargetG <= 0)
        return {0, QStringLiteral("Set a positive espresso target yield to use ratio water.")};
    const double grams = spec.snapshot.value(QStringLiteral("waterToEspressoRatio")).toDouble() * espressoTargetG;
    if (!std::isfinite(grams) || grams < 9.5 || grams >= 500.5)
        return {0, QStringLiteral("Added water must resolve to 10–500 g. Adjust the ratio or espresso target.")};
    return {qRound(grams), {}};
}

inline Result resolve(const QJsonObject& water, double espressoTargetG)
{
    return resolve(parse(water), espressoTargetG);
}

struct Plan {
    Specification spec;
    Result result;
    double espressoTargetG = 0;
    QString mode;
    double temperatureC = 90;
    int flowRate = 40;

    bool enabled() const { return spec.snapshot.value(QStringLiteral("hasWater")).toBool(); }
    bool valid() const { return result.valid(); }
    QJsonObject historySnapshot() const {
        auto water = spec.snapshot;
        if (spec.ratio() && valid()) water.insert(QStringLiteral("resolvedTargetG"), result.amount);
        return water;
    }
};

inline Plan plan(const QJsonObject& water, double espressoTargetG)
{
    Plan value;
    value.spec = parse(water, true, true);
    value.result = resolve(value.spec, espressoTargetG);
    value.espressoTargetG = espressoTargetG;
    value.mode = value.spec.ratio() ? QStringLiteral("weight")
        : water.value(QStringLiteral("mode")).toString(QStringLiteral("weight"));
    value.temperatureC = water.value(QStringLiteral("temperatureC")).toDouble(90);
    value.flowRate = water.value(QStringLiteral("flowRate")).toInt(40);
    return value;
}

} // namespace WaterAmount
