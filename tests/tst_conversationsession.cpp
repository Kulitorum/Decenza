#include <QtTest>

#include <QJSEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QSignalSpy>
#include <QPointer>
#include <QUuid>
#include <QQmlEngine>
#include <limits>
#include <memory>

#include "remote/conversationsession_qml.h"
#include "remote/conversationsessionrequest.h"
#include "remote/servicepayload.h"
#include "rep_conversationsession_source.h"

class FakeConversationSource final : public ConversationSessionStateSimpleSource
{
    Q_OBJECT
public:
    using ConversationSessionStateSimpleSource::ConversationSessionStateSimpleSource;

    struct Pending {
        QString clientId;
        QString requestId;
        QString operation;
    };
    QList<Pending> pending;

public slots:
    void requestState(QString clientId, QString requestId) override
    {
        emit stateReady(clientId, requestId, {{QStringLiteral("marker"), QStringLiteral("ready")}});
    }
    void loadMostRecent(QString clientId, QString requestId) override
    {
        pending.append({std::move(clientId), std::move(requestId), QStringLiteral("loadMostRecent")});
    }
    void openShot(QString clientId, QString requestId, QVariantMap, qint64,
                  QString) override
    {
        pending.append({std::move(clientId), std::move(requestId), QStringLiteral("openShot")});
    }
    void sendMessage(QString clientId, QString requestId, QString) override
    {
        pending.append({std::move(clientId), std::move(requestId), QStringLiteral("sendMessage")});
    }
    void retry(QString clientId, QString requestId) override
    {
        pending.append({std::move(clientId), std::move(requestId), QStringLiteral("retry")});
    }
    void clear(QString clientId, QString requestId) override
    {
        pending.append({std::move(clientId), std::move(requestId), QStringLiteral("clear")});
    }
    void dismissShot(QString clientId, QString requestId) override
    {
        pending.append({std::move(clientId), std::move(requestId), QStringLiteral("dismissShot")});
    }
    void save(QString clientId, QString requestId) override
    {
        pending.append({std::move(clientId), std::move(requestId), QStringLiteral("save")});
    }
    void closeClient(QString) override {}

    void complete(const Pending& request, bool success, const QString& reason = {})
    {
        emit operationCompleted(request.clientId, request.requestId, request.operation,
                                success, {}, reason);
    }
};

