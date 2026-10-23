#pragma once

#include "profileimportservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

struct ProfileImportServiceForeign
{
    Q_GADGET
    QML_FOREIGN(ProfileImportService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ProfileImportService)
public:
    static ProfileImportService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<ProfileImportService>,
                      "The QML factory must create the engine-owned service instance.");
        return new ProfileImportService(ProfileImportService::publishedNode(), engine);
    }
};
