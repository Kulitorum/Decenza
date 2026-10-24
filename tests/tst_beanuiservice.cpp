#include <QtTest>
#include <QFile>
#include <QJSEngine>
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
    QStringList cancelled;

    void invoke(QString requestId, QString operation, QVariantMap arguments) override
    {
        calls.append({std::move(requestId), std::move(operation), std::move(arguments)});
    }
    void cancel(QString id) override { cancelled.append(id); }
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

    void abandoningPageRetiresChildExtractionAndKeepsOtherClients()
    {
        DelayedBeanUiSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BeanUiStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine firstEngine, secondEngine;
        auto* first = BeanUiServiceForeign::create(&firstEngine, &firstEngine);
        auto* second = BeanUiServiceForeign::create(&secondEngine, &secondEngine);
        first->setNode(&node);
        second->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(first->ready() && second->ready(), 5000);
        QSignalSpy pages(first, &BeanUiService::pageTextReady);
        QSignalSpy extracted(first, &BeanUiService::bagDetailsExtracted);
        QSignalSpy failed(first, &BeanUiService::bagDetailsExtractionFailed);
        QSignalSpy otherExtracted(second, &BeanUiService::bagDetailsExtracted);
        const QString url = QStringLiteral("https://roaster.example/same-bag");

        const QString pageId = first->fetchPageText(url, 41);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 1, 5000);
        source.replyValues(source.calls.last(), {{"text", "page for first bag"}});
        QTRY_COMPARE_WITH_TIMEOUT(pages.size(), 1, 5000);
        const QString childId = first->extractCoffeeBagDetails(url, "page for first bag", "coffee", pageId);
        const QString otherId = second->extractCoffeeBagDetailsFromUrl(url, url, "coffee", "other-page");
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 3, 5000);
        first->abandonPageOperation(pageId); // The page itself has already completed.
        QTRY_VERIFY_WITH_TIMEOUT(source.cancelled.contains(pageId), 5000);

        const QString nextPageId = first->fetchPageText(url, 42);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 4, 5000);
        source.reply(childId, "extract");
        source.reply(childId, "extract", "late failure");
        source.reply(otherId, "extractUrl");
        source.replyValues(source.calls.last(), {{"text", "page for second bag"}});
        QTRY_COMPARE_WITH_TIMEOUT(pages.size(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(otherExtracted.size(), 1, 5000);
        QVERIFY(extracted.isEmpty());
        QVERIFY(failed.isEmpty());
        QCOMPARE(otherExtracted.first().at(0).toString(), otherId);
        QCOMPARE(otherExtracted.first().at(1).toString(), url);

        const QString nextChildId = first->extractCoffeeBagDetailsFromUrl(url, url, "coffee", nextPageId);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 5, 5000);
        source.replyValues(source.calls.last(), {{"origin", "Peru"}});
        QTRY_COMPARE_WITH_TIMEOUT(extracted.size(), 1, 5000);
        QCOMPARE(extracted.first().at(0).toString(), nextChildId);
        QCOMPARE(extracted.first().at(1).toString(), url);
        QCOMPARE(extracted.first().at(2).toMap().value("origin").toString(), QStringLiteral("Peru"));

        const QString pendingId = first->extractCoffeeBagDetailsFromUrl(url, url, "coffee", nextPageId);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 6, 5000);
        first->setNode(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QCOMPARE(failed.first().at(0).toString(), pendingId);
        QCOMPARE(failed.first().at(1).toString(), url);
        first->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(first->ready(), 5000);
        source.reply(pendingId, "extractUrl");
        QCoreApplication::processEvents();
        QCOMPARE(source.calls.size(), 6);
        QCOMPARE(extracted.size(), 1);
    }

    void sharedBeanEditorRejectsRetiredPageAndExtractionReplies()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/components/ChangeBeansDialog.qml"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.errorString()));
        const QString source = QString::fromUtf8(file.readAll());
        const auto extract = [&source](const QString& name) {
            const QString needle = QStringLiteral("function ") + name + QLatin1Char('(');
            const qsizetype start = source.indexOf(needle);
            if (start < 0 || source.indexOf(needle, start + 1) >= 0) return QString();
            int depth = 0;
            for (qsizetype i = source.indexOf(QLatin1Char('{'), start); i >= 0 && i < source.size(); ++i) {
                if (source[i] == QLatin1Char('{')) ++depth;
                else if (source[i] == QLatin1Char('}') && --depth == 0)
                    return source.mid(start, i - start + 1);
            }
            return QString();
        };
        QQmlEngine engine;
        auto* transforms = BeanUiServiceForeign::create(&engine, &engine);
        engine.globalObject().setProperty(QStringLiteral("BlobTransforms"), engine.newQObject(transforms));
        for (const QString& name : {QStringLiteral("startPageExtraction"), QStringLiteral("abandonInfoExtraction"),
                 QStringLiteral("resetForm"), QStringLiteral("onPageTextReady"), QStringLiteral("onPageTextFailed"),
                 QStringLiteral("onBagDetailsExtracted"), QStringLiteral("onBagDetailsExtractionFailed")}) {
            const QString function = extract(name);
            QVERIFY2(!function.isEmpty(), qPrintable(name));
            const auto loaded = engine.evaluate(function);
            QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        }
        // Execute the shipping close handler as well as form replacement.
        const qsizetype closed = source.indexOf(QStringLiteral("    onClosed: {"));
        QVERIFY(closed >= 0);
        const qsizetype closeStart = source.indexOf(QLatin1Char('{'), closed);
        qsizetype closeEnd = -1;
        int depth = 0;
        for (qsizetype i = closeStart; i < source.size(); ++i) {
            if (source[i] == QLatin1Char('{')) ++depth;
            else if (source[i] == QLatin1Char('}') && --depth == 0) { closeEnd = i; break; }
        }
        QVERIFY(closeEnd > closeStart);
        const auto closeHandler = engine.evaluate(QStringLiteral("function closeDialog() ")
            + source.mid(closeStart, closeEnd - closeStart + 1));
        QVERIFY2(!closeHandler.isError(), qPrintable(closeHandler.toString()));
        const auto fixture = engine.evaluate(QStringLiteral(R"JS(
            var root = this, editBagId = 41, formMode = 'edit', bagKind = 'coffee';
            var _infoOperationId = '', _extractionRequestId = '', _fetchUrl = '';
            var _pageSearchOperationId = '', _awaitingCreate = false;
            var fetchingInfo = false, infoStatus = '', _extractionCorrections = [];
            var fBeanBaseId = '', fBeanBaseData = '', fRoastLevel = '';
            var EquipmentService = {activePackageId: -1};
            var AccessibilityManager = {enabled: false};
            var TranslationManager = {translate: function(key, fallback) { return fallback; }};
            String.prototype.arg = function(value) { return this.replace(/%[1-9]/, String(value)); };
            function syncDetailFieldsFromBlob() {
                ['Origin','Region','Farm','Producer','Variety','Elevation','Process','Harvest',
                 'TastingNotes','TeaType','Garden','Cultivar','Flush','BrewTempC','LeafRatio',
                 'SteepTime'].forEach(function(key) { root['f' + key] = ''; });
            }
            function infoErrorText(error) { return error; }
            var BeanUiService = {
                pages: [], extracts: [], abandoned: [], images: [], nextId: 0,
                abandonPageOperation: function(id) { if (id) this.abandoned.push(id); },
                abandonDiagnosticOperation: function() {},
                fetchPageText: function(url, bagId) {
                    var id = 'page-' + (++this.nextId);
                    this.pages.push({id: id, bagId: bagId, url: url}); return id;
                },
                extractCoffeeBagDetails: function(url, text, kind, pageId) {
                    var id = 'extract-' + (++this.nextId);
                    this.extracts.push({id: id, pageId: pageId, kind: kind, stage: 1}); return id;
                },
                extractCoffeeBagDetailsFromUrl: function(token, url, kind, pageId) {
                    var id = 'extract-' + (++this.nextId);
                    this.extracts.push({id: id, pageId: pageId, kind: kind, stage: 2}); return id;
                },
                supportsUrlExtraction: function() { return true; },
                applyExtraction: function(blob, fields, current) {
                    return BlobTransforms.applyExtraction(blob, fields, current);
                },
                bagImageKey: function(id) { return 'bag-' + id; },
                replaceBagImageFromUrl: function(key, url) { this.images.push({key: key, url: url}); }
            };
            var url = 'https://roaster.example/same-bag';
            resetForm(); startPageExtraction(url);
            var oldPage = _infoOperationId;
            onPageTextReady(url, 'page for bag 41', oldPage);
            var oldChild = _extractionRequestId;
        )JS"));
        QVERIFY2(!fixture.isError(), qPrintable(fixture.toString()));
        QString error;
        const auto run = [&engine, &error](const QString& code) {
            const auto result = engine.evaluate(code);
            error = result.isError() ? result.toString() : QString();
            return !result.isError();
        };
        const auto value = [&engine](const QString& code) { return engine.evaluate(code); };
        QCOMPARE(value("BeanUiService.extracts.length").toInt(), 1);
        QVERIFY2(run("closeDialog();"), qPrintable(error));
        QVERIFY(!value("fetchingInfo").toBool());
        QCOMPARE(value("_extractionRequestId").toString(), QString());
        QVERIFY2(run("resetForm(); editBagId = 42; startPageExtraction(url); var newPage = _infoOperationId;"
            "onPageTextReady(url, 'late page', oldPage); onPageTextFailed(url, 'emptyPage', oldPage);"
            "onBagDetailsExtracted(oldChild, url, {origin:'Colombia', imageUrl:'https://image.example/old'});"
            "onBagDetailsExtractionFailed(oldChild, url, 'late error');"), qPrintable(error));
        QVERIFY(value("BeanUiService.abandoned.indexOf(oldPage) >= 0").toBool());
        QCOMPARE(value("BeanUiService.extracts.length").toInt(), 1);
        QCOMPARE(value("BeanUiService.images.length").toInt(), 0);
        QCOMPARE(value("fOrigin").toString(), QString());
        QVERIFY(value("fetchingInfo").toBool());
        QVERIFY2(run("onPageTextReady(url, 'page for bag 42', newPage); var newChild = _extractionRequestId;"
            "onBagDetailsExtracted(oldChild, url, {origin:'Colombia'});"), qPrintable(error));
        QCOMPARE(value("_extractionRequestId").toString(), value("newChild").toString());
        QVERIFY(value("fetchingInfo").toBool());
        QVERIFY2(run("onBagDetailsExtracted(newChild, url, {origin:'Peru', imageUrl:'https://image.example/current'});"), qPrintable(error));
        QCOMPARE(value("fOrigin").toString(), QStringLiteral("Peru"));
        QCOMPARE(value("BeanUiService.images[0].key").toString(), QStringLiteral("bag-42"));
        QVERIFY(!value("fetchingInfo").toBool());
        QCOMPARE(value("_extractionRequestId").toString(), QString());

        // The empty-page fallback and its failure retain the new request identity.
        QVERIFY2(run("resetForm(); bagKind = 'tea'; startPageExtraction(url);"
            "onPageTextFailed(url, 'emptyPage', _infoOperationId);"), qPrintable(error));
        QCOMPARE(value("BeanUiService.extracts[2].stage").toInt(), 2);
        QCOMPARE(value("BeanUiService.extracts[2].kind").toString(), QStringLiteral("tea"));
        QVERIFY2(run("onBagDetailsExtractionFailed(oldChild, url, 'late error');"), qPrintable(error));
        QVERIFY(value("fetchingInfo").toBool());
        QVERIFY2(run("onBagDetailsExtractionFailed(_extractionRequestId, url, 'notConfigured');"), qPrintable(error));
        QVERIFY(!value("fetchingInfo").toBool());
        QCOMPARE(value("infoStatus").toString(), QStringLiteral("notConfigured"));
        QVERIFY2(run("startPageExtraction(url); onPageTextReady(url, 'tea page', _infoOperationId);"
            "var replacedChild = _extractionRequestId; var replacedPage = _infoOperationId;"
            "resetForm(); onBagDetailsExtracted(replacedChild, url, {garden:'Stale garden'});"), qPrintable(error));
        QVERIFY(value("BeanUiService.abandoned.indexOf(replacedPage) >= 0").toBool());
        QCOMPARE(value("fGarden").toString(), QString());
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
        const QString profileRequestId = first->fetchCommunityProfile(QStringLiteral("shot-uuid"));
        first->researchBean("pending-research", "Roaster", "Coffee", "");
        first->applyCommunitySuggestion({{"key", QStringLiteral("pending-apply")}});
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 8, 5000);
        first->setNode(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(failedSearch.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(failedProfile.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(failedResearch.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 2, 5000);
        QCOMPARE(failedProfile.first().at(0).toString(), profileRequestId);
        QCOMPARE(failedProfile.first().at(1).toString(), QStringLiteral("shot-uuid"));
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

    void overlappingCommunityProfilesKeepRequestIdentityAndNeverReplay()
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
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);
        QSignalSpy ready(service, &BeanUiService::communityProfileReady);
        QSignalSpy failed(service, &BeanUiService::communityProfileFailed);

        const QString shotId = QStringLiteral("same-shot");
        const QString firstId = service->fetchCommunityProfile(shotId);
        const QString secondId = service->fetchCommunityProfile(shotId);
        QVERIFY(!firstId.isEmpty());
        QVERIFY(!secondId.isEmpty());
        QVERIFY(firstId != secondId);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 2, 5000);
        QCOMPARE(source.calls.at(0).id, firstId);
        QCOMPARE(source.calls.at(1).id, secondId);
        QCOMPARE(source.calls.at(0).arguments.value(QStringLiteral("shotId")).toString(), shotId);
        QCOMPARE(source.calls.at(1).arguments.value(QStringLiteral("shotId")).toString(), shotId);

        // A card waiting for the second fetch must be able to distinguish its
        // completion from an older same-shot request on this same facade.
        source.replyValues(source.calls.at(1), {{"title", QStringLiteral("Newer fetch")}});
        source.reply(firstId, QStringLiteral("communityProfile"), QStringLiteral("network"));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QCOMPARE(ready.first().at(0).toString(), secondId);
        QCOMPARE(ready.first().at(1).toString(), shotId);
        QCOMPARE(ready.first().at(2).toMap().value(QStringLiteral("title")).toString(),
                 QStringLiteral("Newer fetch"));
        QCOMPARE(failed.first().at(0).toString(), firstId);
        QCOMPARE(failed.first().at(1).toString(), shotId);
        QCOMPARE(failed.first().at(2).toString(), QStringLiteral("network"));

        const QString thirdId = service->fetchCommunityProfile(shotId);
        const QString fourthId = service->fetchCommunityProfile(shotId);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 4, 5000);
        failed.clear();
        service->setNode(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 5000);
        QStringList disconnectedIds;
        for (const auto& terminal : failed) {
            disconnectedIds.append(terminal.at(0).toString());
            QCOMPARE(terminal.at(1).toString(), shotId);
            QVERIFY(terminal.at(2).toString().contains(QStringLiteral("Connection lost")));
        }
        disconnectedIds.sort();
        QStringList expectedIds{thirdId, fourthId};
        expectedIds.sort();
        QCOMPARE(disconnectedIds, expectedIds);

        service->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);
        for (const auto& call : source.calls)
            source.replyValues(call, {{"title", QStringLiteral("Late reply")}});
        QCoreApplication::processEvents();
        QCOMPARE(source.calls.size(), 4);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(failed.size(), 2);
    }

    void sharedCommunityCardCorrelatesAndResumesSavedBagActions()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/components/CommunitySuggestionsCard.qml"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.errorString()));
        const QString source = QString::fromUtf8(file.readAll());
        const auto extract = [&source](const QString& name) {
            const QString needle = QStringLiteral("function ") + name + QLatin1Char('(');
            const qsizetype start = source.indexOf(needle);
            if (start < 0 || source.indexOf(needle, start + 1) >= 0) return QString();
            int depth = 0;
            for (qsizetype i = source.indexOf(QLatin1Char('{'), start); i >= 0 && i < source.size(); ++i) {
                if (source[i] == QLatin1Char('{')) ++depth;
                else if (source[i] == QLatin1Char('}') && --depth == 0)
                    return source.mid(start, i - start + 1);
            }
            return QString();
        };
        QJSEngine engine;
        // Execute shipping functions, including Connections handlers. Only their
        // service boundaries and QML property values are supplied by the fixture.
        for (const QString& name : {QStringLiteral("refresh"), QStringLiteral("requestOwnHistory"),
                 QStringLiteral("_lookupTitle"), QStringLiteral("_profileInstalled"),
                 QStringLiteral("_currentTemp"), QStringLiteral("_currentRatio"),
                 QStringLiteral("startApply"), QStringLiteral("startCreate"),
                 QStringLiteral("_prefill"), QStringLiteral("_openWizard"),
                 QStringLiteral("togglePending"), QStringLiteral("runPending"),
                 QStringLiteral("_suggestion"), QStringLiteral("_confirmApply"),
                 QStringLiteral("onCommunityProfileReady"), QStringLiteral("onCommunityProfileFailed")}) {
            const QString function = extract(name);
            QVERIFY2(!function.isEmpty(), qPrintable(name));
            const QJSValue loaded = engine.evaluate(function);
            QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        }
        const QJSValue fixture = engine.evaluate(QStringLiteral(R"JS(
            var root = this;
            var beanBrand = "Roaster", beanType = "Coffee", beanBaseId = "bean-id", isCoffee = true;
            var _recipeBagId = 17, _canApply = true, _commEnabled = false, _commKey = "bean-key";
            var _topShotId = "same-shot", _topProfile = {title: "Family", dose: {median: 18},
                ratio: {median: 2.06}, grinderMatched: true, grindRepresentative: "3.5"};
            var _applyRequestId = "", _applyShotId = "", _applyFetchedTitle = "", _applyProfileJson = "";
            var _applyFetching = false, _applySubmitted = false, _applyResult = "", _fetchFor = "";
            var _applyTempC = -1, _applyTempAvailable = false, pendingAction = "", _queuedRun = null;
            var selectOnApply = true, _hasTemperatureOverride = false, _temperatureOverride = 96.5;
            var ProfileService = {installed: {Family: "family.json"},
                findProfileByTitle: function(title) { return this.installed[title] || ""; },
                profileCatalogInfoForTitle: function() { return {espressoTemperatureC: 90.5}; },
                currentProfileSummary: {espressoTemperatureC: 92, targetWeightG: 40}};
            var RecipeContext = {historyReady: false, preferences: {dyeBeanWeight: 18,
                hasBrewYieldOverride: false, brewYieldOverride: 45}};
            var ProfileManager = {targetWeight: 40};
            var EquipmentService = {activePackageId: 12};
            var BeanUiService = {fetches: [], applied: [],
                fetchCommunityProfile: function(shot) {
                    this.fetches.push(shot); return "fetch-" + this.fetches.length;
                },
                applyCommunitySuggestion: function(suggestion) { this.applied.push(suggestion); }};
            var AppShell = {wizards: [], recipeWizardRequested: function(mode, args) {
                this.wizards.push({mode: mode, prefill: args.prefill}); }};
            var applyDialog = {opens: 0, closes: 0,
                open: function() { ++this.opens; }, close: function() { ++this.closes; }};
        )JS"));
        QVERIFY2(!fixture.isError(), qPrintable(fixture.toString()));
        QString error;
        const auto run = [&engine, &error](const QString& script) {
            const QJSValue result = engine.evaluate(script);
            error = result.isError() ? result.toString() : QString();
            return !result.isError();
        };
        const auto value = [&engine](const QString& expression) { return engine.evaluate(expression); };

        QCOMPARE(value(QStringLiteral("_currentTemp()")).toNumber(), 92.0);
        QCOMPARE(value(QStringLiteral("_currentRatio()")).toNumber(), 40.0 / 18.0);
        // The active recipe's ratio resolves on the host, independently of the
        // profile's stored 40 g target and the raw yield-override setting.
        QVERIFY2(run(QStringLiteral("ProfileManager.targetWeight = 54")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("_currentRatio()")).toNumber(), 3.0);
        QVERIFY2(run(QStringLiteral("_hasTemperatureOverride = true; RecipeContext.preferences.hasBrewYieldOverride = true; ProfileManager.targetWeight = 45")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("_currentTemp()")).toNumber(), 96.5);
        QCOMPARE(value(QStringLiteral("_currentRatio()")).toNumber(), 2.5);
        QVERIFY2(run(QStringLiteral("RecipeContext.preferences.dyeBeanWeight = 0")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("_currentRatio()")).toNumber(), 0.0);
        QVERIFY2(run(QStringLiteral("RecipeContext.preferences.dyeBeanWeight = 18")), qPrintable(error));
        const QVariantMap prefill = value(QStringLiteral("_prefill('profile-json')")).toVariant().toMap();
        QCOMPARE(prefill.value("profileTitle").toString(), QStringLiteral("Family"));
        QCOMPARE(prefill.value("profileJson").toString(), QStringLiteral("profile-json"));
        QCOMPARE(prefill.value("bagId").toInt(), 17);
        QCOMPARE(prefill.value("equipmentId").toInt(), 12);
        QCOMPARE(prefill.value("doseG").toDouble(), 18.0);
        QCOMPARE(prefill.value("yieldMode").toString(), QStringLiteral("ratio"));
        QCOMPARE(prefill.value("yieldValue").toDouble(), 2.1);
        QCOMPARE(prefill.value("grindPinned").toString(), QStringLiteral("3.5"));
        QVERIFY2(run(QStringLiteral("_topProfile.grinderMatched = false; _topProfile.ratio.median = 0")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("_prefill('').grindPinned")).toString(), QString());
        QCOMPARE(value(QStringLiteral("_prefill('').yieldMode")).toString(), QStringLiteral("none"));
        QVERIFY2(run(QStringLiteral("_topProfile.grinderMatched = true; _topProfile.ratio.median = 2.06; startCreate()")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("BeanUiService.fetches.length")).toInt(), 1); // Resolve the concrete title even when the family is installed.
        QCOMPARE(value(QStringLiteral("BeanUiService.fetches[0]")).toString(), QStringLiteral("same-shot"));
        QVERIFY2(run(QStringLiteral("onCommunityProfileReady('other-card', 'same-shot', {title: 'Wrong'}); onCommunityProfileFailed('other-card', 'same-shot', 'network')")), qPrintable(error));
        QVERIFY(value(QStringLiteral("_applyFetching")).toBool());
        QCOMPARE(value(QStringLiteral("AppShell.wizards.length")).toInt(), 0);
        QVERIFY2(run(QStringLiteral("onCommunityProfileReady('fetch-1', 'same-shot', {title: 'Concrete v2', profileJson: 'concrete-json'}); onCommunityProfileFailed('fetch-1', 'same-shot', 'network')")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("_applyRequestId")).toString(), QString());
        QCOMPARE(value(QStringLiteral("AppShell.wizards.length")).toInt(), 1);
        QCOMPARE(value(QStringLiteral("AppShell.wizards[0].prefill.profileTitle")).toString(), QStringLiteral("Concrete v2"));
        QCOMPARE(value(QStringLiteral("AppShell.wizards[0].prefill.profileJson")).toString(), QStringLiteral("concrete-json"));
        QVERIFY2(run(QStringLiteral("ProfileService.installed['Concrete v2'] = 'concrete.json'; startCreate()")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("BeanUiService.fetches.length")).toInt(), 1);
        QCOMPARE(value(QStringLiteral("AppShell.wizards[1].prefill.profileJson")).toString(), QString());

        QVERIFY2(run(QStringLiteral("startApply(); startApply(); onCommunityProfileReady('fetch-2', 'same-shot', {title: 'Stale'}); onCommunityProfileFailed('fetch-2', 'same-shot', 'network')")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("_applyRequestId")).toString(), QStringLiteral("fetch-3"));
        QCOMPARE(value(QStringLiteral("applyDialog.opens")).toInt(), 0);
        QVERIFY2(run(QStringLiteral("onCommunityProfileReady('fetch-3', 'same-shot', {title: 'Concrete v2', profileJson: 'apply-json', temperatureC: 94}); onCommunityProfileReady('fetch-3', 'same-shot', {title: 'Duplicate'}); _confirmApply(); _confirmApply()")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("applyDialog.opens")).toInt(), 1);
        QCOMPARE(value(QStringLiteral("_applyTempC")).toNumber(), 90.5); // Installed profile wins over fetched temperature.
        QCOMPARE(value(QStringLiteral("BeanUiService.applied.length")).toInt(), 1);
        QCOMPARE(value(QStringLiteral("BeanUiService.applied[0].title")).toString(), QStringLiteral("Concrete v2"));
        QCOMPARE(value(QStringLiteral("BeanUiService.applied[0].tempC")).toNumber(), 90.5);
        QCOMPARE(value(QStringLiteral("BeanUiService.applied[0].grind")).toString(), QStringLiteral("3.5"));
        QVERIFY2(run(QStringLiteral("startApply(); refresh(); onCommunityProfileReady('fetch-4', 'same-shot', {title: 'After refresh'}); onCommunityProfileFailed('fetch-4', 'same-shot', 'network')")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("_applyRequestId")).toString(), QString());
        QCOMPARE(value(QStringLiteral("applyDialog.opens")).toInt(), 1);
        QCOMPARE(value(QStringLiteral("_applyResult")).toString(), QString());

        // Save can arrive before the fetch. Resume once, with the saved bag's
        // identity rather than the card's old/active bag, including freezer policy.
        QVERIFY2(run(QStringLiteral("selectOnApply = false; _applyTempAvailable = false; togglePending('apply'); runPending(99, 'Saved roaster', 'Saved coffee', 'saved-bean')")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("BeanUiService.applied.length")).toInt(), 1);
        QVERIFY2(run(QStringLiteral("onCommunityProfileReady('fetch-5', 'same-shot', {title: 'Saved concrete', profileJson: 'saved-json'}); onCommunityProfileReady('fetch-5', 'same-shot', {title: 'Duplicate'}); onCommunityProfileFailed('fetch-5', 'same-shot', 'network')")), qPrintable(error));
        const QVariantMap applied = value(QStringLiteral("BeanUiService.applied[1]")).toVariant().toMap();
        QCOMPARE(value(QStringLiteral("BeanUiService.applied.length")).toInt(), 2);
        QCOMPARE(applied.value("bagId").toInt(), 99);
        QCOMPARE(applied.value("roaster").toString(), QStringLiteral("Saved roaster"));
        QCOMPARE(applied.value("coffee").toString(), QStringLiteral("Saved coffee"));
        QCOMPARE(applied.value("beanBaseId").toString(), QStringLiteral("saved-bean"));
        QCOMPARE(applied.value("profileJson").toString(), QStringLiteral("saved-json"));
        QCOMPARE(applied.value("title").toString(), QStringLiteral("Saved concrete"));
        QCOMPARE(applied.value("topShotId").toString(), QStringLiteral("same-shot"));
        QCOMPARE(applied.value("tempC").toDouble(), -1.0);
        QVERIFY(!applied.value("select").toBool());
        QCOMPARE(value(QStringLiteral("pendingAction")).toString(), QString());
        QVERIFY(value(QStringLiteral("_queuedRun === null")).toBool());

        QVERIFY2(run(QStringLiteral("refresh(); togglePending('create'); runPending(100, 'New roaster', 'New coffee', 'new-bean'); onCommunityProfileReady('fetch-6', 'same-shot', {title: 'Create concrete', profileJson: 'create-json'}); onCommunityProfileReady('fetch-6', 'same-shot', {title: 'Duplicate'})")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("AppShell.wizards.length")).toInt(), 3);
        const QVariantMap savedPrefill = value(QStringLiteral("AppShell.wizards[2].prefill")).toVariant().toMap();
        QCOMPARE(savedPrefill.value("bagId").toInt(), 100);
        QCOMPARE(savedPrefill.value("roasterName").toString(), QStringLiteral("New roaster"));
        QCOMPARE(savedPrefill.value("coffeeName").toString(), QStringLiteral("New coffee"));
        QCOMPARE(savedPrefill.value("beanBaseId").toString(), QStringLiteral("new-bean"));
        QCOMPARE(savedPrefill.value("profileJson").toString(), QStringLiteral("create-json"));
        QCOMPARE(savedPrefill.value("profileTitle").toString(), QStringLiteral("Create concrete"));
        QCOMPARE(savedPrefill.value("grindPinned").toString(), QStringLiteral("3.5"));
        QVERIFY2(run(QStringLiteral("startCreate(); onCommunityProfileFailed('fetch-7', 'same-shot', 'network'); onCommunityProfileReady('fetch-7', 'same-shot', {title: 'Late success'})")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("_applyRequestId")).toString(), QString());
        QCOMPARE(value(QStringLiteral("_applyResult")).toString(), QStringLiteral("failed"));
        QCOMPARE(value(QStringLiteral("AppShell.wizards.length")).toInt(), 3);
        QVERIFY2(run(QStringLiteral("refresh(); togglePending('create'); togglePending('create')")), qPrintable(error));
        QCOMPARE(value(QStringLiteral("pendingAction")).toString(), QString());
        QCOMPARE(value(QStringLiteral("AppShell.wizards.length")).toInt(), 3);
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