class tst_ConversationSession final : public QObject
{
    Q_OBJECT

private:
    static QString uniqueLocalUrl()
    {
        return QStringLiteral("local:conversation-test-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void loadMostRecentUsesCorrelatedSessionOperation()
    {
        FakeConversationSource source;
        QRemoteObjectHost host{QUrl(uniqueLocalUrl())};
        QVERIFY(host.enableRemoting<ConversationSessionStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        ConversationSession::setPublishedNode(&node);
        QQmlEngine engine;
        auto* session = ConversationSessionForeign::create(&engine, &engine);
        QSignalSpy completed(session, &ConversationSession::completed);
        QVERIFY(completed.isValid());
        QTRY_VERIFY(session->ready());

        const QString requestId = session->request(QStringLiteral("loadMostRecent"));
        QTRY_COMPARE(source.pending.size(), 1);
        const auto operation = source.pending.first();
        QCOMPARE(operation.requestId, requestId);
        QCOMPARE(operation.operation, QStringLiteral("loadMostRecent"));
        source.complete(operation, true);
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), requestId);
        QCOMPARE(completed.first().at(1).toString(), QStringLiteral("loadMostRecent"));
        QVERIFY(completed.first().at(2).toBool());
    }

    void facadeUsesConnectionOwnedClientIdentity()
    {
        FakeConversationSource source;
        QRemoteObjectHost host{QUrl(uniqueLocalUrl())};
        QVERIFY(host.enableRemoting<ConversationSessionStateSourceAPI>(&source));
        QRemoteObjectNode node;
        const QString clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        node.setProperty("decenzaClientId", clientId);
        QVERIFY(node.connectToNode(host.hostUrl()));
        ConversationSession::setPublishedNode(&node);
        QQmlEngine engine;
        auto* session = ConversationSessionForeign::create(&engine, &engine);
        QTRY_VERIFY(session->ready());

        session->request(QStringLiteral("loadMostRecent"));
        QTRY_COMPARE(source.pending.size(), 1);
        QCOMPARE(source.pending.first().clientId, clientId);
    }

    void storedShotRequestIdentityAndOverridesAreStrict()
    {
        const QString canonical = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(ConversationSessionRequest::isCanonicalUuid(canonical));
        QVERIFY(!ConversationSessionRequest::isCanonicalUuid(canonical.toUpper()));
        QVERIFY(!ConversationSessionRequest::isCanonicalUuid(QStringLiteral("{%1}").arg(canonical)));

        const QVariantMap overrides{
            {QStringLiteral("tasteBalance"), QStringLiteral("bright")},
            {QStringLiteral("tasteBody"), QStringLiteral("silky")},
            {QStringLiteral("enjoyment0to100"), 82}
        };
        QVERIFY(ConversationSessionRequest::validStoredShotOverrides(overrides));
        QVERIFY(ConversationSessionRequest::validStoredShotOverrides({}));

        QVariantMap arbitraryShot = overrides;
        arbitraryShot.insert(QStringLiteral("profileName"), QStringLiteral("client supplied"));
        QVERIFY(!ConversationSessionRequest::validStoredShotOverrides(arbitraryShot));
        QVariantMap fractionalScore = overrides;
        fractionalScore.insert(QStringLiteral("enjoyment0to100"), 82.5);
        QVERIFY(!ConversationSessionRequest::validStoredShotOverrides(fractionalScore));
        QVariantMap outOfRange = overrides;
        outOfRange.insert(QStringLiteral("enjoyment0to100"), 101);
        QVERIFY(!ConversationSessionRequest::validStoredShotOverrides(outOfRange));
        QVariantMap nonFinite = overrides;
        nonFinite.insert(QStringLiteral("enjoyment0to100"), std::numeric_limits<double>::infinity());
        QVERIFY(!ConversationSessionRequest::validStoredShotOverrides(nonFinite));
        QVariantMap longTaste = overrides;
        longTaste.insert(QStringLiteral("tasteBody"), QString(257, QLatin1Char('x')));
        QVERIFY(!ConversationSessionRequest::validStoredShotOverrides(longTaste));
    }

    void shotPayloadBoundsUseQtRemoteObjectEncoding()
    {
        QVariantMap small{{QStringLiteral("tasteBalance"), QStringLiteral("sweet")}};
        const qint64 smallSize = ServicePayload::serializedSize(small);
        QVERIFY(smallSize > 0);
        QVERIFY(ServicePayload::fits(small, smallSize));
        QVERIFY(!ServicePayload::fits(small, smallSize - 1));

        QVariantMap tooLarge{{QStringLiteral("payload"), QString(70 * 1024, QLatin1Char('x'))}};
        QVERIFY(!ServicePayload::fits(tooLarge, ConversationSessionRequest::MaxShotPayloadBytes));
    }

    void facadeCorrelatesOutOfOrderCompletionsAndSameNodeIsIdempotent()
    {
        FakeConversationSource source;
        QRemoteObjectHost host{QUrl(uniqueLocalUrl())};
        QVERIFY(host.enableRemoting<ConversationSessionStateSourceAPI>(&source));

        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        ConversationSession::setPublishedNode(&node);
        QQmlEngine engine;
        auto* session = ConversationSessionForeign::create(&engine, &engine);
        QSignalSpy completed(session, &ConversationSession::completed);
        QVERIFY(completed.isValid());
        QTRY_VERIFY(session->ready());
        QCOMPARE(session->state().value(QStringLiteral("marker")).toString(),
                 QStringLiteral("ready"));

        session->setNode(&node);
        QVERIFY(session->ready());
        QCOMPARE(session->state().value(QStringLiteral("marker")).toString(),
                 QStringLiteral("ready"));

        const QString first = session->request(QStringLiteral("sendMessage"),
            {{QStringLiteral("text"), QStringLiteral("first")}});
        const QString second = session->request(QStringLiteral("save"));
        QTRY_COMPARE(source.pending.size(), 2);
        const auto requests = source.pending;
        QCOMPARE(requests.at(0).requestId, first);
        QCOMPARE(requests.at(1).requestId, second);

        source.complete(requests.at(1), true);
        source.complete(requests.at(0), false, QStringLiteral("failed"));
        QTRY_COMPARE(completed.size(), 2);
        QCOMPARE(completed.at(0).at(0).toString(), second);
        QCOMPARE(completed.at(0).at(1).toString(), QStringLiteral("save"));
        QCOMPARE(completed.at(0).at(2).toBool(), true);
        QCOMPARE(completed.at(1).at(0).toString(), first);
        QCOMPARE(completed.at(1).at(1).toString(), QStringLiteral("sendMessage"));
        QCOMPARE(completed.at(1).at(2).toBool(), false);
        QCOMPARE(completed.at(1).at(4).toString(), QStringLiteral("failed"));
    }

    void queuedUnsupportedFailureIsCorrelated()
    {
        FakeConversationSource source;
        QRemoteObjectHost host{QUrl(uniqueLocalUrl())};
        QVERIFY(host.enableRemoting<ConversationSessionStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        ConversationSession::setPublishedNode(&node);
        QQmlEngine engine;
        auto* session = ConversationSessionForeign::create(&engine, &engine);
        QSignalSpy completed(session, &ConversationSession::completed);
        QVERIFY(completed.isValid());
        QTRY_VERIFY(session->ready());

        const QString requestId = session->request(QStringLiteral("notSupported"));
        QVERIFY(!requestId.isEmpty());
        QCOMPARE(completed.size(), 0); // QML can store the returned ID first.
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.constFirst().at(0).toString(), requestId);
        QCOMPARE(completed.constFirst().at(1).toString(), QStringLiteral("notSupported"));
        QCOMPARE(completed.constFirst().at(2).toBool(), false);
        QCOMPARE(completed.constFirst().at(4).toString(), QStringLiteral("unsupportedOperation"));
    }

    void disconnectTerminatesOutstandingRequest()
    {
        FakeConversationSource source;
        auto host = std::make_unique<QRemoteObjectHost>();
        QVERIFY(host->setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host->enableRemoting<ConversationSessionStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host->hostUrl()));
        ConversationSession::setPublishedNode(&node);
        QQmlEngine engine;
        auto* session = ConversationSessionForeign::create(&engine, &engine);
        QSignalSpy completed(session, &ConversationSession::completed);
        QVERIFY(completed.isValid());
        QTRY_VERIFY(session->ready());

        const QString requestId = session->request(QStringLiteral("sendMessage"),
            {{QStringLiteral("text"), QStringLiteral("pending")}});
        QTRY_COMPARE(source.pending.size(), 1);
        host.reset();
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.constFirst().at(0).toString(), requestId);
        QCOMPARE(completed.constFirst().at(1).toString(), QStringLiteral("sendMessage"));
        QCOMPARE(completed.constFirst().at(2).toBool(), false);
        QCOMPARE(completed.constFirst().at(4).toString(), QStringLiteral("disconnected"));
        QVERIFY(!session->ready());
    }

    void nodeReplacementPublishesDisconnectedStateBeforeRetiredCompletion()
    {
        FakeConversationSource source;
        QRemoteObjectHost host{QUrl(uniqueLocalUrl())};
        QVERIFY(host.enableRemoting<ConversationSessionStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        ConversationSession::setPublishedNode(&node);
        QQmlEngine engine;
        auto* session = ConversationSessionForeign::create(&engine, &engine);
        QSignalSpy completed(session, &ConversationSession::completed);
        QVERIFY(completed.isValid());
        QTRY_VERIFY(session->ready());

        QString followupId;
        connect(session, &ConversationSession::completed, session,
                [&followupId, session](const QString&, const QString& operation,
                                       bool, const QVariantMap&, const QString&) {
            if (operation == QStringLiteral("sendMessage"))
                followupId = session->request(QStringLiteral("save"));
        });
        const QString retiredId = session->request(QStringLiteral("sendMessage"),
            {{QStringLiteral("text"), QStringLiteral("retire")}});
        QTRY_COMPARE(source.pending.size(), 1);

        session->setNode(nullptr);
        QTRY_COMPARE(completed.size(), 2);
        QVERIFY(!followupId.isEmpty());
        QCOMPARE(completed.at(0).at(0).toString(), retiredId);
        QCOMPARE(completed.at(0).at(4).toString(), QStringLiteral("disconnected"));
        QCOMPARE(completed.at(1).at(0).toString(), followupId);
        QCOMPARE(completed.at(1).at(1).toString(), QStringLiteral("save"));
        QCOMPARE(completed.at(1).at(4).toString(), QStringLiteral("disconnected"));
        QVERIFY(!session->ready());
        QVERIFY(!session->errorMessage().isEmpty());
    }

    void queuedStateFromRetiredReplicaIsIgnored()
    {
        FakeConversationSource source;
        QRemoteObjectHost host{QUrl(uniqueLocalUrl())};
        QVERIFY(host.enableRemoting<ConversationSessionStateSourceAPI>(&source));
        QRemoteObjectNode node;
        const QString clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        node.setProperty("decenzaClientId", clientId);
        QVERIFY(node.connectToNode(host.hostUrl()));
        ConversationSession::setPublishedNode(&node);
        QQmlEngine engine;
        auto* session = ConversationSessionForeign::create(&engine, &engine);
        QTRY_VERIFY(session->ready());

        auto* oldReplica = session->findChild<ConversationSessionStateReplica*>();
        QVERIFY(oldReplica);
        QRemoteObjectNode replacement;
        replacement.setProperty("decenzaClientId", clientId);
        connect(oldReplica, &ConversationSessionStateReplica::stateChanged, session,
                [session, &replacement](const QString&, const QVariantMap&) {
            session->setNode(&replacement);
        }, Qt::DirectConnection);

        // The service's queued delivery is posted first. Rebind synchronously
        // from this direct observer before that old-replica callback runs.
        emit oldReplica->stateChanged(clientId,
            {{QStringLiteral("marker"), QStringLiteral("stale-old-source")} });
        QVERIFY(!session->ready());
        QCoreApplication::processEvents();
        QVERIFY(!session->ready());
        QVERIFY(session->state().isEmpty());
    }

    void deletingSessionFromSetNodeStateNotificationStopsRetirement()
    {
        FakeConversationSource source;
        QRemoteObjectHost host{QUrl(uniqueLocalUrl())};
        QVERIFY(host.enableRemoting<ConversationSessionStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        ConversationSession::setPublishedNode(&node);
        QQmlEngine engine;
        auto* session = ConversationSessionForeign::create(&engine, &engine);
        QTRY_VERIFY(session->ready());
        session->request(QStringLiteral("sendMessage"),
            {{QStringLiteral("text"), QStringLiteral("retire")} });
        QTRY_COMPARE(source.pending.size(), 1);

        QPointer<ConversationSession> guard(session);
        connect(session, &ConversationSession::stateChanged, session,
                [session] { delete session; }, Qt::DirectConnection);
        session->setNode(nullptr);
        QVERIFY(guard.isNull());
    }

    void deletingSessionFromDisconnectCompletionStopsContinuation()
    {
        FakeConversationSource source;
        auto host = std::make_unique<QRemoteObjectHost>();
        QVERIFY(host->setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host->enableRemoting<ConversationSessionStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host->hostUrl()));
        ConversationSession::setPublishedNode(&node);
        QQmlEngine engine;
        auto* session = ConversationSessionForeign::create(&engine, &engine);
        QTRY_VERIFY(session->ready());
        session->request(QStringLiteral("sendMessage"),
            {{QStringLiteral("text"), QStringLiteral("first")} });
        session->request(QStringLiteral("sendMessage"),
            {{QStringLiteral("text"), QStringLiteral("second")} });
        QTRY_COMPARE(source.pending.size(), 2);

        QPointer<ConversationSession> guard(session);
        connect(session, &ConversationSession::completed, session,
                [session](const QString&, const QString&, bool, const QVariantMap&,
                          const QString& reason) {
            if (reason == QStringLiteral("disconnected"))
                delete session;
        }, Qt::DirectConnection);
        host.reset();
        QTRY_VERIFY(guard.isNull());
    }

    void disconnectCompletesAllRetiredRequestsAfterReentrantRebind()
    {
        FakeConversationSource oldSource;
        FakeConversationSource newSource;
        auto oldHost = std::make_unique<QRemoteObjectHost>();
        auto newHost = std::make_unique<QRemoteObjectHost>();
        QVERIFY(oldHost->setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(newHost->setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(oldHost->enableRemoting<ConversationSessionStateSourceAPI>(&oldSource));
        QVERIFY(newHost->enableRemoting<ConversationSessionStateSourceAPI>(&newSource));
        QRemoteObjectNode oldNode;
        QRemoteObjectNode newNode;
        QVERIFY(oldNode.connectToNode(oldHost->hostUrl()));
        QVERIFY(newNode.connectToNode(newHost->hostUrl()));
        ConversationSession::setPublishedNode(&oldNode);
        QQmlEngine engine;
        auto* session = ConversationSessionForeign::create(&engine, &engine);
        QSignalSpy completed(session, &ConversationSession::completed);
        QVERIFY(completed.isValid());
        QTRY_VERIFY(session->ready());

        const QString firstId = session->request(QStringLiteral("sendMessage"),
            {{QStringLiteral("text"), QStringLiteral("first")} });
        const QString secondId = session->request(QStringLiteral("sendMessage"),
            {{QStringLiteral("text"), QStringLiteral("second")} });
        QTRY_COMPARE(oldSource.pending.size(), 2);

        bool rebound = false;
        bool staleDisconnectStatePublished = false;
        connect(session, &ConversationSession::stateChanged, session,
                [session, &rebound, &staleDisconnectStatePublished] {
            if (rebound && !session->errorMessage().isEmpty())
                staleDisconnectStatePublished = true;
        }, Qt::DirectConnection);
        connect(session, &ConversationSession::completed, session,
                [session, &newNode, &rebound](const QString&, const QString&, bool,
                                              const QVariantMap&, const QString& reason) {
            if (!rebound && reason == QStringLiteral("disconnected")) {
                rebound = true;
                session->setNode(&newNode);
            }
        }, Qt::DirectConnection);

        oldHost.reset();
        QTRY_COMPARE(completed.size(), 2);
        QTRY_VERIFY(session->ready());
        QVERIFY(rebound);
        QVERIFY(!staleDisconnectStatePublished);
        QCOMPARE(session->state().value(QStringLiteral("marker")).toString(),
                 QStringLiteral("ready"));
        QStringList failedIds;
        for (const auto& completion : completed) {
            failedIds.append(completion.at(0).toString());
            QCOMPARE(completion.at(2).toBool(), false);
            QCOMPARE(completion.at(4).toString(), QStringLiteral("disconnected"));
        }
        QVERIFY(failedIds.contains(firstId));
        QVERIFY(failedIds.contains(secondId));
    }
};

QTEST_MAIN(tst_ConversationSession)
#include "tst_conversationsession.moc"
