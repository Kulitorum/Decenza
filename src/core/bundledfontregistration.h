#pragma once

#include <QFont>
#include <QFontDatabase>
#include <QStringList>
#include <QVector>

namespace BundledFontRegistration {

struct Font {
    QString resource;
    int id = -1;
    QStringList families;

    bool usable() const { return id >= 0 && !families.isEmpty(); }
};

inline const QStringList& uiFontResources()
{
    static const QStringList resources = {
        QStringLiteral(":/fonts/DecenzaSans-Regular.ttf"),
        QStringLiteral(":/fonts/DecenzaSans-Medium.ttf"),
        QStringLiteral(":/fonts/DecenzaSans-Bold.ttf"),
        QStringLiteral(":/fonts/DecenzaSans-Light.ttf"),
    };
    return resources;
}

inline QString symbolFontResource()
{
    return QStringLiteral(":/fonts/NotoSansMath-Regular.ttf");
}

inline Font registerFont(const QString& resource)
{
    Font result;
    result.resource = resource;
    result.id = QFontDatabase::addApplicationFont(resource);
    if (result.id >= 0)
        result.families = QFontDatabase::applicationFontFamilies(result.id);
    return result;
}

inline QVector<Font> registerUiFonts()
{
    QVector<Font> result;
    result.reserve(uiFontResources().size());
    for (const QString& resource : uiFontResources())
        result.append(registerFont(resource));
    return result;
}

inline Font registerSymbolFont()
{
    return registerFont(symbolFontResource());
}

inline QString primaryFamily(const QVector<Font>& registrations)
{
    for (const Font& registration : registrations) {
        if (registration.usable())
            return registration.families.first();
    }
    return {};
}

inline QString symbolFamily(const Font& registration)
{
    return registration.usable() ? registration.families.first() : QString();
}

inline QStringList applicationFontFamilies(const QString& primary, const QString& symbol)
{
    QStringList families;
    if (!primary.isEmpty())
        families.append(primary);
    if (!symbol.isEmpty())
        families.append(symbol);
    return families;
}

inline QFont applicationFont(const QString& primary, const QString& symbol = {})
{
    QFont font;
    font.setFamilies(applicationFontFamilies(primary, symbol));
    return font;
}

} // namespace BundledFontRegistration
