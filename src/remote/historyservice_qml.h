#pragma once

#include "historyservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct HistoryServiceForeign
{
    Q_GADGET
    QML_FOREIGN(HistoryService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(HistoryService)
public:
    static HistoryService* create(QQmlEngine* engine, QJSEngine* jsEngine)
    {
        static_assert(!std::is_default_constructible_v<HistoryService>);
        return HistoryService::create(engine, jsEngine);
    }
};
