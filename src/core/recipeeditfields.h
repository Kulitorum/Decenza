#pragma once

#include <QMetaType>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <cmath>
#include <limits>
#include <utility>

// Portable recipe-editor field schema shared by native storage and the QtRO
// facade. Keep presentation-only and inventory aggregates out of this list.
namespace RecipeEditFields {

inline bool hotWaterActive(const QString& hotWaterJson)
{
    if (hotWaterJson.isEmpty())
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(hotWaterJson.toUtf8());
    return doc.isObject() && doc.object().value(QStringLiteral("hasWater")).toBool();
}

inline bool milkActive(const QString& steamJson)
{
    if (steamJson.isEmpty())
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(steamJson.toUtf8());
    return doc.isObject() && doc.object().value(QStringLiteral("hasMilk")).toBool();
}

// Whether a recipe brews as one two-step drink, and which step leads
// (recipe-drink-sequence): a profile, active hot water and no milk; the block
// order picks the steps. Read from the blocks, never drinkType, so the label
// (Recipe::deriveDrinkType) and the machine cannot disagree.
enum class DrinkSequence { None, WaterAfter, WaterBefore };

inline DrinkSequence drinkSequence(const QString& profileTitle, const QString& steamJson,
                                   const QString& hotWaterJson)
{
    if (profileTitle.trimmed().isEmpty() || !hotWaterActive(hotWaterJson) || milkActive(steamJson))
        return DrinkSequence::None;
    return QJsonDocument::fromJson(hotWaterJson.toUtf8()).object()
                   .value(QStringLiteral("order")).toString() == QLatin1String("before")
        ? DrinkSequence::WaterBefore
        : DrinkSequence::WaterAfter;
}

// The same, from a recipe's raw fields (Recipe::toVariantMap, the active-recipe cache).
inline DrinkSequence drinkSequence(const QVariantMap& recipe)
{
    return drinkSequence(recipe.value(QStringLiteral("profileTitle")).toString(),
                         recipe.value(QStringLiteral("steamJson")).toString(),
                         recipe.value(QStringLiteral("hotWaterJson")).toString());
}

inline bool saveValidationPasses(const QString& name, const QString& profileTitle,
                                 const QString& hotWaterJson)
{
    return !name.trimmed().isEmpty()
        && (!profileTitle.trimmed().isEmpty() || hotWaterActive(hotWaterJson));
}

inline const QStringList& keys()
{
    static const QStringList value{
        QStringLiteral("name"), QStringLiteral("profileTitle"),
        QStringLiteral("profileJson"), QStringLiteral("drinkType"),
        QStringLiteral("bagId"), QStringLiteral("beanBaseId"),
        QStringLiteral("roasterName"), QStringLiteral("coffeeName"),
        QStringLiteral("equipmentId"), QStringLiteral("doseG"),
        QStringLiteral("yieldValue"), QStringLiteral("yieldMode"),
        QStringLiteral("tempOffsetC"), QStringLiteral("grindPinned"),
        QStringLiteral("rpmPinned"), QStringLiteral("steamJson"),
        QStringLiteral("hotWaterJson"), QStringLiteral("archived")
    };
    return value;
}

inline bool integerValue(const QVariant& value, qint64* out)
{
    const int type = value.metaType().id();
    if (type == QMetaType::LongLong || type == QMetaType::Int
        || type == QMetaType::Short || type == QMetaType::Char) {
        *out = value.toLongLong();
        return true;
    }
    if (type == QMetaType::ULongLong || type == QMetaType::UInt
        || type == QMetaType::UShort || type == QMetaType::UChar) {
        const qulonglong number = value.toULongLong();
        if (number > static_cast<qulonglong>(std::numeric_limits<qint64>::max()))
            return false;
        *out = static_cast<qint64>(number);
        return true;
    }
    // JSON and QML JavaScript numbers are doubles. Accept only exact safe
    // integers; silently rounding an id would guard a different row.
    if (type == QMetaType::Double || type == QMetaType::Float) {
        const double number = value.toDouble();
        constexpr double safeInteger = 9007199254740991.0;
        if (!std::isfinite(number) || std::trunc(number) != number
            || std::abs(number) > safeInteger)
            return false;
        *out = static_cast<qint64>(number);
        return true;
    }
    return false;
}

inline bool doubleValue(const QVariant& value, double* out)
{
    const int type = value.metaType().id();
    if (type != QMetaType::Double && type != QMetaType::Float
        && type != QMetaType::LongLong && type != QMetaType::Int
        && type != QMetaType::Short && type != QMetaType::Char
        && type != QMetaType::ULongLong && type != QMetaType::UInt
        && type != QMetaType::UShort && type != QMetaType::UChar)
        return false;
    *out = value.toDouble();
    return std::isfinite(*out);
}

inline bool projectSnapshot(const QVariantMap& raw, QVariantMap* snapshot)
{
    if (!snapshot)
        return false;
    QVariantMap normalized;
    for (const QString& key : keys()) {
        const auto it = raw.constFind(key);
        if (it == raw.cend())
            return false;
        if (key == QLatin1String("bagId") || key == QLatin1String("equipmentId")
            || key == QLatin1String("rpmPinned")) {
            qint64 value = 0;
            if (!integerValue(it.value(), &value))
                return false;
            normalized.insert(key, QVariant::fromValue(value));
        } else if (key == QLatin1String("doseG") || key == QLatin1String("yieldValue")
                   || key == QLatin1String("tempOffsetC")) {
            double value = 0.0;
            if (!doubleValue(it.value(), &value))
                return false;
            normalized.insert(key, value);
        } else if (key == QLatin1String("archived")) {
            if (it->metaType().id() != QMetaType::Bool)
                return false;
            normalized.insert(key, it.value());
        } else {
            if (it->metaType().id() != QMetaType::QString)
                return false;
            normalized.insert(key, it.value());
        }
    }
    *snapshot = std::move(normalized);
    return true;
}

// Storage writes require the complete editable-row snapshot. The projection
// helper also accepts inventory maps with extra read-only fields for clients.
inline bool hasExactSnapshot(const QVariantMap& raw, QVariantMap* snapshot)
{
    return raw.size() == keys().size() && projectSnapshot(raw, snapshot);
}

} // namespace RecipeEditFields
