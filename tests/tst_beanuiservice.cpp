#include <QtTest>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QSignalSpy>
#include <QQmlEngine>
#include <QUrlQuery>

#include "core/beanimagekey.h"
#include "remote/beanuiservice_qml.h"
#include "remote/beanuiservicesource.h"

class DelayedBeanUiSource final : public BeanUiStateSimpleSource
{
public:
    using BeanUiStateSimpleSource::setCapabilities;
    struct Call { QString id; QString operation; QVariantMap arguments; };
    QList<Call> calls;

    void invoke(QString requestId, QString operation, QVariantMap arguments) override
    {
        calls.append({std::move(requestId), std::move(operation), std::move(arguments)});
    }
    void cancel(QString) override {}
    void replyValues(const Call& call, const QVariantMap& values)
    {
        emit result(call.id, call.operation, values, {});
    }
    void reply(const QString& id, const QString& operation, const QString& reason = {})
    {
        emit result(id, operation, {}, reason);
    }
};

class tst_BeanUiService final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void cleanup() { BeanUiService::setPublishedNode(nullptr); }

    void beanImageRouteKeyRoundTripsEncodedSafeNames()
    {
        const QStringList keys{
            QStringLiteral("bag-148"),
            QStringLiteral("roaster.name"),
            QStringLiteral("name with spaces"),
            QStringLiteral("café"),
        };
        for (const QString& expected : keys) {
            QString decoded;
            QVERIFY(BeanImageKey::decodePathSegment(
                BeanImageKey::encodePathSegment(expected), &decoded));
            QCOMPARE(decoded, expected);

            const QString route = QStringLiteral("/api/session/bean-image/")
                + QString::fromLatin1(BeanImageKey::encodePathSegment(expected));
            QVERIFY(BeanImageKey::decodeSessionImagePath(route, &decoded));
            QCOMPARE(decoded, expected);
            QVERIFY(BeanImageKey::decodeSessionImagePath(route + QStringLiteral("?v=17"), &decoded));
            QCOMPARE(decoded, expected);
        }

        QString rejected;
        QVERIFY(!BeanImageKey::decodePathSegment(QByteArrayLiteral("a%2Fb"), &rejected));
        QVERIFY(!BeanImageKey::decodePathSegment(QByteArrayLiteral("a%5Cb"), &rejected));
        QVERIFY(!BeanImageKey::decodePathSegment(QByteArrayLiteral("a..b"), &rejected));
        QVERIFY(!BeanImageKey::decodePathSegment(QByteArrayLiteral("bad%2"), &rejected));
        QVERIFY(!BeanImageKey::decodePathSegment(QByteArrayLiteral("%00"), &rejected));
        QVERIFY(!BeanImageKey::decodeSessionImagePath(
            QStringLiteral("/api/session/bean-image/bag-148?other=1"), &rejected));
        QVERIFY(!BeanImageKey::decodeSessionImagePath(
            QStringLiteral("/api/session/bean-image/bag-148?v=1&v=2"), &rejected));
        QVERIFY(!BeanImageKey::decodeSessionImagePath(
            QStringLiteral("/api/session/bean-image/bag-148?v=x"), &rejected));
        QVERIFY(!BeanImageKey::decodeSessionImagePath(
            QStringLiteral("/api/session/bean-image/bag-148?"), &rejected));
    }

    void disconnectDetachesPendingIdBeforeQueuedFailure()
    {
        DelayedBeanUiSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BeanUiStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        auto* service = BeanUiServiceForeign::create(&engine, &engine);
        service->setNode(&node);
        auto* replica = node.acquire<BeanUiStateReplica>();
        QTRY_VERIFY_WITH_TIMEOUT(replica->isReplicaValid(), 5000);
        QSignalSpy failed(service, &BeanUiService::productPageSearchFailed);

        const QString requestId = service->findProductPage(QStringLiteral("ui-token"),
            QStringLiteral("Roaster"), QStringLiteral("Coffee"), QStringLiteral("coffee"));
        QVERIFY(!requestId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 1, 5000);
        QCOMPARE(source.calls.first().operation, QStringLiteral("findPage"));

        service->setNode(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("ui-token"));
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("Connection lost")));

        // A late reply from the detached replica must not complete the request twice.
        source.reply(requestId, QStringLiteral("findPage"));
        QCoreApplication::processEvents();
        QCOMPARE(failed.size(), 1);
    }

    void communityOperationsAreScopedAndNeverReplayOnReconnect()
    {
        DelayedBeanUiSource source;
        source.setCapabilities({{"configured", true}, {"webSearch", true},
            {"communityApiAvailable", true}, {"communityBaseUrl", QStringLiteral("https://visualizer.example")},
            {"communityMinEnjoyment", 70}});
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BeanUiStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine firstEngine;
        QQmlEngine secondEngine;
        auto* first = BeanUiServiceForeign::create(&firstEngine, &firstEngine);
        auto* second = BeanUiServiceForeign::create(&secondEngine, &secondEngine);
        first->setNode(&node);
        second->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(first->ready() && second->ready(), 5000);
        QVERIFY(first->communityApiAvailable());
        QVERIFY(first->currentProviderSupportsWebSearch());
        QVERIFY(source.calls.isEmpty()); // Availability never starts a provider request.
        QCOMPARE(first->communitySearchKey(" Roaster ", " Coffee ", " ID "),
                 QStringLiteral("roaster\ncoffee\nid"));
        QCOMPARE(first->communityRecipeName(" Coffee ", " Profile "), QStringLiteral("Coffee · Profile"));
        const QUrl url = first->communityPageUrl(QStringLiteral("Roaster"), QStringLiteral("Coffee"));
        QCOMPARE(url.host(), QStringLiteral("visualizer.example"));
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("min_enjoyment")), QStringLiteral("70"));
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("bean_type")), QStringLiteral("Coffee"));

        QSignalSpy firstResults(first, &BeanUiService::communityResultsReady);
        QSignalSpy secondResults(second, &BeanUiService::communityResultsReady);
        first->communitySearch("Roaster", "Coffee", "ID");
        second->communitySearch("Roaster", "Coffee", "ID");
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 2, 5000);
        QVERIFY(source.calls.at(0).id != source.calls.at(1).id);
        source.replyValues(source.calls.at(0), {{"profiles", QVariantList{}}});
        QTRY_COMPARE_WITH_TIMEOUT(firstResults.size(), 1, 5000);
        QCOMPARE(firstResults.first().at(0).toString(), QStringLiteral("roaster\ncoffee\nid"));
        QVERIFY(secondResults.isEmpty());
        source.replyValues(source.calls.at(1), {{"profiles", QVariantList{}}});
        QTRY_COMPARE_WITH_TIMEOUT(secondResults.size(), 1, 5000);

        QSignalSpy research(first, &BeanUiService::beanResearchReady);
        QSignalSpy otherResearch(second, &BeanUiService::beanResearchReady);
        const QString researchId = first->researchBean("ui-token", "Roaster", "Coffee", "");
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 3, 5000);
        QCOMPARE(source.calls.last().id, researchId);
        QCOMPARE(source.calls.last().operation, QStringLiteral("researchBean"));
        source.replyValues(source.calls.last(), {{"advice", QStringLiteral("Cited advice")}});
        QTRY_COMPARE_WITH_TIMEOUT(research.size(), 1, 5000);
        QCOMPARE(research.first().at(0).toString(), QStringLiteral("ui-token"));
        QCOMPARE(research.first().at(1).toString(), QStringLiteral("Cited advice"));
        QVERIFY(otherResearch.isEmpty());

        QSignalSpy applied(first, &BeanUiService::communitySuggestionApplied);
        QSignalSpy otherApplied(second, &BeanUiService::communitySuggestionApplied);
        first->applyCommunitySuggestion({{"key", QStringLiteral("same-ui-key")}});
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 4, 5000);
        source.replyValues(source.calls.last(), {{"ok", false}, {"recipeName", QStringLiteral("Saved recipe")},
                                               {"selected", false}});
        QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 1, 5000);
        QCOMPARE(applied.first().at(0).toString(), QStringLiteral("same-ui-key"));
        QVERIFY(!applied.first().at(1).toBool());
        QCOMPARE(applied.first().at(2).toString(), QStringLiteral("Saved recipe"));
        QVERIFY(otherApplied.isEmpty());

        QSignalSpy failedSearch(first, &BeanUiService::communitySearchFailed);
        QSignalSpy failedProfile(first, &BeanUiService::communityProfileFailed);
        QSignalSpy failedResearch(first, &BeanUiService::beanResearchFailed);
        first->communitySearch("Pending", "Coffee", "");
        first->fetchCommunityProfile(QStringLiteral("shot-uuid"));
        first->researchBean("pending-research", "Roaster", "Coffee", "");
        first->applyCommunitySuggestion({{"key", QStringLiteral("pending-apply")}});
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 8, 5000);
        first->setNode(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(failedSearch.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(failedProfile.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(failedResearch.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 2, 5000);
        QCOMPARE(failedProfile.first().at(0).toString(), QStringLiteral("shot-uuid"));
        QCOMPARE(failedResearch.first().at(0).toString(), QStringLiteral("pending-research"));
        QCOMPARE(applied.last().at(0).toString(), QStringLiteral("pending-apply"));
        QVERIFY(!applied.last().at(1).toBool());
        first->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(first->ready(), 5000);
        for (qsizetype i = 4; i < source.calls.size(); ++i)
            source.replyValues(source.calls.at(i), {});
        QCoreApplication::processEvents();
        QCOMPARE(source.calls.size(), 8);
        QCOMPARE(firstResults.size(), 1);
        QCOMPARE(research.size(), 1);
        QCOMPARE(applied.size(), 2);
    }

    void requestLimitEmitsOperationSpecificFailure()
    {
        DelayedBeanUiSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BeanUiStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        auto* service = BeanUiServiceForeign::create(&engine, &engine);
        service->setNode(&node);
        auto* replica = node.acquire<BeanUiStateReplica>();
        QTRY_VERIFY_WITH_TIMEOUT(replica->isReplicaValid(), 5000);

        for (int i = 0; i < 128; ++i)
            service->fetchCanonicalDetails({{QStringLiteral("id"), QString::number(i)}});
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 128, 10000);

        QSignalSpy failed(service, &BeanUiService::productPageSearchFailed);
        const QString requestId = service->findProductPage(QStringLiteral("limit-token"),
            QStringLiteral("Roaster"), QStringLiteral("Coffee"), QStringLiteral("coffee"));
        QVERIFY(!requestId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("limit-token"));
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("Too many bean requests")));
        QCOMPARE(source.calls.size(), 128);
    }
};

QTEST_MAIN(tst_BeanUiService)
#include "tst_beanuiservice.moc"
