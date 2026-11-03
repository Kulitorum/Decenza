#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>
#include <optional>

struct ShotWaterResult {
    QString status;
    QString amountSource = QStringLiteral("unavailable");
    std::optional<double> weightG;
    std::optional<double> volumeMl;
    std::optional<double> temperatureC;

    bool isRecorded() const {
        return status == QLatin1String("dispensed") || status == QLatin1String("interrupted")
            || status == QLatin1String("notDispensed");
    }
    bool isComplete() const { return status == QLatin1String("dispensed"); }

    QVariantMap toMap() const {
        if (!isRecorded()) return {};
        QVariantMap result{{QStringLiteral("status"), status},
                           {QStringLiteral("amountSource"), QStringLiteral("unavailable")}};
        if (status == QLatin1String("notDispensed")) return result;
        const auto validAmount = [](const std::optional<double>& value) {
            return value && std::isfinite(*value) && *value >= 0;
        };
        if (amountSource == QLatin1String("scale") && validAmount(weightG) && !volumeMl) {
            result.insert(QStringLiteral("weightG"), *weightG);
            result.insert(QStringLiteral("amountSource"), amountSource);
        } else if (amountSource == QLatin1String("flowEstimate") && validAmount(volumeMl) && !weightG) {
            result.insert(QStringLiteral("volumeMl"), *volumeMl);
            result.insert(QStringLiteral("amountSource"), amountSource);
        }
        if (temperatureC && std::isfinite(*temperatureC) && *temperatureC >= 0 && *temperatureC <= 100)
            result.insert(QStringLiteral("temperatureC"), *temperatureC);
        return result;
    }
    QString toJson() const {
        const auto map = toMap();
        return map.isEmpty() ? QString() : QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));
    }
    static ShotWaterResult fromMap(const QVariantMap& map) {
        ShotWaterResult result;
        result.status = map.value(QStringLiteral("status")).toString();
        result.amountSource = map.value(QStringLiteral("amountSource")).toString();
        const auto number = [&](const char* key) -> std::optional<double> {
            const auto value = QJsonValue::fromVariant(map.value(QLatin1String(key)));
            return value.isDouble() ? std::optional<double>(value.toDouble()) : std::nullopt;
        };
        result.weightG = number("weightG");
        result.volumeMl = number("volumeMl");
        result.temperatureC = number("temperatureC");
        return result;
    }
    static ShotWaterResult fromJson(const QString& json) {
        return fromMap(QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap());
    }
    static ShotWaterResult notDispensed() {
        ShotWaterResult result;
        result.status = QStringLiteral("notDispensed");
        return result;
    }
    // Normalize legacy intent once on the host; never expose its target as an outcome.
    static QVariantMap legacySetting(const QString& spec) {
        const auto water = QJsonDocument::fromJson(spec.toUtf8()).object();
        if (!water.value(QStringLiteral("hasWater")).toBool()) return {};
        QVariantMap setting{{QStringLiteral("hasWater"), true}};
        const auto temp = water.value(QStringLiteral("temperatureC"));
        if (temp.isDouble() && std::isfinite(temp.toDouble()) && temp.toDouble() >= 0 && temp.toDouble() <= 100)
            setting.insert(QStringLiteral("temperatureC"), temp.toDouble());
        return setting;
    }
    // Legacy spec temperature is intent; its amount is never a measured result.
    static std::optional<double> setting(const QVariantMap& result, const QString& spec) {
        const auto normalized = fromMap(result).toMap();
        if (!normalized.isEmpty()) {
            if (normalized.contains(QStringLiteral("temperatureC")))
                return normalized.value(QStringLiteral("temperatureC")).toDouble();
            return std::nullopt;
        }
        const auto water = legacySetting(spec);
        if (water.contains(QStringLiteral("temperatureC")))
            return water.value(QStringLiteral("temperatureC")).toDouble();
        return std::nullopt;
    }
};
