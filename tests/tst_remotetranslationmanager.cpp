#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <memory>

#include "core/translationcatalog.h"
#include "remote/languageservice.h"
#include "rep_languageservice_source.h"
#include "webclient/remotetranslationmanager.h"

namespace {
QVariantMap row(const QString& key, const QString& translation,
                const QString& fallback = QStringLiteral("Registered fallback"))
{
    return {{QStringLiteral("key"), key}, {QStringLiteral("fallback"), fallback},
            {QStringLiteral("translation"), translation}};
}
}

class CatalogLanguageSource final : public LanguageStateSimpleSource
{
    Q_OBJECT
public:
    struct Call { QString id; QVariantMap arguments; QString language; };
    QList<Call> calls;

    CatalogLanguageSource() { publish(1); setReady(true); }
    void publish(qint64 revision, const QString& language = QStringLiteral("fr"))
    {
        // Registration advances catalogRevision without changing translationVersion.
        setSnapshot({{QStringLiteral("catalogRevision"), revision},
                     {QStringLiteral("translationVersion"), 1},
                     {QStringLiteral("currentLanguage"), language}});
    }
    void refresh() override {}
    void execute(QString requestId, QString operation, QVariantMap arguments) override
    {
        if (operation == QLatin1String("requestCatalogPage"))
            calls.append({requestId, arguments, snapshot().value(QStringLiteral("currentLanguage")).toString()});
    }
    QVariantMap result(int index, const QVariantList& rows, int total) const
    {
        const auto& call = calls.at(index);
        return {{QStringLiteral("revision"), call.arguments.value(QStringLiteral("revision"))},
                {QStringLiteral("offset"), call.arguments.value(QStringLiteral("offset"))},
                {QStringLiteral("language"), call.language},
                {QStringLiteral("total"), total}, {QStringLiteral("rows"), rows}};
    }
    void complete(int index, const QVariantMap& page)
    {
        emit operationCompleted(calls.at(index).id, QStringLiteral("requestCatalogPage"), true, page, {});
    }
};

struct CatalogFixture
{
    CatalogLanguageSource source;
    QRemoteObjectHost host;
    QRemoteObjectNode node;
    QQmlEngine engine;
    LanguageService* service = nullptr;
    RemoteTranslationManager* manager = nullptr;

    bool start()
    {
        if (!host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0")))
            || !host.enableRemoting<LanguageStateSourceAPI>(&source)
            || !node.connectToNode(host.hostUrl()))
            return false;
        LanguageService::setPublishedNode(&node);
        service = LanguageService::create(&engine, &engine);
        manager = RemoteTranslationManager::create(&engine, &engine);
        manager->setLanguageService(service);
        return true;
    }
    ~CatalogFixture() { LanguageService::setPublishedNode(nullptr); }
};

