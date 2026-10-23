#pragma once

#include "profile.h"
#include "temperaturedisplay.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

#include <cmath>

// The small profile-number snapshot used by RecipeDrinkCard. Installed
// profiles are normalized through Profile::fromJson; a recipe's frozen copy
// follows the card's JSON Number() behavior instead.
struct RecipeProfileDisplayNumbers {
    bool valid = true;
    double baseTempC = 0.0;
    double yieldG = 0.0;
    QVector<double> stepTempsC;
};

namespace RecipeProfileDisplay {

inline void addTemperatureFallback(RecipeProfileDisplayNumbers& numbers)
{
    if (numbers.stepTempsC.isEmpty() && numbers.baseTempC > 0.0)
        numbers.stepTempsC.append(numbers.baseTempC);
}

inline RecipeProfileDisplayNumbers fromProfile(const Profile& profile)
{
    RecipeProfileDisplayNumbers numbers;
    numbers.baseTempC = profile.espressoTemperature();
    numbers.yieldG = profile.targetWeight();
    const QList<ProfileFrame>& steps = profile.steps();
    if (!std::isfinite(numbers.baseTempC) || numbers.baseTempC < 0.0
        || !std::isfinite(numbers.yieldG) || numbers.yieldG < 0.0
        || steps.size() > Profile::MAX_FRAMES) {
        numbers.valid = false;
        return numbers;
    }
    numbers.stepTempsC.reserve(steps.size());
    for (const ProfileFrame& frame : steps) {
        if (!std::isfinite(frame.temperature)) {
            numbers.valid = false;
            numbers.stepTempsC.clear();
            return numbers;
        }
        if (frame.temperature > 0.0)
            numbers.stepTempsC.append(frame.temperature);
    }
    addTemperatureFallback(numbers);
    return numbers;
}

// Mirrors RecipesPage._numbersFromProfileObj for a frozen recipe profile:
// numeric strings are accepted, non-positive frame temperatures are omitted,
// trajectory order is preserved, and the base temperature is the fallback.
inline RecipeProfileDisplayNumbers fromFrozenJson(const QJsonObject& object)
{
    auto jsNumber = [](const QJsonValue& value) {
        if (value.isDouble())
            return std::isfinite(value.toDouble()) ? value.toDouble() : 0.0;
        if (value.isBool())
            return value.toBool() ? 1.0 : 0.0;
        if (value.isString()) {
            bool ok = false;
            const double number = value.toString().trimmed().toDouble(&ok);
            return ok && std::isfinite(number) ? number : 0.0;
        }
        return 0.0;
    };

    RecipeProfileDisplayNumbers numbers;
    numbers.baseTempC = jsNumber(object.value(QStringLiteral("espresso_temperature")));
    numbers.yieldG = jsNumber(object.value(QStringLiteral("target_weight")));
    const QJsonValue stepsValue = object.value(QStringLiteral("steps"));
    if (numbers.baseTempC < 0.0 || numbers.yieldG < 0.0
        || (!stepsValue.isUndefined() && !stepsValue.isArray())) {
        numbers.valid = false;
        return numbers;
    }
    const QJsonArray steps = stepsValue.toArray();
    if (steps.size() > Profile::MAX_FRAMES) {
        numbers.valid = false;
        return numbers;
    }
    numbers.stepTempsC.reserve(steps.size());
    for (qsizetype i = 0; i < steps.size(); ++i) {
        const QJsonValue step = steps.at(i);
        if (!step.isObject())
            continue;
        const double temperature = jsNumber(step.toObject().value(QStringLiteral("temperature")));
        if (temperature > 0.0)
            numbers.stepTempsC.append(temperature);
    }
    addTemperatureFallback(numbers);
    return numbers;
}

inline QString temperatureText(const RecipeProfileDisplayNumbers& numbers,
                               double recipeOffsetC, bool fahrenheit)
{
    if (!numbers.valid || !std::isfinite(recipeOffsetC) || numbers.baseTempC <= 0.0)
        return {};
    const QString text = TemperatureDisplay::format(numbers.stepTempsC, numbers.baseTempC,
                                                     false, 0.0, fahrenheit, recipeOffsetC);
    return text.size() <= 256 ? text : QString();
}

} // namespace RecipeProfileDisplay
