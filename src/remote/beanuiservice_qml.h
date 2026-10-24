#pragma once
#include "beanuiservice.h"
#include <QtQml/QQmlEngine>
#include <type_traits>
struct BeanUiServiceForeign {
    Q_GADGET
    QML_FOREIGN(BeanUiService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(BeanUiService)
public:
    static BeanUiService* create(QQmlEngine* engine, QJSEngine*) {
        static_assert(!std::is_default_constructible_v<BeanUiService>);
        return new BeanUiService(BeanUiService::publishedNode(), engine);
    }
};
