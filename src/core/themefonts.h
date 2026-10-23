#pragma once

#include <QMap>
#include <QVariantMap>

// Canonical font-size roles shared by native SettingsTheme and the packaged
// remote Settings facade. Keeping defaults and editable bounds in one table
// prevents Theme.qml from seeing zero-sized fonts before host data arrives.
namespace ThemeFonts {

struct FontRole {
    int def;
    int min;
    int max;
};

inline const QMap<QString, FontRole>& roles()
{
    //                          default  min  max
    static const QMap<QString, FontRole> value = {
        {QStringLiteral("headingSize"),  {32, 16,  64}},
        {QStringLiteral("titleSize"),    {24, 12,  48}},
        {QStringLiteral("subtitleSize"), {18, 10,  36}},
        {QStringLiteral("bodySize"),     {18, 10,  36}},
        {QStringLiteral("labelSize"),    {14,  8,  28}},
        {QStringLiteral("captionSize"),  {12,  8,  24}},
        {QStringLiteral("valueSize"),    {48, 24,  96}},
        {QStringLiteral("timerSize"),    {72, 36, 120}}
    };
    return value;
}

inline const QMap<QString, int>& sizeDefaults()
{
    static const QMap<QString, int> value = [] {
        QMap<QString, int> result;
        for (auto it = roles().constBegin(); it != roles().constEnd(); ++it)
            result.insert(it.key(), it.value().def);
        return result;
    }();
    return value;
}

inline QVariantMap sizeDefaultsAsVariantMap()
{
    QVariantMap result;
    for (auto it = sizeDefaults().constBegin(); it != sizeDefaults().constEnd(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

} // namespace ThemeFonts
