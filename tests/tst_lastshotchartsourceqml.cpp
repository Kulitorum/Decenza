#include <QtTest>

#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <memory>

class LastShotHistoryFixture final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready MEMBER ready NOTIFY stateChanged)
    Q_PROPERTY(int historyRevision MEMBER historyRevision NOTIFY historyChanged)
public:
    bool ready = false;
    int historyRevision = 0;
    QStringList recentRequests;
    QStringList shotRequests;
    QList<qint64> requestedShots;

    Q_INVOKABLE QString requestMostRecentShotId()
    {
        const QString id = QStringLiteral("recent-%1").arg(recentRequests.size());
        recentRequests.append(id);
        return id;
    }
    Q_INVOKABLE QString requestShot(qint64 shotId)
    {
        const QString id = QStringLiteral("shot-%1").arg(shotRequests.size());
        shotRequests.append(id);
        requestedShots.append(shotId);
        return id;
    }
    void setReady(bool value)
    {
        ready = value;
        emit stateChanged();
    }
    void changeHistory()
    {
        ++historyRevision;
        emit historyChanged();
    }
    void finishRecent(const QString& requestId, qint64 shotId)
    {
        emit operationCompleted(requestId, QStringLiteral("mostRecentShotId"), true,
                                {{QStringLiteral("shotId"), shotId}}, {});
    }
    void finishShot(const QString& requestId, qint64 shotId, double pressure = 9.0)
    {
        const QVariantList samples = pressure > 0
            ? QVariantList{QVariantMap{{QStringLiteral("x"), 0}, {QStringLiteral("y"), pressure}}}
            : QVariantList{};
        emit operationCompleted(requestId, QStringLiteral("shot"), true,
            {{QStringLiteral("shotId"), shotId},
             {QStringLiteral("shot"), QVariantMap{{QStringLiteral("id"), shotId},
                                                  {QStringLiteral("pressure"), samples}}}}, {});
    }

signals:
    void stateChanged();
    void historyChanged();
    void operationCompleted(const QString& requestId, const QString& operation, bool success,
                            const QVariantMap& result, const QString& reason);
    void shotReady(qint64 shotId, const QVariantMap& shot);
};

class LastShotChartSourceQmlTest final : public QObject
{
    Q_OBJECT

    static QVariantMap settings(bool selected)
    {
        return {{QStringLiteral("theme"), QVariantMap{
                    {QStringLiteral("backgroundSource"), selected ? QStringLiteral("shot") : QStringLiteral("color")},
                    {QStringLiteral("backgroundShotAdvanced"), false},
                    {QStringLiteral("isDarkMode"), true},
                    {QStringLiteral("customThemeColors"), QVariantMap{}},
                    {QStringLiteral("effectiveFontSizes"), QVariantMap{}}}},
                {QStringLiteral("graph"), QVariantMap{{QStringLiteral("rightAxisMode"), QStringLiteral("weight")},
                                                     {QStringLiteral("flowMultiplier"), 1}}},
                {QStringLiteral("app"), QVariantMap{{QStringLiteral("temperatureUnit"), QStringLiteral("C")}}}};
    }

