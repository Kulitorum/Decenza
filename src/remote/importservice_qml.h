#pragma once

#include "importservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

struct ImportServiceForeign
{
    Q_GADGET
    QML_FOREIGN(ImportService)
    QML_NAMED_ELEMENT(ImportService)
    QML_SINGLETON
public:
    static ImportService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<ImportService>);
        return ImportService::create(engine);
    }
};
