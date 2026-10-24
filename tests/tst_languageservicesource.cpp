#include "core/settings.h"
#include "core/settings_ai.h"
#include "core/translationmanager.h"
#include "core/translationcatalog.h"
#include "remote/servicepayload.h"
#include <QJsonObject>
#include <QJsonDocument>
#include "remote/languageservice.h"
#include "remote/languageservice_qml.h"
#include "remote/languageservicesource.h"
#include "remote/languageoperationpolicy.h"
#include "remote/importservicesource.h"
#include "history/shotimporter.h"
#include "profile/profileimporter.h"
#include "rep_languageservice_source.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDir>
#include <QThread>
#include <algorithm>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QJSValue>
#include <memory>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUrl>
#include <QtTest>

class PausedLanguageReply final : public QNetworkReply
{
public:
    explicit PausedLanguageReply(const QNetworkRequest& request, QObject* parent)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
    }
    void abort() override {}
protected:
    qint64 readData(char*, qint64) override { return -1; }
};

class PausedLanguageNetwork final : public QNetworkAccessManager
{
public:
    int requests = 0;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        ++requests;
        return new PausedLanguageReply(request, this);
    }
};

class DelayedLanguageSource final : public LanguageStateSimpleSource
{
    Q_OBJECT
public:
    using LanguageStateSimpleSource::setReady;
    using LanguageStateSimpleSource::setSnapshot;

    struct Call { QString id; QString operation; QVariantMap arguments; };
    QList<Call> calls;

    DelayedLanguageSource()
    {
        setSnapshot({{QStringLiteral("translationVersion"), 1},
                     {QStringLiteral("currentLanguage"), QStringLiteral("en")},
                     {QStringLiteral("sourceId"), QStringLiteral("fixture-source")},
                     {QStringLiteral("catalogRevision"), 1}});
        setReady(true);
    }

    void refresh() override {}
    void execute(QString requestId, QString operation, QVariantMap arguments) override
    {
        calls.append({std::move(requestId), std::move(operation), std::move(arguments)});
    }

    void complete(int index, QVariantList rows, int total)
    {
        const Call call = calls.at(index);
        QVariantMap result = call.arguments;
        result.remove(QStringLiteral("limit"));
        result.insert(QStringLiteral("rows"), rows);
        result.insert(QStringLiteral("total"), total);
        emit operationCompleted(call.id, call.operation, true, result, {});
    }
};

class LanguageEditorFixture final : public QObject
{
    Q_OBJECT
public:
    struct Call { QString id; QString operation; QVariantMap args; };
    QList<Call> calls;
    bool refuseLocally = false;
    QStringList cancelledTransfers;
    Q_INVOKABLE void cancelTransfer(const QString& id) { cancelledTransfers.append(id); }
    Q_INVOKABLE QString execute(const QString& operation, const QVariantMap& args)
    {
        if (refuseLocally) return {};
        const QString id = QStringLiteral("edit-%1").arg(calls.size());
        calls.append({id, operation, args});
        return id;
    }

};

class LanguageServiceSourceTest : public QObject
{
    Q_OBJECT
private:
    QNetworkAccessManager m_network;

    static QVariantMap stringArguments(const NativeLanguageServiceSource& source,
                                       QVariantMap arguments)
    {
        const QVariantMap snapshot = source.snapshot();
        arguments.insert(QStringLiteral("language"), snapshot.value(QStringLiteral("currentLanguage")));
        arguments.insert(QStringLiteral("revision"), snapshot.value(QStringLiteral("catalogRevision")));
        arguments.insert(QStringLiteral("sourceId"), snapshot.value(QStringLiteral("sourceId")));
        return arguments;
    }

    static QJSValue qmlCall(QJSValue object, const QString& method, const QJSValueList& args = {})
    {
        return object.property(method).callWithInstance(object, args);
    }