    static std::unique_ptr<QObject> createSource(QQmlEngine& engine,
                                                LastShotHistoryFixture& history,
                                                bool selected = true)
    {
        QFile file(QStringLiteral(LAST_SHOT_CHART_SOURCE_QML_FILE));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        QByteArray source = file.readAll();
        source.replace("pragma Singleton", "");
        source.replace("import Decenza", "");
        // Instantiate the production singleton as an ordinary object, supplying
        // dependencies through initial properties instead of a runtime QML registry.
        source.replace("HistoryService", "fixtureHistory");
        source.replace("Settings.", "fixtureSettings.");
        source.replace("Theme.", "fixtureTheme.");
        source.replace("GraphSeries.", "fixtureGraphSeries.");
        source.replace("WebDebugLogger.", "fixtureLog.");
        source.replace("id: root", R"(
            id: root
            required property QtObject fixtureHistory
            required property var fixtureSettings
            property var fixtureTheme: ({ fontFamilies: [] })
            property var fixtureGraphSeries: ({ entries: [] })
            property QtObject fixtureLog: QtObject {
                function warn(subsystem, tag, message) {}
            }
        )");
        QQmlComponent component(&engine);
        component.setData(source, QUrl::fromLocalFile(file.fileName()));
        if (!component.isReady()) {
            QTest::qFail(qPrintable(component.errorString()), __FILE__, __LINE__);
            return {};
        }
        auto object = std::unique_ptr<QObject>(component.createWithInitialProperties({
            {QStringLiteral("fixtureHistory"), QVariant::fromValue<QObject*>(&history)},
            {QStringLiteral("fixtureSettings"), settings(selected)}}));
        if (!object)
            QTest::qFail(qPrintable(component.errorString()), __FILE__, __LINE__);
        return object;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void waitsForReadyAndRejectsOtherConsumersAndSupersededLoads()
    {
        QQmlEngine engine;
        LastShotHistoryFixture history;
        auto source = createSource(engine, history);
        QVERIFY(source);
        QVERIFY(history.recentRequests.isEmpty());
        QVERIFY(!source->property("ready").toBool());
        history.setReady(true);
        QCOMPARE(history.recentRequests.size(), 1);
        history.finishRecent(QStringLiteral("another-consumer"), 99);
        QVERIFY(history.shotRequests.isEmpty());
        history.finishRecent(history.recentRequests.last(), 42);
        QCOMPARE(history.requestedShots, QList<qint64>{42});
        const QString staleShotRequest = history.shotRequests.last();

        // History changes while the detail load is in flight. Neither its old
        // completion nor a concurrent detail view of the same shot may win.
        history.changeHistory();
        QCOMPARE(history.recentRequests.size(), 2);
        history.finishRecent(history.recentRequests.last(), 43);
        history.finishShot(staleShotRequest, 42);
        QVERIFY(!source->property("hasShot").toBool());
        emit history.shotReady(43, {{QStringLiteral("id"), 43}});
        history.finishShot(QStringLiteral("detail-request"), 43);
        QVERIFY(!source->property("hasShot").toBool());
        history.finishShot(history.shotRequests.last(), 43);
        QVERIFY(source->property("hasShot").toBool());
        QCOMPARE(source->property("_shotId").toLongLong(), qint64(43));
    }

    void deletionDuringLoadAndNoSamplesClearTheRenderedBackground()
    {
        QQmlEngine engine;
        LastShotHistoryFixture history;
        history.ready = true;
        auto source = createSource(engine, history);
        QVERIFY(source);
        history.finishRecent(history.recentRequests.last(), 42);
        history.finishShot(history.shotRequests.last(), 42);
        QVERIFY(source->property("hasShot").toBool());
        source->setProperty("_renderedUrl", "image://old-shot");

        history.changeHistory();
        const QString oldLookup = history.recentRequests.last();
        history.finishRecent(oldLookup, 42);
        const QString deletedShotLoad = history.shotRequests.last();
        history.changeHistory();
        history.finishRecent(history.recentRequests.last(), -1);
        history.finishShot(deletedShotLoad, 42);
        QVERIFY(source->property("ready").toBool());
        QVERIFY(!source->property("hasShot").toBool());
        QVERIFY(source->property("imageSource").toString().isEmpty());

        history.changeHistory();
        history.finishRecent(history.recentRequests.last(), 43);
        history.finishShot(history.shotRequests.last(), 43, 0);
        QVERIFY(source->property("ready").toBool());
        QVERIFY(!source->property("hasShot").toBool());
    }

    void reconnectAndSameShotReloadInvalidateRenderCache()
    {
        QQmlEngine engine;
        LastShotHistoryFixture history;
        history.ready = true;
        auto source = createSource(engine, history);
        QVERIFY(source);
        history.finishRecent(history.recentRequests.last(), 42);
        history.finishShot(history.shotRequests.last(), 42);
        const QString oldKey = source->property("cacheKey").toString();
        source->setProperty("_renderedUrl", "image://old-host");
        history.changeHistory();
        history.finishRecent(history.recentRequests.last(), 42);
        const QString disconnectedRequest = history.shotRequests.last();
        history.setReady(false);
        QVERIFY(source->property("imageSource").toString().isEmpty());
        QVERIFY(!source->property("ready").toBool());
        history.setReady(true);
        history.finishShot(disconnectedRequest, 42);
        QVERIFY(!source->property("hasShot").toBool());
        history.finishRecent(history.recentRequests.last(), 42);
        history.finishShot(history.shotRequests.last(), 42, 8);
        QVERIFY(source->property("hasShot").toBool());
        QVERIFY(source->property("cacheKey").toString() != oldKey);
    }

    void chooserLoadsOnDemandAndResumesWhenHistoryBecomesReady()
    {
        QQmlEngine engine;
        LastShotHistoryFixture history;
        auto source = createSource(engine, history, false);
        QVERIFY(source);
        history.setReady(true);
        QVERIFY(history.recentRequests.isEmpty());
        history.changeHistory();
        QVERIFY(history.recentRequests.isEmpty());
        history.setReady(false);
        QVERIFY(QMetaObject::invokeMethod(source.get(), "ensureLoaded"));
        history.setReady(true);
        QCOMPARE(history.recentRequests.size(), 1);
        history.finishRecent(history.recentRequests.last(), -1);
        QVERIFY(source->property("ready").toBool());
        history.changeHistory();
        QCOMPARE(history.recentRequests.size(), 1);
    }
};

QTEST_GUILESS_MAIN(LastShotChartSourceQmlTest)
#include "tst_lastshotchartsourceqml.moc"
