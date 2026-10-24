#pragma once

#include "languageservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct LanguageServiceForeign
{
    Q_GADGET
    QML_FOREIGN(LanguageService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(LanguageService)
public:
    static LanguageService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<LanguageService>);
        return new LanguageService(LanguageService::publishedNode(), engine);
    }
};

struct LanguageStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(LanguageStateReplica)
    QML_NAMED_ELEMENT(LanguageStateReplica)
    QML_UNCREATABLE("LanguageStateReplica is owned by LanguageService")
};
