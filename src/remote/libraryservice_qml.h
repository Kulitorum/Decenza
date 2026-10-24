#pragma once

#include "libraryservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct LibraryServiceForeign
{
    Q_GADGET
    QML_FOREIGN(LibraryService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(LibraryService)
public:
    static LibraryService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<LibraryService>);
        return LibraryService::create(engine);
    }
};
