#pragma once

#include <QMetaType>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <cmath>

// Strict numeric conversion for values crossing the QtRO/QML boundary. In
// particular, QVariant's permissive string conversions must not turn a wire
// string such as "12" into a valid numeric preference.
namespace ServiceValue {

inline bool hasExactKeys(const QVariantMap& values, const QStringList& keys)
{
    if (values.size() != keys.size())
        return false;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!keys.contains(it.key()))
            return false;
    }
    return true;
}

inline bool isNumeric(const QVariant& value)
{
    switch (value.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Float:
    case QMetaType::Double:
        return true;
    default:
        return false;
    }
}

inline bool finiteNumber(const QVariant& value, double* result)
{
    if (!isNumeric(value))
        return false;
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return false;
    if (result)
        *result = number;
    return true;
}

inline constexpr qint64 MaximumExactInteger = 9007199254740991LL;

inline bool boundedInteger64(const QVariant& value, qint64 minimum, qint64 maximum, qint64* result)
{
    double number = 0.0;
    if (!finiteNumber(value, &number) || std::floor(number) != number
        || number < static_cast<double>(minimum)
        || number > static_cast<double>(maximum)
        || number < -static_cast<double>(MaximumExactInteger)
        || number > static_cast<double>(MaximumExactInteger))
        return false;
    if (result)
        *result = static_cast<qint64>(number);
    return true;
}

inline bool boundedInteger(const QVariant& value, int minimum, int maximum, int* result)
{
    qint64 number = 0;
    if (!boundedInteger64(value, minimum, maximum, &number)) return false;
    if (result) *result = static_cast<int>(number);
    return true;
}

} // namespace ServiceValue
