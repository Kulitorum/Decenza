#pragma once

#include <QMetaType>
#include <QVariant>

#include <cmath>

// Strict numeric conversion for values crossing the QtRO/QML boundary. In
// particular, QVariant's permissive string conversions must not turn a wire
// string such as "12" into a valid numeric preference.
namespace ServiceValue {

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

inline bool boundedInteger(const QVariant& value, int minimum, int maximum, int* result)
{
    double number = 0.0;
    if (!finiteNumber(value, &number) || std::floor(number) != number
        || number < static_cast<double>(minimum)
        || number > static_cast<double>(maximum))
        return false;
    if (result)
        *result = static_cast<int>(number);
    return true;
}

} // namespace ServiceValue
