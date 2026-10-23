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
    static QJsonObject readoutCapabilitiesJson();
    static QString defaultDisplayModeForType(const QString& type);
    static QStringList zoneOptionValues(const QString& key);
    static QJsonObject displayModeDefaultsJson();
    static QVariantMap sleepOptionDefaults();
    static QJsonObject sleepOptionDefaultsJson();
    static QVariantList widgetCatalog();
    static QVariantMap widgetChipNames();
    static QVariantList widgetCategoryNames();
    static QJsonObject widgetCatalogJson();
    static QVariantList layoutActionCatalog();
    static QVariantMap layoutActionLabels();
    static QString gestureReservedActionForType(const QString& type);
    static bool typeSupportsGestureOverrides(const QString& type);
    static QJsonObject layoutActionCatalogJson();
    static QVector<QPair<QString, QString>> translationStrings();
};
