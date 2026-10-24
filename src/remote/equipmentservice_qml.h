#pragma once
#include "equipmentservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct EquipmentServiceForeign
{
    Q_GADGET
    QML_FOREIGN(EquipmentService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(EquipmentService)
public:
    static EquipmentService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<EquipmentService>);
        return new EquipmentService(EquipmentService::publishedNode(), engine);
    }
};
