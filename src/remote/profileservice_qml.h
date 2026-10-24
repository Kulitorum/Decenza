#pragma once

#include "profileservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct ProfileServiceForeign
{
    Q_GADGET
    QML_FOREIGN(ProfileService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ProfileService)
public:
    static ProfileService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<ProfileService>);
        return new ProfileService(ProfileService::publishedNode(), engine);
    }
};