class tst_RemoteTranslationManager final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void callableBindingPublishesOnlyCompleteRevisions()
    {
        CatalogFixture fixture;
        QVERIFY(fixture.start());
        QVERIFY(fixture.manager->translateFn().isCallable());
        QQmlComponent component(&fixture.engine);
        component.setData(QByteArrayLiteral(
            "import QtQml\nQtObject { required property QtObject translator; "
            "readonly property string label: translator.translate('greeting', 'Hello') }"), QUrl());
        auto root = std::unique_ptr<QObject>(component.createWithInitialProperties(
            {{QStringLiteral("translator"), QVariant::fromValue<QObject*>(fixture.manager)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        QCOMPARE(root->property("label").toString(), QStringLiteral("Hello"));
        QTRY_COMPARE(fixture.source.calls.size(), 1);
        fixture.source.complete(0, fixture.source.result(0, {row(QStringLiteral("greeting"), QStringLiteral("Bonjour"))}, 2));
        QTRY_COMPARE(fixture.source.calls.size(), 2);
        QVERIFY(!fixture.manager->catalogReady());
        QCOMPARE(root->property("label").toString(), QStringLiteral("Hello"));
        fixture.source.complete(1, fixture.source.result(1, {row(QStringLiteral("empty"), {})}, 2));
        QTRY_VERIFY(fixture.manager->catalogReady());
        QTRY_COMPARE(root->property("label").toString(), QStringLiteral("Bonjour"));
        QCOMPARE(fixture.manager->currentLanguage(), QStringLiteral("fr"));
        const QString escapedFallback = QStringLiteral("Literal \\n <br> & 'quoted'");
        QCOMPARE(fixture.manager->translateString(QStringLiteral("empty"), escapedFallback), escapedFallback);
        const qint64 version = fixture.manager->translationVersion();

        fixture.source.publish(2, QStringLiteral("de"));
        QTRY_COMPARE(fixture.source.calls.size(), 3);
        QVERIFY(!fixture.manager->catalogReady());
        fixture.source.complete(2, fixture.source.result(2, {row(QStringLiteral("greeting"), QStringLiteral("Hallo"))}, 1));
        QTRY_COMPARE(root->property("label").toString(), QStringLiteral("Hallo"));
        QCOMPARE(fixture.manager->currentLanguage(), QStringLiteral("de"));
        QVERIFY(fixture.manager->translationVersion() > version);
        QVERIFY(fixture.manager->hasTranslation(QStringLiteral("greeting")));
    }

    void oldRevisionAndUnrelatedCompletionsCannotPublish()
    {
        CatalogFixture fixture;
        QVERIFY(fixture.start());
        QTRY_COMPARE(fixture.source.calls.size(), 1);
        fixture.source.publish(2);
        QTRY_COMPARE(fixture.source.calls.size(), 2);
        fixture.source.complete(0, fixture.source.result(0, {row(QStringLiteral("greeting"), QStringLiteral("Old"))}, 1));
        emit fixture.source.operationCompleted(QStringLiteral("another-client"), QStringLiteral("requestCatalogPage"),
                                               true, fixture.source.result(1, {}, 0), {});
        fixture.source.complete(1, fixture.source.result(1, {row(QStringLiteral("greeting"), QStringLiteral("New"))}, 1));
        QTRY_VERIFY(fixture.manager->catalogReady());
        QCOMPARE(fixture.manager->translateString(QStringLiteral("greeting"), QStringLiteral("Hello")), QStringLiteral("New"));
        fixture.source.complete(0, fixture.source.result(0, {}, 0));
        QCoreApplication::processEvents();
        QCOMPARE(fixture.manager->translateString(QStringLiteral("greeting"), QStringLiteral("Hello")), QStringLiteral("New"));
    }

    void malformedOrIncompletePagesFailClosed_data()
    {
        QTest::addColumn<QString>("defect");
        for (const QString& defect : {QStringLiteral("revision"), QStringLiteral("language"),
                 QStringLiteral("offset"), QStringLiteral("total"), QStringLiteral("changedTotal"),
                 QStringLiteral("empty"), QStringLiteral("entryTooLarge"), QStringLiteral("duplicate"),
                 QStringLiteral("pageTooLarge")})
            QTest::newRow(qPrintable(defect)) << defect;
    }

    void malformedOrIncompletePagesFailClosed()
    {
        QFETCH(QString, defect);
        CatalogFixture fixture;
        QVERIFY(fixture.start());
        QTRY_COMPARE(fixture.source.calls.size(), 1);
        int index = 0;
        if (defect == QLatin1String("changedTotal")) {
            fixture.source.complete(0, fixture.source.result(0, {row(QStringLiteral("first"), QStringLiteral("A"))}, 2));
            QTRY_COMPARE(fixture.source.calls.size(), 2);
            index = 1;
        }
        QVariantMap page = fixture.source.result(index, {row(QStringLiteral("greeting"), QStringLiteral("Wrong"))}, 1);
        if (defect == QLatin1String("revision")) page.insert(QStringLiteral("revision"), 0);
        if (defect == QLatin1String("language")) page.insert(QStringLiteral("language"), QStringLiteral("de"));
        if (defect == QLatin1String("offset")) page.insert(QStringLiteral("offset"), 1);
        if (defect == QLatin1String("total")) page.insert(QStringLiteral("total"), TranslationCatalog::MaximumEntries + 1);
        if (defect == QLatin1String("changedTotal")) page.insert(QStringLiteral("total"), 3);
        if (defect == QLatin1String("empty")) page.insert(QStringLiteral("rows"), QVariantList{});
        if (defect == QLatin1String("entryTooLarge"))
            page.insert(QStringLiteral("rows"), QVariantList{row(QStringLiteral("greeting"), QString(TranslationCatalog::MaximumTextLength + 1, QLatin1Char('x')))});
        if (defect == QLatin1String("duplicate")) {
            page.insert(QStringLiteral("total"), 2);
            page.insert(QStringLiteral("rows"), QVariantList{row(QStringLiteral("same"), {}), row(QStringLiteral("same"), {})});
        }
        if (defect == QLatin1String("pageTooLarge")) {
            QVariantList rows;
            const QString text(TranslationCatalog::MaximumTextLength, QLatin1Char('x'));
            for (int i = 0; i < 16; ++i) rows.append(row(QString::number(i), text, text));
            page.insert(QStringLiteral("total"), 16);
            page.insert(QStringLiteral("rows"), rows);
        }
        fixture.source.complete(index, page);
        QTRY_COMPARE(fixture.manager->catalogStatus(), QStringLiteral("incompatible"));
        QVERIFY(!fixture.manager->catalogReady());
        QCOMPARE(fixture.manager->translateString(QStringLiteral("greeting"), QStringLiteral("Hello")), QStringLiteral("Hello"));
    }

    void disconnectAndServiceReplacementDiscardPendingPages()
    {
        CatalogFixture fixture;
        QVERIFY(fixture.start());
        QTRY_COMPARE(fixture.source.calls.size(), 1);
        fixture.source.complete(0, fixture.source.result(0, {row(QStringLiteral("greeting"), QStringLiteral("Bonjour"))}, 1));
        QTRY_VERIFY(fixture.manager->catalogReady());
        fixture.service->setNode(nullptr);
        QVERIFY(!fixture.manager->catalogReady());
        QCOMPARE(fixture.manager->translateString(QStringLiteral("greeting"), QStringLiteral("Hello")), QStringLiteral("Hello"));
        fixture.service->setNode(&fixture.node);
        QTRY_COMPARE(fixture.source.calls.size(), 2);
        auto* replacement = LanguageService::create(&fixture.engine, &fixture.engine);
        fixture.manager->setLanguageService(replacement);
        QTRY_COMPARE(fixture.source.calls.size(), 3);
        fixture.source.complete(1, fixture.source.result(1, {row(QStringLiteral("greeting"), QStringLiteral("Old"))}, 1));
        fixture.source.complete(2, fixture.source.result(2, {row(QStringLiteral("greeting"), QStringLiteral("New"))}, 1));
        QTRY_VERIFY(fixture.manager->catalogReady());
        QCOMPARE(fixture.manager->translateString(QStringLiteral("greeting"), QStringLiteral("Hello")), QStringLiteral("New"));
        delete replacement;
        QCOMPARE(fixture.manager->catalogStatus(), QStringLiteral("disconnected"));
        QVERIFY(!fixture.manager->catalogReady());
    }
};

QTEST_GUILESS_MAIN(tst_RemoteTranslationManager)
#include "tst_remotetranslationmanager.moc"
