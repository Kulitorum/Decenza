#include "settingssearch.h"

SettingsSearch::SettingsSearch(QObject* parent)
    : QObject(parent)
{
}

SettingsSearch* SettingsSearch::qmlAttachedProperties(QObject* object)
{
    return new SettingsSearch(object);
}
