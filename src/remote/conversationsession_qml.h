#pragma once

#include "conversationsession.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct ConversationSessionForeign
{
    Q_GADGET
    QML_FOREIGN(ConversationSession)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ConversationSession)
public:
    static ConversationSession* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<ConversationSession>);
        return new ConversationSession(ConversationSession::publishedNode(), engine);
    }
};

struct ConversationSessionStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(ConversationSessionStateReplica)
    QML_NAMED_ELEMENT(ConversationSessionStateReplica)
    QML_UNCREATABLE("ConversationSessionStateReplica is owned by ConversationSession")
};
