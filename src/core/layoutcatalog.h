#pragma once

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Pure widget/action catalogs and option schemas shared by native and web settings.
class LayoutCatalog final
{
public:
    static bool typeHasOptions(const QString& type);
    static QStringList optionKeysForType(const QString& type);
    static QString defaultDisplayModeForType(const QString& type);
    static QStringList zoneOptionValues(const QString& key);
    static QVariantMap sleepOptionDefaults();
    static QVariantList widgetCatalog();
    static QVariantMap widgetChipNames();
    static QVariantList widgetCategoryNames();
    static QVariantList layoutActionCatalog();
    static QVariantMap layoutActionLabels();
    static QString gestureReservedActionForType(const QString& type);
    static bool typeSupportsGestureOverrides(const QString& type);
    static QVector<QPair<QString, QString>> translationStrings();
};