    static QString stageLanguageFile(NativeImportServiceSource& imports, const QByteArray& bytes)
    {
        QSignalSpy completed(&imports, &ImportStateSimpleSource::operationCompleted);
        imports.execute(ServiceOperation::newId(), QStringLiteral("beginTransfer"),
            {{QStringLiteral("kind"), QStringLiteral("languageJson")},
             {QStringLiteral("files"), QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("language.json")},
                                                               {QStringLiteral("size"), bytes.size()}}}}});
        if (completed.size() != 1 || !completed.first().at(2).toBool()) return {};
        const QString id = completed.takeFirst().at(3).toMap().value(QStringLiteral("transferId")).toString();
        for (qsizetype offset = 0; offset < bytes.size(); offset += 48 * 1024) {
            imports.execute(ServiceOperation::newId(), QStringLiteral("appendTransferChunk"),
                {{QStringLiteral("transferId"), id}, {QStringLiteral("fileIndex"), 0},
                 {QStringLiteral("offset"), offset}, {QStringLiteral("data"), bytes.mid(offset, 48 * 1024)}});
            if (completed.size() != 1 || !completed.takeFirst().at(2).toBool()) return {};
        }
        imports.execute(ServiceOperation::newId(), QStringLiteral("commitTransfer"), {{QStringLiteral("transferId"), id}});
        return completed.size() == 1 && completed.first().at(2).toBool() ? id : QString();
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        LanguageService::setPublishedNode(nullptr);
        const QString translationDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/translations");
        QDir(translationDir).removeRecursively();
        Settings settings;
        settings.setValue(QStringLiteral("localization/language"), QStringLiteral("en"));
        settings.ai()->setAiProvider(QString());
        settings.ai()->setOpenaiApiKey(QString());
        QTest::failOnWarning();
    }

    void batchOperationRejectsConcurrentCanonicalWork()
    {
        QVERIFY(LanguageOperationPolicy::batchTranslateUploadAllowed(false, false, false));
        QVERIFY(!LanguageOperationPolicy::batchTranslateUploadAllowed(true, false, false));
        QVERIFY(!LanguageOperationPolicy::batchTranslateUploadAllowed(false, true, false));
        QVERIFY(!LanguageOperationPolicy::batchTranslateUploadAllowed(false, false, true));
    }

    void cleanup() { LanguageService::setPublishedNode(nullptr); }

    void requestStringsArePagedAndBounded()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        manager.registerString(QStringLiteral("language.page.a"), QStringLiteral("Page A"));
        manager.registerString(QStringLiteral("language.page.b"), QStringLiteral("Page B"));
        manager.registerString(QStringLiteral("language.page.c"), QStringLiteral("Page C"));
        manager.registerString(QStringLiteral("language.page.same-fallback"), QStringLiteral("Page A"));
        NativeLanguageServiceSource source(&manager);
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());

        source.execute(QStringLiteral("page-1"), QStringLiteral("requestStrings"),
                       stringArguments(source, {{QStringLiteral("offset"), 0}, {QStringLiteral("limit"), 2}}));
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("page-1"));
        QVERIFY(completed.at(0).at(2).toBool());
        const QVariantMap first = completed.takeFirst().at(3).toMap();
        QCOMPARE(first.value(QStringLiteral("rows")).toList().size(), 2);
        QCOMPARE(first.value(QStringLiteral("total")).toInt(), 4);
        QCOMPARE(manager.uniqueStringCount(), 3);

        source.execute(QStringLiteral("page-2"), QStringLiteral("requestStrings"),
                       stringArguments(source, {{QStringLiteral("offset"), 2}, {QStringLiteral("limit"), 2}}));
        QCOMPARE(completed.size(), 1);
        const QVariantMap second = completed.takeFirst().at(3).toMap();
        QCOMPARE(second.value(QStringLiteral("rows")).toList().size(), 2);

        source.execute(QStringLiteral("too-wide"), QStringLiteral("requestStrings"),
                       stringArguments(source, {{QStringLiteral("offset"), 0}, {QStringLiteral("limit"), 51}}));
        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.at(0).at(2).toBool());
        QCOMPARE(completed.at(0).at(4).toString(), QStringLiteral("invalidArguments"));
    }

    void unknownCommandsFailClosedAndSnapshotHasNoProviderConfiguration()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        NativeLanguageServiceSource source(&manager);
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());

        source.execute(QStringLiteral("unknown-1"), QStringLiteral("setAnything"),
                       {{QStringLiteral("apiKey"), QStringLiteral("must-not-be-forwarded")}});
        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.at(0).at(2).toBool());
        QCOMPARE(completed.at(0).at(4).toString(), QStringLiteral("unsupportedOperation"));
        const QVariantMap snapshot = source.snapshot();
        QVERIFY(!snapshot.contains(QStringLiteral("apiKey")));
        QVERIFY(!snapshot.contains(QStringLiteral("openaiApiKey")));
        QVERIFY(!snapshot.contains(QStringLiteral("provider")));
    }

    void editRefusalIsReportedAgainstItsRequest()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        NativeLanguageServiceSource source(&manager);
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());

        source.execute(QStringLiteral("edit-1"), QStringLiteral("setGroupTranslation"),
                       stringArguments(source, {{QStringLiteral("fallback"), QStringLiteral("not registered")},
                        {QStringLiteral("translation"), QStringLiteral("wrong")}}));
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("edit-1"));
        QCOMPARE(completed.at(0).at(1).toString(), QStringLiteral("setGroupTranslation"));
        QVERIFY(!completed.at(0).at(2).toBool());
        QCOMPARE(completed.at(0).at(4).toString(), QStringLiteral("writeFailed"));
    }

    void stringMutationsRejectChangedLanguageRevisionAndHost()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        manager.registerString(QStringLiteral("context.key"), QStringLiteral("Context fallback"));
        NativeLanguageServiceSource source(&manager);
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        const QStringList operations{QStringLiteral("setGroupTranslation"), QStringLiteral("clearAiTranslation"),
                                     QStringLiteral("copyAiToFinal"), QStringLiteral("clearAllAiTranslations"),
                                     QStringLiteral("requestStrings")};
        for (const QString& operation : operations) {
            QVariantMap arguments;
            if (operation == QLatin1String("requestStrings"))
                arguments = {{QStringLiteral("offset"), 0}, {QStringLiteral("limit"), 2}};
            else if (operation != QLatin1String("clearAllAiTranslations"))
                arguments.insert(QStringLiteral("fallback"), QStringLiteral("Context fallback"));
            if (operation == QLatin1String("setGroupTranslation"))
                arguments.insert(QStringLiteral("translation"), QStringLiteral("Must not save"));
            const QVariantMap current = stringArguments(source, arguments);
            for (const QString& field : {QStringLiteral("language"), QStringLiteral("revision"), QStringLiteral("sourceId")}) {
                QVariantMap stale = current;
                stale.insert(field, field == QLatin1String("revision")
                             ? QVariant(current.value(field).toLongLong() + 1) : QVariant(QStringLiteral("different")));
                source.execute(operation + field, operation, stale);
                QCOMPARE(completed.size(), 1);
                const auto result = completed.takeFirst();
                QVERIFY(!result.at(2).toBool());
                QCOMPARE(result.at(4).toString(), QStringLiteral("staleContext"));
                QCOMPARE(manager.translateString(QStringLiteral("context.key"), QStringLiteral("Context fallback")),
                         QStringLiteral("Context fallback"));
            }
        }
        QVariantMap draft = stringArguments(source, {{QStringLiteral("fallback"), QStringLiteral("Context fallback")},
                                                      {QStringLiteral("translation"), QStringLiteral("Draft")}});
        manager.setTranslation(QStringLiteral("context.key"), QStringLiteral("Another client"));
        source.execute(QStringLiteral("stale-edit"), QStringLiteral("setGroupTranslation"), draft);
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("staleContext"));
        QCOMPARE(manager.translateString(QStringLiteral("context.key"), QStringLiteral("Context fallback")), QStringLiteral("Another client"));
        source.execute(QStringLiteral("fresh-edit"), QStringLiteral("setGroupTranslation"),
                       stringArguments(source, {{QStringLiteral("fallback"), QStringLiteral("Context fallback")},
                                                {QStringLiteral("translation"), QStringLiteral("Fresh draft")}}));
        QVERIFY(completed.takeFirst().at(2).toBool());
        QCOMPARE(manager.translateString(QStringLiteral("context.key"), QStringLiteral("Context fallback")), QStringLiteral("Fresh draft"));
    }

    void capturedDeleteLanguageDoesNotRetargetAfterSelectionChanges()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        QVERIFY(manager.addLanguage(QStringLiteral("qaa"), QStringLiteral("First fixture")));
        QVERIFY(manager.addLanguage(QStringLiteral("qab"), QStringLiteral("Second fixture")));
        manager.setCurrentLanguage(QStringLiteral("qaa"));
        const QString confirmedLanguage = manager.currentLanguage();
        NativeLanguageServiceSource source(&manager);
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        manager.setCurrentLanguage(QStringLiteral("qab"));
        source.execute(QStringLiteral("captured-delete"), QStringLiteral("deleteLanguage"),
                       {{QStringLiteral("code"), confirmedLanguage}});
        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("invalidLanguage"));
        QVERIFY(manager.availableLanguages().contains(QStringLiteral("qaa")));
        QVERIFY(manager.availableLanguages().contains(QStringLiteral("qab")));
    }

    void cleanStopCompletesItsRunAsCancelledWithoutProviderTraffic()
    {
        Settings settings;
        settings.setValue(QStringLiteral("localization/language"), QStringLiteral("de"));
        settings.ai()->setAiProvider(QStringLiteral("openai"));
        settings.ai()->setOpenaiApiKey(QStringLiteral("fixture-key-never-sent"));
        PausedLanguageNetwork network;
        TranslationManager manager(&network, &settings);
        manager.registerString(QStringLiteral("cancel.key"), QStringLiteral("Cancel this fixture"));
        NativeLanguageServiceSource source(&manager);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LanguageStateSourceAPI>(&source));
        QRemoteObjectNode client;
        QVERIFY(client.connectToNode(host.hostUrl()));
        LanguageService::setPublishedNode(&client);
        QQmlEngine engine;
        auto* service = LanguageServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &LanguageService::operationCompleted);
        const QVariantMap offeredContext = service->currentContext();
        const int priorRequests = network.requests;
        for (const QString& field : {QStringLiteral("language"), QStringLiteral("revision"), QStringLiteral("sourceId")}) {
            QVariantMap staleOffer = offeredContext;
            staleOffer.insert(field, field == QLatin1String("revision")
                              ? QVariant(offeredContext.value(field).toULongLong() + 1) : QVariant(QStringLiteral("changed")));
            service->execute(QStringLiteral("autoTranslate"), staleOffer);
        }
        QTRY_COMPARE(completed.size(), 3);
        for (const auto& result : completed)
            QCOMPARE(result.at(4).toString(), QStringLiteral("staleContext"));
        QCOMPARE(network.requests, priorRequests);
        QVERIFY(!manager.isAutoTranslating());
        completed.clear();
        const QString run = service->execute(QStringLiteral("autoTranslate"), offeredContext);
        QTRY_VERIFY(manager.isAutoTranslating());
        QVERIFY(network.requests > priorRequests);
        const QString stop = service->execute(QStringLiteral("cancelAutoTranslate"));
        QTRY_COMPARE(completed.size(), 2);
        const auto runResult = completed.at(0);
        QCOMPARE(runResult.at(0).toString(), run);
        QVERIFY(!runResult.at(2).toBool());
        QCOMPARE(runResult.at(4).toString(), QStringLiteral("cancelled"));
        QCOMPARE(completed.at(1).at(0).toString(), stop);
        QVERIFY(completed.at(1).at(2).toBool());
        QVERIFY(service->errorMessage().isEmpty());
        QVERIFY(!manager.isAutoTranslating());
        settings.ai()->setAiProvider(QString());
        settings.ai()->setOpenaiApiKey(QString());
    }

    void sharedEditorKeepsDraftContextAcrossRefreshFailureRetryAndOtherEditors()
    {
        QQmlEngine engine;
        LanguageEditorFixture service;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(DECENZA_SOURCE_DIR)
            + QStringLiteral("/qml/components/LanguageStringEditor.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        engine.globalObject().setProperty(QStringLiteral("fixtureLanguageExecutor"), engine.newQObject(&service));
        const QJSValue executor = engine.evaluate(QStringLiteral(
            "(function(operation, args) { return fixtureLanguageExecutor.execute(operation, args); })"));
        QVERIFY(!executor.isError());
        const QVariantMap initial{{QStringLiteral("executeOperation"), QVariant::fromValue(executor)}};
        std::unique_ptr<QObject> first(component.createWithInitialProperties(initial));
        std::unique_ptr<QObject> second(component.createWithInitialProperties(initial));
        QVERIFY2(first && second, qPrintable(component.errorString()));
        QJSValue a = engine.newQObject(first.get());
        QJSValue b = engine.newQObject(second.get());
        const QVariantMap context{{QStringLiteral("language"), QStringLiteral("de")},
                                  {QStringLiteral("revision"), 4}, {QStringLiteral("sourceId"), QStringLiteral("host-a")}};
        const QVariantMap refreshed{{QStringLiteral("language"), QStringLiteral("da")},
                                    {QStringLiteral("revision"), 5}, {QStringLiteral("sourceId"), QStringLiteral("host-a")}};
        QVERIFY(qmlCall(a, QStringLiteral("acceptRefresh"), {engine.toScriptValue(context)}).toBool());
        QVERIFY(qmlCall(b, QStringLiteral("acceptRefresh"), {engine.toScriptValue(context)}).toBool());
        QVERIFY(qmlCall(a, QStringLiteral("beginEdit"), {QStringLiteral("Row A"), QStringLiteral("Original A")}).toBool());
        QVERIFY(qmlCall(b, QStringLiteral("beginEdit"), {QStringLiteral("Row B"), QStringLiteral("Original B")}).toBool());
        qmlCall(a, QStringLiteral("setText"), {QStringLiteral("Row A"), QStringLiteral("Draft A")});
        qmlCall(b, QStringLiteral("setText"), {QStringLiteral("Row B"), QStringLiteral("Draft B")});
        QVERIFY(!qmlCall(a, QStringLiteral("acceptRefresh"), {engine.toScriptValue(refreshed)}).toBool());
        QCOMPARE(a.property("displayedContext").property("language").toString(), QStringLiteral("de"));
        QCOMPARE(a.property("draft").property("text").toString(), QStringLiteral("Draft A"));
        QVERIFY(!qmlCall(a, QStringLiteral("beginEdit"), {QStringLiteral("Row B"), QStringLiteral("Original B")}).toBool());
        qmlCall(a, QStringLiteral("setText"), {QStringLiteral("Row B"), QStringLiteral("Must not replace A")});
        qmlCall(a, QStringLiteral("finishEdit"), {QStringLiteral("Row B"), QStringLiteral("Must not save A")});
        QVERIFY(service.calls.isEmpty());
        QCOMPARE(a.property("draft").property("text").toString(), QStringLiteral("Draft A"));
        qmlCall(a, QStringLiteral("finishEdit"), {QStringLiteral("Row A"), QStringLiteral("Draft A")});
        QCOMPARE(service.calls.size(), 1);
        QCOMPARE(service.calls.at(0).args.value(QStringLiteral("language")).toString(), QStringLiteral("de"));
        QCOMPARE(service.calls.at(0).args.value(QStringLiteral("revision")).toInt(), 4);
        QVERIFY(!qmlCall(a, QStringLiteral("discard"), {}).toBool());
        qmlCall(a, QStringLiteral("complete"), {service.calls.at(0).id, QStringLiteral("setGroupTranslation"), false, QStringLiteral("staleContext")});
        QCOMPARE(a.property("errorReason").toString(), QStringLiteral("staleContext"));
        QCOMPARE(a.property("draft").property("text").toString(), QStringLiteral("Draft A"));
        qmlCall(a, QStringLiteral("finishEdit"), {QStringLiteral("Row A"), QStringLiteral("Retry A")});
        QCOMPARE(service.calls.size(), 2);
        QCOMPARE(service.calls.at(1).args.value(QStringLiteral("language")).toString(), QStringLiteral("de"));
        qmlCall(a, QStringLiteral("complete"), {service.calls.at(1).id, QStringLiteral("setGroupTranslation"), false, QStringLiteral("unavailable")});
        QCOMPARE(a.property("draft").property("text").toString(), QStringLiteral("Retry A"));
        QVERIFY(qmlCall(a, QStringLiteral("discard"), {}).toBool());
        QVERIFY(qmlCall(a, QStringLiteral("acceptRefresh"), {engine.toScriptValue(refreshed)}).toBool());
        QCOMPARE(a.property("displayedContext").property("language").toString(), QStringLiteral("da"));
        QCOMPARE(b.property("draft").property("text").toString(), QStringLiteral("Draft B"));
        qmlCall(b, QStringLiteral("finishEdit"), {QStringLiteral("Row B"), QStringLiteral("Draft B")});
        QCOMPARE(service.calls.size(), 3);
        qmlCall(b, QStringLiteral("complete"), {service.calls.at(2).id, QStringLiteral("setGroupTranslation"), true, QStringLiteral("")});
        QVERIFY(b.property("draft").isNull());
        QVERIFY(a.property("draft").isNull());

        service.refuseLocally = true;
        QVERIFY(qmlCall(a, QStringLiteral("beginEdit"), {QStringLiteral("Row A"), QStringLiteral("Original A")}).toBool());
        qmlCall(a, QStringLiteral("finishEdit"), {QStringLiteral("Row A"), QStringLiteral("Offline draft")});
        QCOMPARE(a.property("errorReason").toString(), QStringLiteral("unavailable"));
        QCOMPARE(a.property("draft").property("text").toString(), QStringLiteral("Offline draft"));
        QVERIFY(qmlCall(a, QStringLiteral("discard"), {}).toBool());
    }

    void languageImportRetainsBusyOrStaleTransferAndConsumesMatchingDocumentOnce()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        manager.registerString(QStringLiteral("file.key"), QStringLiteral("File fallback"));
        ShotImporter shots(nullptr);
        ProfileImporter profiles(nullptr, nullptr);
        NativeImportServiceSource imports(&shots, nullptr);
        imports.m_profiles = &profiles;
        NativeLanguageServiceSource source(&manager);
        source.setImportService(&imports);
        bool fileAllowed = false;
        source.setFileOperationAllowed([&fileAllowed] { return fileAllowed; });
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        const QByteArray document = QJsonDocument(QJsonObject{{QStringLiteral("language"), QStringLiteral("en")},
            {QStringLiteral("translations"), QJsonObject{{QStringLiteral("file.key"), QStringLiteral("Imported text")}}}}).toJson();
        const QString transfer = stageLanguageFile(imports, document);
        QVERIFY(!transfer.isEmpty());
        const QVariantMap captured = stringArguments(source, {{QStringLiteral("transferId"), transfer}});
        source.execute(QStringLiteral("busy-import"), QStringLiteral("importTranslation"), captured);
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("machineBusy"));
        QVERIFY(manager.exportTranslationCatalog().value(QStringLiteral("entries")).toObject()
                    .value(QStringLiteral("file.key")).toObject().value(QStringLiteral("translation")).toString().isEmpty());
        fileAllowed = true;
        manager.registerString(QStringLiteral("file.other"), QStringLiteral("Another client registered this"));
        source.execute(QStringLiteral("stale-import"), QStringLiteral("importTranslation"), captured);
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("staleContext"));
        source.execute(QStringLiteral("fresh-import"), QStringLiteral("importTranslation"),
                       stringArguments(source, {{QStringLiteral("transferId"), transfer}}));
        QTRY_COMPARE(completed.size(), 1);
        const auto saved = completed.takeFirst();
        QVERIFY2(saved.at(2).toBool(), qPrintable(saved.at(4).toString()));
        QVERIFY(saved.at(3).toMap().value(QStringLiteral("translationsSaved")).toBool());
        QVERIFY(saved.at(3).toMap().value(QStringLiteral("metadataSaved")).toBool());
        QCOMPARE(manager.translateString(QStringLiteral("file.key"), QStringLiteral("File fallback")), QStringLiteral("Imported text"));
        source.execute(QStringLiteral("reused-import"), QStringLiteral("importTranslation"),
                       stringArguments(source, {{QStringLiteral("transferId"), transfer}}));
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("invalidTransfer"));
        const QByteArray wrongLanguage = QJsonDocument(QJsonObject{{QStringLiteral("language"), QStringLiteral("qaa")},
            {QStringLiteral("translations"), QJsonObject{{QStringLiteral("file.key"), QStringLiteral("Wrong target")}}}}).toJson();
        const QString wrongTransfer = stageLanguageFile(imports, wrongLanguage);
        QVERIFY(!wrongTransfer.isEmpty());
        source.execute(QStringLiteral("wrong-language"), QStringLiteral("importTranslation"),
                       stringArguments(source, {{QStringLiteral("transferId"), wrongTransfer}}));
        QTRY_COMPARE(completed.size(), 1);
        const auto refused = completed.takeFirst();
        QVERIFY(!refused.at(2).toBool());
        QCOMPARE(refused.at(4).toString(), QStringLiteral("languageMismatch"));
        QVERIFY(!refused.at(3).toMap().value(QStringLiteral("translationsSaved")).toBool());
        QCOMPARE(manager.translateString(QStringLiteral("file.key"), QStringLiteral("File fallback")), QStringLiteral("Imported text"));
    }

    void languageExportUsesBoundedOpaqueChunksAndReleasesItsBundle()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        for (int i = 0; i < 6; ++i)
            manager.setTranslation(QStringLiteral("export.%1").arg(i), QString(12000, QLatin1Char('t')));
        NativeLanguageServiceSource source(&manager);
        bool fileAllowed = false;
        source.setFileOperationAllowed([&fileAllowed] { return fileAllowed; });
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        source.execute(QStringLiteral("active-export"), QStringLiteral("exportTranslation"), stringArguments(source, {}));
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("machineBusy"));
        fileAllowed = true;
        source.execute(QStringLiteral("file-export"), QStringLiteral("exportTranslation"), stringArguments(source, {}));
        QTRY_COMPARE(completed.size(), 1);
        const auto exported = completed.takeFirst();
        QVERIFY2(exported.at(2).toBool(), qPrintable(exported.at(4).toString()));
        const QVariantMap bundle = exported.at(3).toMap();
        QCOMPARE(bundle.value(QStringLiteral("mimeType")).toString(), QStringLiteral("application/json"));
        QCOMPARE(bundle.value(QStringLiteral("filter")).toString(), QStringLiteral("JSON files (*.json)"));
        QVERIFY(bundle.value(QStringLiteral("size")).toLongLong() > 48 * 1024);
        QVERIFY(bundle.value(QStringLiteral("displayName")).toString().endsWith(QStringLiteral(".json")));
        QVERIFY(!bundle.contains(QStringLiteral("path")));
        const QString id = bundle.value(QStringLiteral("bundleId")).toString();
        source.execute(QStringLiteral("busy-export"), QStringLiteral("exportTranslation"), stringArguments(source, {}));
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("busy"));
        source.execute(QStringLiteral("bad-offset"), QStringLiteral("readBundleChunk"),
                       {{QStringLiteral("bundleId"), id}, {QStringLiteral("offset"), 1}});
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("invalidOffset"));
        QByteArray received;
        bool finished = false;
        while (!finished) {
            source.execute(ServiceOperation::newId(), QStringLiteral("readBundleChunk"),
                           {{QStringLiteral("bundleId"), id}, {QStringLiteral("offset"), received.size()}});
            const auto chunk = completed.takeFirst();
            QVERIFY2(chunk.at(2).toBool(), qPrintable(chunk.at(4).toString()));
            const QVariantMap payload = chunk.at(3).toMap();
            const QByteArray bytes = payload.value(QStringLiteral("data")).toByteArray();
            QVERIFY(!bytes.isEmpty() && bytes.size() <= 48 * 1024);
            received += bytes;
            QCOMPARE(payload.value(QStringLiteral("nextOffset")).toLongLong(), qint64(received.size()));
            finished = payload.value(QStringLiteral("complete")).toBool();
        }
        QCOMPARE(qint64(received.size()), bundle.value(QStringLiteral("size")).toLongLong());
        QCOMPARE(QJsonDocument::fromJson(received).object().value(QStringLiteral("translations")).toObject().size(), 6);
        source.execute(QStringLiteral("release-export"), QStringLiteral("releaseBundle"), {{QStringLiteral("bundleId"), id}});
        QVERIFY(completed.takeFirst().at(2).toBool());
        source.execute(QStringLiteral("released-export"), QStringLiteral("readBundleChunk"),
                       {{QStringLiteral("bundleId"), id}, {QStringLiteral("offset"), 0}});
        QVERIFY(!completed.takeFirst().at(2).toBool());
    }

    void asyncFilePreparationRefusesInterveningChangesAndCorrelatedCancellation()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        manager.registerString(QStringLiteral("async.key"), QStringLiteral("Async fallback"));
        ShotImporter shots(nullptr);
        ProfileImporter profiles(nullptr, nullptr);
        NativeImportServiceSource imports(&shots, nullptr);
        imports.m_profiles = &profiles;
        NativeLanguageServiceSource source(&manager);
        source.setImportService(&imports);
        bool allowed = true;
        source.setFileOperationAllowed([&allowed] { return allowed; });
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        const QByteArray bytes = QJsonDocument(QJsonObject{{QStringLiteral("language"), QStringLiteral("en")},
            {QStringLiteral("translations"), QJsonObject{{QStringLiteral("async.key"), QStringLiteral("Imported")}}}}).toJson();
        const auto start = [&](const QString& id) {
            const QString transfer = stageLanguageFile(imports, bytes);
            if (transfer.isEmpty()) return false;
            source.execute(id, QStringLiteral("importTranslation"), stringArguments(source, {{QStringLiteral("transferId"), transfer}}));
            return true;
        };
        QVERIFY(start(QStringLiteral("changing-import")));
        QVERIFY(completed.isEmpty());
        manager.setTranslation(QStringLiteral("async.key"), QStringLiteral("Newer"));
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("staleContext"));
        QCOMPARE(manager.translateString(QStringLiteral("async.key"), QStringLiteral("Async fallback")), QStringLiteral("Newer"));
        QVERIFY(start(QStringLiteral("running-import")));
        allowed = false;
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("machineBusy"));
        allowed = true;
        QVERIFY(start(QStringLiteral("cancelled-import")));
        source.execute(QStringLiteral("unrelated-cancel"), QStringLiteral("cancelFileOperation"),
                       {{QStringLiteral("requestId"), QStringLiteral("another-client-request")}});
        const auto unrelated = completed.takeFirst();
        QVERIFY(unrelated.at(2).toBool());
        QVERIFY(!unrelated.at(3).toMap().value(QStringLiteral("cancelled")).toBool());
        QCOMPARE(source.m_activeFileRequest, QStringLiteral("cancelled-import"));
        source.execute(QStringLiteral("concurrent-export"), QStringLiteral("exportTranslation"), stringArguments(source, {}));
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("busy"));
        source.execute(QStringLiteral("cancel-request"), QStringLiteral("cancelFileOperation"),
                       {{QStringLiteral("requestId"), QStringLiteral("cancelled-import")}});
        QCOMPARE(completed.size(), 2);
        QCOMPARE(completed.first().at(0).toString(), QStringLiteral("cancelled-import"));
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("cancelled"));
        QVERIFY(completed.last().at(2).toBool());
        QTRY_VERIFY(source.m_activeFileRequest.isEmpty());
        QCOMPARE(completed.size(), 2);
        completed.clear();
        QCOMPARE(manager.translateString(QStringLiteral("async.key"), QStringLiteral("Async fallback")), QStringLiteral("Newer"));
        source.execute(QStringLiteral("stale-export"), QStringLiteral("exportTranslation"), stringArguments(source, {}));
        manager.registerString(QStringLiteral("async.new"), QStringLiteral("New registration"));
        QTRY_COMPARE(completed.size(), 1);
        const auto refused = completed.takeFirst();
        QCOMPARE(refused.at(4).toString(), QStringLiteral("staleContext"));
        QVERIFY(refused.at(3).toMap().isEmpty());
        QVERIFY(!source.m_bundles.hasBundle());
    }

    void commitSignalsCannotHideSavedOutcomeOrPublishStaleCounts()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        manager.registerString(QStringLiteral("commit.key"), QStringLiteral("Commit fallback"));
        ShotImporter shots(nullptr);
        ProfileImporter profiles(nullptr, nullptr);
        NativeImportServiceSource imports(&shots, nullptr);
        imports.m_profiles = &profiles;
        NativeLanguageServiceSource source(&manager);
        source.setImportService(&imports);
        source.setFileOperationAllowed([] { return true; });
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        connect(&manager, &TranslationManager::translationsChanged, &source, [&source, &manager] {
            source.execute(QStringLiteral("commit-cancel"), QStringLiteral("cancelFileOperation"),
                           {{QStringLiteral("requestId"), QStringLiteral("committing-import")}});
            manager.registerString(QStringLiteral("commit.new"), QStringLiteral("Newly visible fallback"));
        });
        const QByteArray bytes = QJsonDocument(QJsonObject{{QStringLiteral("language"), QStringLiteral("en")},
            {QStringLiteral("translations"), QJsonObject{{QStringLiteral("commit.key"), QStringLiteral("Saved")}}}}).toJson();
        const QString transfer = stageLanguageFile(imports, bytes);
        QVERIFY(!transfer.isEmpty());
        source.execute(QStringLiteral("committing-import"), QStringLiteral("importTranslation"),
                       stringArguments(source, {{QStringLiteral("transferId"), transfer}}));
        QTRY_COMPARE(completed.size(), 2);
        QCOMPARE(completed.first().at(0).toString(), QStringLiteral("commit-cancel"));
        QVERIFY(completed.first().at(2).toBool());
        QVERIFY(!completed.first().at(3).toMap().value(QStringLiteral("cancelled")).toBool());
        QCOMPARE(completed.last().at(0).toString(), QStringLiteral("committing-import"));
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(completed.last().at(3).toMap().value(QStringLiteral("translationsSaved")).toBool());
        QCOMPARE(manager.translateString(QStringLiteral("commit.key"), QStringLiteral("Commit fallback")), QStringLiteral("Saved"));
        QCOMPARE(source.snapshot().value(QStringLiteral("uniqueStringCount")).toInt(), 2);
        QCOMPARE(source.snapshot().value(QStringLiteral("uniqueUntranslatedCount")).toInt(), 1);
    }

    void destroyedFileOwnersCannotApplyPreparedResults()
    {
        Settings settings;
        auto manager = std::make_unique<TranslationManager>(&m_network, &settings);
        ShotImporter shots(nullptr);
        ProfileImporter profiles(nullptr, nullptr);
        NativeImportServiceSource imports(&shots, nullptr);
        imports.m_profiles = &profiles;
        const QByteArray bytes = QJsonDocument(QJsonObject{{QStringLiteral("language"), QStringLiteral("en")},
            {QStringLiteral("translations"), QJsonObject{{QStringLiteral("lifetime.key"), QStringLiteral("Must not apply")}}}}).toJson();
        auto source = std::make_unique<NativeLanguageServiceSource>(manager.get());
        source->setImportService(&imports);
        source->setFileOperationAllowed([] { return true; });
        QString transfer = stageLanguageFile(imports, bytes);
        QVERIFY(!transfer.isEmpty());
        source->execute(QStringLiteral("destroyed-source"), QStringLiteral("importTranslation"),
                        stringArguments(*source, {{QStringLiteral("transferId"), transfer}}));
        QPointer<QThread> worker = source->m_fileWorker;
        QVERIFY(worker);
        source.reset();
        QTRY_VERIFY(!worker || worker->isFinished());
        QVERIFY(manager->captureFileSnapshot().translations.isEmpty());
        source = std::make_unique<NativeLanguageServiceSource>(manager.get());
        source->setImportService(&imports);
        source->setFileOperationAllowed([] { return true; });
        transfer = stageLanguageFile(imports, bytes);
        QVERIFY(!transfer.isEmpty());
        QSignalSpy completed(source.get(), &LanguageStateSimpleSource::operationCompleted);
        source->execute(QStringLiteral("destroyed-manager"), QStringLiteral("importTranslation"),
                        stringArguments(*source, {{QStringLiteral("transferId"), transfer}}));
        manager.reset();
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("unavailable"));
        QVERIFY(source->m_activeFileRequest.isEmpty());
    }

    void languageFileMainCallbackTiming_data()
    {
        QTest::addColumn<int>("entries");
        QTest::addColumn<int>("textLength");
        QTest::newRow("realistic-4000") << 4000 << 64;
        QTest::newRow("near-limit-10000") << 10000 << 70;
    }

    void languageFileMainCallbackTiming()
    {
        QFETCH(int, entries);
        QFETCH(int, textLength);
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        QJsonObject values;
        for (int i = 0; i < entries; ++i) {
            const QString key = QStringLiteral("perf.%1").arg(i);
            manager.registerString(key, QStringLiteral("Fallback %1").arg(i));
            values.insert(key, QString(textLength, QChar(0x4e00)));
        }
        const QByteArray bytes = QJsonDocument(QJsonObject{{QStringLiteral("language"), QStringLiteral("en")},
            {QStringLiteral("translations"), values}}).toJson();
        ShotImporter shots(nullptr);
        ProfileImporter profiles(nullptr, nullptr);
        NativeImportServiceSource imports(&shots, nullptr);
        imports.m_profiles = &profiles;
        NativeLanguageServiceSource source(&manager);
        source.setImportService(&imports);
        source.setFileOperationAllowed([] { return true; });
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        QList<qint64> importCallbacks;
        QList<qint64> exportCallbacks;
        for (int trial = 0; trial < 3; ++trial) {
            const QString transfer = stageLanguageFile(imports, bytes);
            QVERIFY(!transfer.isEmpty());
            source.execute(ServiceOperation::newId(), QStringLiteral("importTranslation"),
                           stringArguments(source, {{QStringLiteral("transferId"), transfer}}));
            QTRY_COMPARE(completed.size(), 1);
            QVERIFY2(completed.first().at(2).toBool(), qPrintable(completed.first().at(4).toString()));
            QVERIFY(source.m_lastFileCallbackNanos > 0);
            importCallbacks.append(source.m_lastFileCallbackNanos);
            completed.clear();
            QCOMPARE(source.snapshot().value(QStringLiteral("uniqueStringCount")).toInt(), entries);
            QCOMPARE(source.snapshot().value(QStringLiteral("uniqueUntranslatedCount")).toInt(), 0);
            source.execute(ServiceOperation::newId(), QStringLiteral("exportTranslation"), stringArguments(source, {}));
            QTRY_COMPARE(completed.size(), 1);
            QVERIFY(completed.first().at(2).toBool());
            exportCallbacks.append(source.m_lastFileCallbackNanos);
            const QString bundle = completed.takeFirst().at(3).toMap().value(QStringLiteral("bundleId")).toString();
            source.execute(ServiceOperation::newId(), QStringLiteral("releaseBundle"), {{QStringLiteral("bundleId"), bundle}});
            QVERIFY(completed.takeFirst().at(2).toBool());
        }
        std::sort(importCallbacks.begin(), importCallbacks.end());
        std::sort(exportCallbacks.begin(), exportCallbacks.end());
        qInfo().nospace() << "language-file-main-callback entries=" << entries
            << " importMedianMs=" << double(importCallbacks.at(1)) / 1000000.0
            << " importWorstMs=" << double(importCallbacks.last()) / 1000000.0
            << " exportMedianMs=" << double(exportCallbacks.at(1)) / 1000000.0
            << " exportWorstMs=" << double(exportCallbacks.last()) / 1000000.0;
    }

    void sharedFileActionsCapturePickerTargetCorrelateAndReleaseIndependentFlows()
    {
        QQmlEngine engine;
        LanguageEditorFixture service;
        engine.globalObject().setProperty(QStringLiteral("fixtureFileExecutor"), engine.newQObject(&service));
        const QJSValue execute = engine.evaluate(QStringLiteral(
            "(function(operation,args) {return fixtureFileExecutor.execute(operation,args);})"));
        const QJSValue cancel = engine.evaluate(QStringLiteral(
            "(function(id) {fixtureFileExecutor.cancelTransfer(id);})"));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(DECENZA_SOURCE_DIR)
            + QStringLiteral("/qml/components/LanguageFileActions.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const QVariantMap initial{{QStringLiteral("executeOperation"), QVariant::fromValue(execute)},
                                  {QStringLiteral("cancelTransfer"), QVariant::fromValue(cancel)}};
        std::unique_ptr<QObject> first(component.createWithInitialProperties(initial));
        std::unique_ptr<QObject> second(component.createWithInitialProperties(initial));
        QVERIFY2(first && second, qPrintable(component.errorString()));
        QJSValue a = engine.newQObject(first.get());
        QJSValue b = engine.newQObject(second.get());
        const QVariantMap context{{QStringLiteral("sourceId"), QStringLiteral("host-a")},
                                  {QStringLiteral("language"), QStringLiteral("en")}, {QStringLiteral("revision"), 4}};
        QJSValue offered = engine.toScriptValue(context);
        QVERIFY(qmlCall(a, QStringLiteral("beginImport"), {offered}).toBool());
        QVERIFY(qmlCall(a, QStringLiteral("cancelPicker")).toBool());
        QVERIFY(service.calls.isEmpty());
        QVERIFY(qmlCall(a, QStringLiteral("beginImport"), {offered}).toBool());
        offered.setProperty(QStringLiteral("language"), QStringLiteral("da"));
        QVERIFY(qmlCall(a, QStringLiteral("acceptSelection")).toBool());
        qmlCall(a, QStringLiteral("transferCompleted"), {true,
            engine.toScriptValue(QVariantMap{{QStringLiteral("transferId"), QStringLiteral("transfer-a")}}), QString()});
        QCOMPARE(service.calls.size(), 1);
        QCOMPARE(service.calls.first().args.value(QStringLiteral("language")).toString(), QStringLiteral("en"));
        QCOMPARE(service.calls.first().args.value(QStringLiteral("revision")).toInt(), 4);
        qmlCall(a, QStringLiteral("completeOperation"), {QStringLiteral("another-client"), QStringLiteral("importTranslation"), true, engine.newObject(), QString()});
        QCOMPARE(a.property("phase").toString(), QStringLiteral("request"));
        qmlCall(a, QStringLiteral("completeOperation"), {service.calls.first().id, QStringLiteral("importTranslation"), false, engine.newObject(), QStringLiteral("staleContext")});
        QVERIFY(!a.property("busy").toBool());
        QCOMPARE(service.cancelledTransfers, QStringList{QStringLiteral("transfer-a")});
        QVERIFY(qmlCall(b, QStringLiteral("beginImport"), {offered}).toBool());
        QVERIFY(qmlCall(b, QStringLiteral("acceptSelection")).toBool());
        QVERIFY(qmlCall(a, QStringLiteral("beginExport"), {offered}).toBool());
        QCOMPARE(service.calls.size(), 2);
        const QVariantMap descriptor{{QStringLiteral("bundleId"), QStringLiteral("bundle-a")},
                                     {QStringLiteral("displayName"), QStringLiteral("translations-da.json")},
                                     {QStringLiteral("size"), 64}};
        QSignalSpy download(first.get(), SIGNAL(downloadRequested(QVariant)));
        QVERIFY(download.isValid());
        qmlCall(a, QStringLiteral("completeOperation"), {service.calls.at(1).id, QStringLiteral("exportTranslation"), true, engine.toScriptValue(descriptor), QString()});
        QCOMPARE(download.size(), 1);
        QCOMPARE(a.property("phase").toString(), QStringLiteral("download"));
        qmlCall(a, QStringLiteral("downloadCompleted"), {true, QString()});
        QVERIFY(!a.property("busy").toBool());
        QCOMPARE(b.property("phase").toString(), QStringLiteral("transfer"));
        QVERIFY(qmlCall(a, QStringLiteral("beginExport"), {offered}).toBool());
        qmlCall(a, QStringLiteral("completeOperation"), {service.calls.at(2).id, QStringLiteral("exportTranslation"), true, engine.toScriptValue(descriptor), QString()});
        qmlCall(a, QStringLiteral("refuseDownload"), {engine.toScriptValue(descriptor)});
        QCOMPARE(service.calls.last().operation, QStringLiteral("releaseBundle"));
        QCOMPARE(service.calls.last().args.value(QStringLiteral("bundleId")).toString(), QStringLiteral("bundle-a"));
        QVERIFY(!a.property("busy").toBool());
        qmlCall(b, QStringLiteral("transferCompleted"), {true,
            engine.toScriptValue(QVariantMap{{QStringLiteral("transferId"), QStringLiteral("transfer-b")}}), QString()});
        const QString destroyedRequest = service.calls.last().id;
        second.reset();
        QCOMPARE(service.cancelledTransfers.last(), QStringLiteral("transfer-b"));
        QCOMPARE(service.calls.last().operation, QStringLiteral("cancelFileOperation"));
        QCOMPARE(service.calls.last().args.value(QStringLiteral("requestId")).toString(), destroyedRequest);
    }

    void cachedUniqueCountsRefreshAfterTranslationEdits()
    {
        Settings settings;
        settings.setValue(QStringLiteral("localization/language"), QStringLiteral("de"));
        TranslationManager manager(&m_network, &settings);
        manager.registerString(QStringLiteral("cache.a"), QStringLiteral("Shared fallback"));
        manager.registerString(QStringLiteral("cache.b"), QStringLiteral("Shared fallback"));
        NativeLanguageServiceSource source(&manager);

        QCOMPARE(source.snapshot().value(QStringLiteral("uniqueStringCount")).toInt(),
                 manager.uniqueStringCount());
        QCOMPARE(source.snapshot().value(QStringLiteral("uniqueUntranslatedCount")).toInt(),
                 manager.uniqueUntranslatedCount());
        manager.setTranslation(QStringLiteral("cache.a"), QStringLiteral("Übersetzung"));
        QCOMPARE(source.snapshot().value(QStringLiteral("uniqueUntranslatedCount")).toInt(),
                 manager.uniqueUntranslatedCount());
        manager.setTranslation(QStringLiteral("cache.b"), QStringLiteral("Übersetzung"));
        QCOMPARE(source.snapshot().value(QStringLiteral("uniqueUntranslatedCount")).toInt(),
                 manager.uniqueUntranslatedCount());
    }

    void staleStringPagesCannotMixIntoRefreshedGeneration()
    {
        DelayedLanguageSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LanguageStateSourceAPI>(&source));
        QRemoteObjectNode client;
        QVERIFY(client.connectToNode(host.hostUrl()));
        LanguageService::setPublishedNode(&client);
        QQmlEngine engine;
        auto* service = LanguageServiceForeign::create(&engine, &engine);
        QVERIFY(service);
        QTRY_VERIFY(service->ready());
        QSignalSpy changed(service, &LanguageService::stateChanged);
        QVERIFY(changed.isValid());

        service->refreshStrings();
        QTRY_COMPARE(source.calls.size(), 1);
        QCOMPARE(source.calls.at(0).arguments.value(QStringLiteral("offset")).toInt(), 0);
        service->refreshStrings();
        QTRY_COMPARE(source.calls.size(), 2);
        QCOMPARE(source.calls.at(1).arguments.value(QStringLiteral("offset")).toInt(), 0);

        const qsizetype stateEvents = changed.size();
        source.complete(0, {QVariantMap{{QStringLiteral("key"), QStringLiteral("old-key")}}}, 2);
        QTRY_VERIFY(changed.size() > stateEvents);
        QVERIFY(service->strings().isEmpty());
        QCOMPARE(source.calls.size(), 2); // A stale page must not request the next offset.

        source.complete(1, {QVariantMap{{QStringLiteral("key"), QStringLiteral("new-key-1")}}}, 2);
        QTRY_COMPARE(source.calls.size(), 3);
        QCOMPARE(source.calls.at(2).arguments.value(QStringLiteral("offset")).toInt(), 1);
        source.complete(2, {QVariantMap{{QStringLiteral("key"), QStringLiteral("new-key-2")}}}, 2);
        QTRY_VERIFY(!service->stringsLoading());
        QCOMPARE(service->strings().size(), 2);
        QCOMPARE(service->stringsContext().value(QStringLiteral("language")).toString(), QStringLiteral("en"));
        QCOMPARE(service->stringsContext().value(QStringLiteral("revision")).toInt(), 1);
        QCOMPARE(service->strings().at(0).toMap().value(QStringLiteral("key")).toString(),
                 QStringLiteral("new-key-1"));
        QCOMPARE(service->strings().at(1).toMap().value(QStringLiteral("key")).toString(),
                 QStringLiteral("new-key-2"));

        LanguageService::setPublishedNode(nullptr);
    }

    void refreshedStringPagesPublishAtomicallyAndRejectAnotherContext()
    {
        DelayedLanguageSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LanguageStateSourceAPI>(&source));
        QRemoteObjectNode client;
        QVERIFY(client.connectToNode(host.hostUrl()));
        LanguageService::setPublishedNode(&client);
        QQmlEngine engine;
        auto* service = LanguageServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        service->refreshStrings();
        QTRY_COMPARE(source.calls.size(), 1);
        source.complete(0, {QVariantMap{{QStringLiteral("key"), QStringLiteral("original")}}}, 1);
        QTRY_VERIFY(!service->stringsLoading());
        const QVariantList original = service->strings();
        const QVariantMap originalContext = service->stringsContext();
        QVariantMap nextSnapshot = source.snapshot();
        nextSnapshot.insert(QStringLiteral("catalogRevision"), 2);
        nextSnapshot.insert(QStringLiteral("translationVersion"), 2);
        source.setSnapshot(nextSnapshot);
        QTRY_COMPARE(source.calls.size(), 2);
        QVERIFY(service->stringsLoading());
        QCOMPARE(service->strings(), original);
        QCOMPARE(service->stringsContext(), originalContext);
        source.complete(1, {QVariantMap{{QStringLiteral("key"), QStringLiteral("new-first")}}}, 2);
        QTRY_COMPARE(source.calls.size(), 3);
        const auto call = source.calls.at(2);
        QVariantMap wrongContext = call.arguments;
        wrongContext.remove(QStringLiteral("limit"));
        wrongContext.insert(QStringLiteral("revision"), 3);
        wrongContext.insert(QStringLiteral("total"), 2);
        wrongContext.insert(QStringLiteral("rows"), QVariantList{QVariantMap{{QStringLiteral("key"), QStringLiteral("wrong-second")}}});
        emit source.operationCompleted(call.id, call.operation, true, wrongContext, {});
        QTRY_VERIFY(!service->stringsLoading());
        QCOMPARE(service->errorMessage(), QStringLiteral("staleContext"));
        QCOMPARE(service->strings(), original);
        QCOMPARE(service->stringsContext(), originalContext);
        service->refreshStrings();
        QTRY_COMPARE(source.calls.size(), 4);
        source.complete(3, {QVariantMap{{QStringLiteral("key"), QStringLiteral("fresh")}}}, 1);
        QTRY_VERIFY(!service->stringsLoading());
        QCOMPARE(service->stringsContext().value(QStringLiteral("revision")).toInt(), 2);
        QCOMPARE(service->strings().first().toMap().value(QStringLiteral("key")).toString(), QStringLiteral("fresh"));
    }

    void disconnectClearsLoadingAndReconnectStartsFreshPage()
    {
        DelayedLanguageSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LanguageStateSourceAPI>(&source));
        QRemoteObjectNode client;
        QVERIFY(client.connectToNode(host.hostUrl()));
        LanguageService::setPublishedNode(&client);
        QQmlEngine engine;
        auto* service = LanguageServiceForeign::create(&engine, &engine);
        QVERIFY(service);
        QTRY_VERIFY(service->ready());

        service->refreshStrings();
        QTRY_COMPARE(source.calls.size(), 1);
        source.setReady(false);
        QTRY_VERIFY(!service->stringsLoading());
        source.setReady(true);
        QTRY_VERIFY(service->ready());
        QTRY_COMPARE(source.calls.size(), 2);
        QCOMPARE(source.calls.at(1).arguments.value(QStringLiteral("offset")).toInt(), 0);
        source.complete(1, {QVariantMap{{QStringLiteral("key"), QStringLiteral("fresh")}}}, 1);
        QTRY_VERIFY(!service->stringsLoading());
        QCOMPARE(service->strings().size(), 1);

        LanguageService::setPublishedNode(nullptr);
    }


    void catalogPagesMatchCanonicalExportAndRejectStaleRevision()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        for (int i = 0; i < 260; ++i)
            manager.registerString(QStringLiteral("catalog.%1").arg(i), QStringLiteral("Fallback %1").arg(i));
        NativeLanguageServiceSource source(&manager);
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        const qint64 revision = manager.catalogRevision();
        QCOMPARE(source.snapshot().value(QStringLiteral("catalogRevision")).toLongLong(), revision);
        const QJsonObject expected = manager.exportTranslationCatalog().value(QStringLiteral("entries")).toObject();
        QJsonObject received;
        int offset = 0;
        int pages = 0;
        do {
            source.execute(QStringLiteral("catalog-page-%1").arg(pages++), QStringLiteral("requestCatalogPage"),
                           {{QStringLiteral("revision"), revision}, {QStringLiteral("offset"), offset}});
            QCOMPARE(completed.size(), 1);
            const auto completion = completed.takeFirst();
            QVERIFY2(completion.at(2).toBool(), qPrintable(completion.at(4).toString()));
            const QVariantMap page = completion.at(3).toMap();
            QVERIFY(ServicePayload::fits(page, 512 * 1024));
            QCOMPARE(page.value(QStringLiteral("offset")).toInt(), offset);
            QCOMPARE(page.value(QStringLiteral("total")).toInt(), expected.size());
            const QVariantList rows = page.value(QStringLiteral("rows")).toList();
            QVERIFY(!rows.isEmpty());
            QVERIFY(rows.size() <= 128);
            for (const auto& value : rows) {
                QVariantMap row = value.toMap();
                const QString key = row.take(QStringLiteral("key")).toString();
                QVERIFY(!received.contains(key));
                received.insert(key, QJsonObject::fromVariantMap(row));
            }
            offset += rows.size();
        } while (offset < expected.size());
        QCOMPARE(received, expected);
        QVERIFY(pages >= 3);

        const int translationVersion = manager.translationVersion();
        manager.registerString(QStringLiteral("catalog.new"), QStringLiteral("Registered after first capture"));
        QCOMPARE(manager.translationVersion(), translationVersion);
        QVERIFY(manager.catalogRevision() > static_cast<quint64>(revision));
        source.execute(QStringLiteral("catalog-stale"), QStringLiteral("requestCatalogPage"),
                       {{QStringLiteral("revision"), revision}, {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.size(), 1);
        const auto stale = completed.takeFirst();
        QVERIFY(!stale.at(2).toBool());
        QCOMPARE(stale.at(4).toString(), QStringLiteral("staleRevision"));
        QTRY_COMPARE(source.snapshot().value(QStringLiteral("catalogRevision")).toULongLong(), manager.catalogRevision());
    }

    void catalogPagesBoundSerializedBytesAndRejectInvalidCatalogs()
    {
        Settings settings;
        TranslationManager manager(&m_network, &settings);
        const QString text(TranslationCatalog::MaximumTextLength, QLatin1Char('x'));
        for (int i = 0; i < 20; ++i)
            manager.registerString(QStringLiteral("large.%1").arg(i), text);
        NativeLanguageServiceSource source(&manager);
        QSignalSpy completed(&source, &LanguageStateSimpleSource::operationCompleted);
        source.execute(QStringLiteral("large-page"), QStringLiteral("requestCatalogPage"),
                       {{QStringLiteral("revision"), QVariant::fromValue<qlonglong>(manager.catalogRevision())},
                        {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.size(), 1);
        const auto result = completed.takeFirst();
        QVERIFY2(result.at(2).toBool(), qPrintable(result.at(4).toString()));
        const QVariantMap page = result.at(3).toMap();
        QVERIFY(ServicePayload::fits(page, 512 * 1024));
        QVERIFY(!page.value(QStringLiteral("rows")).toList().isEmpty());
        QVERIFY(page.value(QStringLiteral("rows")).toList().size() < 20);

        manager.registerString(QStringLiteral("oversized"), text + QLatin1Char('x'));
        source.execute(QStringLiteral("bad-catalog"), QStringLiteral("requestCatalogPage"),
                       {{QStringLiteral("revision"), QVariant::fromValue<qlonglong>(manager.catalogRevision())},
                        {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.size(), 1);
        const auto invalid = completed.takeFirst();
        QVERIFY(!invalid.at(2).toBool());
        QVERIFY(invalid.at(3).toMap().isEmpty());
        QVERIFY(invalid.at(4).toString().contains(QStringLiteral("size limit")));
    }

    void catalogFacadeUsesCorrelatedReadOperation()
    {
        DelayedLanguageSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LanguageStateSourceAPI>(&source));
        QRemoteObjectNode client;
        QVERIFY(client.connectToNode(host.hostUrl()));
        LanguageService::setPublishedNode(&client);
        QQmlEngine engine;
        auto* service = LanguageServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &LanguageService::operationCompleted);
        const QString id = service->requestCatalogPage(7, 128);
        QTRY_COMPARE(source.calls.size(), 1);
        QCOMPARE(source.calls.first().id, id);
        QCOMPARE(source.calls.first().operation, QStringLiteral("requestCatalogPage"));
        QCOMPARE(source.calls.first().arguments.value(QStringLiteral("revision")).toLongLong(), qint64(7));
        QCOMPARE(source.calls.first().arguments.value(QStringLiteral("offset")).toInt(), 128);
        source.complete(0, {}, 128);
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QCOMPARE(completed.first().at(1).toString(), QStringLiteral("requestCatalogPage"));
    }

};

QTEST_GUILESS_MAIN(LanguageServiceSourceTest)
#include "tst_languageservicesource.moc"
