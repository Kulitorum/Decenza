#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QSignalSpy>
#include <QUrl>

#include "core/appsettings.h"
#include "core/layoutcatalog.h"
#include "core/layoutsnapshot.h"
#include "core/settings_network.h"
#include "remote/layoutservice_qml.h"
#include "remote/layoutservicesource.h"
#include "rep_layoutservice_source.h"

class PendingLayoutSource final : public LayoutStateSimpleSource
{
    Q_OBJECT
public:
    using LayoutStateSimpleSource::setReady;
    using LayoutStateSimpleSource::setRevision;
    using LayoutStateSimpleSource::setLayoutConfiguration;
    using LayoutStateSimpleSource::setErrorMessage;

    int calls = 0;
    QString lastId;
    QString lastOperation;
    qint64 lastRevision = -1;

    PendingLayoutSource()
    {
        setReady(true);
        setRevision(7);
        setLayoutConfiguration(QStringLiteral(
            R"({"zones":{"topLeft":[]},"offsets":{},"scales":{},"zoneOptions":{}})"));
    }

    void execute(QString requestId, qint64 expectedRevision,
                 QString operation, QVariantMap) override
    {
        ++calls;
        lastId = std::move(requestId);
        lastRevision = expectedRevision;
        lastOperation = std::move(operation);
    }
};

class tst_LayoutService : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning();
        LayoutService::setPublishedNode(nullptr);
        AppSettings settings;
        settings.clear();
    }

    void cleanup() { LayoutService::setPublishedNode(nullptr); }

    void snapshotHelpersMatchSettingsNetwork()
    {
        SettingsNetwork settings;
        const QJsonObject configuration{
            {QStringLiteral("zones"), QJsonObject{
                {QStringLiteral("topLeft"), QJsonArray{
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("custom")},
                                {QStringLiteral("id"), QStringLiteral("custom1")},
                                {QStringLiteral("content"), QStringLiteral("Hello")}}
                }}
            }},
            {QStringLiteral("offsets"), QJsonObject{{QStringLiteral("centerStatus"), -42}}},
            {QStringLiteral("scales"), QJsonObject{{QStringLiteral("topLeft"), 1.25}}},
            {QStringLiteral("zoneOptions"), QJsonObject{
                {QStringLiteral("topLeft"), QJsonObject{
                    {QStringLiteral("alignment"), QStringLiteral("center")}
                }}
            }}
        };
        settings.setLayoutConfiguration(QString::fromUtf8(QJsonDocument(configuration).toJson(QJsonDocument::Compact)));
        const LayoutSnapshot snapshot(settings.layoutConfiguration());

        QCOMPARE(snapshot.zoneItems(QStringLiteral("topLeft")), settings.getZoneItems(QStringLiteral("topLeft")));
        QCOMPARE(snapshot.zoneScale(QStringLiteral("topLeft")), settings.getZoneScale(QStringLiteral("topLeft")));
        QCOMPARE(snapshot.zoneYOffset(QStringLiteral("centerStatus")), settings.getZoneYOffset(QStringLiteral("centerStatus")));
        QCOMPARE(snapshot.zoneOption(QStringLiteral("topLeft"), QStringLiteral("alignment"), QStringLiteral("center")),
                 settings.getZoneOption(QStringLiteral("topLeft"), QStringLiteral("alignment"), QStringLiteral("center")));
        QCOMPARE(snapshot.itemProperties(QStringLiteral("custom1")), settings.getItemProperties(QStringLiteral("custom1")));
        QCOMPARE(snapshot.itemIsConfigured(QStringLiteral("custom1")), settings.itemIsConfigured(QStringLiteral("custom1")));
        QCOMPARE(snapshot.zoneYOffset(QStringLiteral("centerStatus")), -42);
        QCOMPARE(snapshot.zoneScale(QStringLiteral("topLeft")), 1.25);
    }

    void mutationsAreCorrelatedAndRevisionChecked()
    {
        SettingsNetwork settings;
        settings.setLayoutConfiguration(QStringLiteral(
            R"({"zones":{"topLeft":[{"type":"custom","id":"custom1"}]},"offsets":{},"scales":{},"zoneOptions":{}})"));
        NativeLayoutServiceSource source(&settings);
        QVERIFY(source.ready());
        const qint64 initialRevision = source.revision();
        QSignalSpy completed(&source, &LayoutStateSource::operationCompleted);

        source.execute(QStringLiteral("patch-a"), initialRevision, QStringLiteral("setItemProperties"), {
            {QStringLiteral("itemId"), QStringLiteral("custom1")},
            {QStringLiteral("properties"), QVariantMap{
                {QStringLiteral("content"), QStringLiteral("Updated")},
                {QStringLiteral("align"), QStringLiteral("center")}
            }}
        });
        QCOMPARE(completed.size(), 1);
        const auto success = completed.takeFirst();
        QCOMPARE(success.at(0).toString(), QStringLiteral("patch-a"));
        QCOMPARE(success.at(1).toString(), QStringLiteral("setItemProperties"));
        QVERIFY(success.at(2).toBool());
        QCOMPARE(success.at(3).toLongLong(), initialRevision + 1);
        const auto persisted = LayoutSnapshot(success.at(4).toString()).itemProperties(QStringLiteral("custom1"));
        QCOMPARE(persisted.value(QStringLiteral("content")).toString(), QStringLiteral("Updated"));
        QCOMPARE(persisted.value(QStringLiteral("align")).toString(), QStringLiteral("center"));

        const qint64 beforeDirectChange = source.revision();
        settings.setZoneScale(QStringLiteral("topLeft"), 1.5);
        QVERIFY(source.revision() > beforeDirectChange);
        source.execute(QStringLiteral("stale"), beforeDirectChange, QStringLiteral("setZoneYOffset"), {
            {QStringLiteral("zoneName"), QStringLiteral("topLeft")},
            {QStringLiteral("offset"), 21}
        });
        QCOMPARE(completed.size(), 1);
        const auto conflict = completed.takeFirst();
        QCOMPARE(conflict.at(0).toString(), QStringLiteral("stale"));
        QVERIFY(!conflict.at(2).toBool());
        QCOMPARE(conflict.at(5).toString(), QStringLiteral("conflict"));
        QCOMPARE(settings.getZoneYOffset(QStringLiteral("topLeft")), 0);
    }

    void detachedPreviewDoesNotPersistOrPublish()
    {
        SettingsNetwork settings;
        const QString before = settings.layoutConfiguration();
        SettingsNetwork preview(LayoutSnapshot(before).object());
        QSignalSpy changed(&settings, &SettingsNetwork::layoutConfigurationChanged);
        QSignalSpy previewChanged(&preview, &SettingsNetwork::layoutConfigurationChanged);
        preview.setZoneItems(QStringLiteral("topLeft"), {});
        preview.addItem(QStringLiteral("custom"), QStringLiteral("topLeft"), -1);
        QVERIFY(preview.layoutConfiguration() != before);
        QCOMPARE(settings.layoutConfiguration(), before);
        SettingsNetwork reader;
        QCOMPARE(reader.layoutConfiguration(), before);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(previewChanged.size(), 0);
        preview.resetLayoutToDefault();
        QCOMPARE(preview.layoutConfiguration(), before);
        QCOMPARE(reader.layoutConfiguration(), before);
    }

    void nearLimitLayoutCanShrinkOrReset_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QVariantMap>("fields");
        QTest::newRow("remove") << QStringLiteral("removeItem")
            << QVariantMap{{QStringLiteral("itemId"), QStringLiteral("custom1")},
                           {QStringLiteral("zone"), QStringLiteral("topLeft")}};
        QTest::newRow("shrink") << QStringLiteral("setItemProperty")
            << QVariantMap{{QStringLiteral("itemId"), QStringLiteral("custom1")},
                           {QStringLiteral("key"), QStringLiteral("content")},
                           {QStringLiteral("value"), QStringLiteral("short")}};
        QTest::newRow("reset-zone") << QStringLiteral("resetZoneToDefault")
            << QVariantMap{{QStringLiteral("zoneName"), QStringLiteral("topLeft")}};
        QTest::newRow("reset-layout") << QStringLiteral("resetLayoutToDefault") << QVariantMap{};
    }

    void nearLimitLayoutCanShrinkOrReset()
    {
        QFETCH(QString, operation);
        QFETCH(QVariantMap, fields);
        SettingsNetwork settings;
        const QJsonObject item{{QStringLiteral("type"), QStringLiteral("custom")},
                               {QStringLiteral("id"), QStringLiteral("custom1")},
                               {QStringLiteral("content"), QString(256 * 1024 - 512, QLatin1Char('x'))}};
        const QJsonObject layout{{QStringLiteral("zones"), QJsonObject{
            {QStringLiteral("topLeft"), QJsonArray{item}}}}};
        settings.setLayoutConfiguration(QString::fromUtf8(QJsonDocument(layout).toJson(QJsonDocument::Compact)));
        const qsizetype beforeBytes = settings.layoutConfiguration().toUtf8().size();
        QVERIFY(beforeBytes > 256 * 1024 - 1024);
        NativeLayoutServiceSource source(&settings);
        QVERIFY(source.ready());
        QSignalSpy completed(&source, &LayoutStateSource::operationCompleted);
        QSignalSpy changed(&settings, &SettingsNetwork::layoutConfigurationChanged);

        source.execute(QStringLiteral("shrink-layout"), source.revision(), operation, fields);

        QCOMPARE(completed.size(), 1);
        QVERIFY(completed.first().at(2).toBool());
        QVERIFY(source.ready());
        QVERIFY(settings.layoutConfiguration().toUtf8().size() < beforeBytes);
        QCOMPARE(changed.size(), 1);
    }

    void oversizedResultIsRejectedBeforePersistence()
    {
        SettingsNetwork settings;
        const QJsonObject item{{QStringLiteral("type"), QStringLiteral("custom")},
                               {QStringLiteral("id"), QStringLiteral("custom1")},
                               {QStringLiteral("content"), QString(256 * 1024 - 512, QLatin1Char('x'))}};
        const QJsonObject layout{{QStringLiteral("zones"), QJsonObject{
            {QStringLiteral("topLeft"), QJsonArray{item}}}}};
        settings.setLayoutConfiguration(QString::fromUtf8(QJsonDocument(layout).toJson(QJsonDocument::Compact)));
        NativeLayoutServiceSource source(&settings);
        const QString before = settings.layoutConfiguration();
        const qint64 revision = source.revision();
        QSignalSpy completed(&source, &LayoutStateSource::operationCompleted);
        QSignalSpy changed(&settings, &SettingsNetwork::layoutConfigurationChanged);

        source.execute(QStringLiteral("grow-layout"), revision, QStringLiteral("setItemProperty"), {
            {QStringLiteral("itemId"), QStringLiteral("custom1")},
            {QStringLiteral("key"), QStringLiteral("extra")},
            {QStringLiteral("value"), QString(1024, QLatin1Char('x'))}});

        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(5).toString(), QStringLiteral("tooLarge"));
        QCOMPARE(settings.layoutConfiguration(), before);
        QCOMPARE(source.revision(), revision);
        QCOMPARE(changed.size(), 0);
    }

    void rejectsUnknownFieldsAndMissingItemsWithoutWriting()
    {
        SettingsNetwork settings;
        settings.setLayoutConfiguration(QStringLiteral(
            R"({"zones":{"topLeft":[{"type":"custom","id":"custom1"}]},"offsets":{},"scales":{},"zoneOptions":{}})"));
        NativeLayoutServiceSource source(&settings);
        QSignalSpy completed(&source, &LayoutStateSource::operationCompleted);
        const QString before = settings.layoutConfiguration();

        source.execute(QStringLiteral("unknown-field"), source.revision(), QStringLiteral("setZoneScale"), {
            {QStringLiteral("zoneName"), QStringLiteral("topLeft")},
            {QStringLiteral("scale"), 1.4},
            {QStringLiteral("unexpected"), true}
        });
        source.execute(QStringLiteral("missing-item"), source.revision(), QStringLiteral("setItemProperty"), {
            {QStringLiteral("itemId"), QStringLiteral("deleted-item")},
            {QStringLiteral("key"), QStringLiteral("content")},
            {QStringLiteral("value"), QStringLiteral("lost")}
        });
        QCOMPARE(completed.size(), 2);
        for (const auto& result : completed) {
            QVERIFY(!result.at(2).toBool());
            QCOMPARE(result.at(5).toString(), QStringLiteral("invalidRequest"));
        }
        QCOMPARE(settings.layoutConfiguration(), before);
    }

    void rejectsInvalidZoneValuesAndMalformedZoneRows()
    {
        SettingsNetwork settings;
        settings.setLayoutConfiguration(QStringLiteral(
            R"({"zones":{"topLeft":[{"type":"custom","id":"custom1"}]},"offsets":{},"scales":{},"zoneOptions":{}})"));
        NativeLayoutServiceSource source(&settings);
        QSignalSpy completed(&source, &LayoutStateSource::operationCompleted);
        const QString before = settings.layoutConfiguration();

        source.execute(QStringLiteral("bad-option"), source.revision(), QStringLiteral("setZoneOption"), {
            {QStringLiteral("zoneName"), QStringLiteral("topLeft")},
            {QStringLiteral("key"), QStringLiteral("distribution")},
            {QStringLiteral("value"), QStringLiteral("arbitrary")}
        });
        source.execute(QStringLiteral("unknown-zone"), source.revision(), QStringLiteral("setZoneScale"), {
            {QStringLiteral("zoneName"), QStringLiteral("unknown")},
            {QStringLiteral("scale"), 1.25}
        });
        source.execute(QStringLiteral("malformed-row"), source.revision(), QStringLiteral("setZoneItems"), {
            {QStringLiteral("zoneName"), QStringLiteral("topLeft")},
            {QStringLiteral("items"), QVariantList{QVariantMap{
                {QStringLiteral("id"), QStringLiteral("no-type")}
            }}}
        });
        QVariantList tooManyRows;
        for (int i = 0; i <= 128; ++i) {
            tooManyRows.append(QVariantMap{
                {QStringLiteral("id"), QStringLiteral("item-%1").arg(i)},
                {QStringLiteral("type"), QStringLiteral("custom")}
            });
        }
        source.execute(QStringLiteral("oversized-zone"), source.revision(), QStringLiteral("setZoneItems"), {
            {QStringLiteral("zoneName"), QStringLiteral("topLeft")},
            {QStringLiteral("items"), tooManyRows}
        });
        QCOMPARE(completed.size(), 4);
        for (const auto& result : completed) {
            QVERIFY(!result.at(2).toBool());
            QCOMPARE(result.at(5).toString(), QStringLiteral("invalidRequest"));
        }
        QCOMPARE(settings.layoutConfiguration(), before);
        QCOMPARE(LayoutCatalog::zoneOptionValues(QStringLiteral("distribution")),
                 QStringList({QStringLiteral("packed"), QStringLiteral("equalWidth"), QStringLiteral("spaced")}));
    }

    void applyZonePresetUpdatesItemsAndOptionsOnce()
    {
        SettingsNetwork settings;
        settings.setLayoutConfiguration(QStringLiteral(
            R"({"zones":{"topLeft":[{"type":"custom","id":"old"}],"bottomLeft":[{"type":"sleep","id":"keep"}]},"offsets":{},"scales":{},"zoneOptions":{"topLeft":{"alignment":"right","style":"surface"}}})"));
        NativeLayoutServiceSource source(&settings);
        QSignalSpy completed(&source, &LayoutStateSource::operationCompleted);
        QSignalSpy layoutChanged(&settings, &SettingsNetwork::layoutConfigurationChanged);
        const qint64 revision = source.revision();

        source.execute(QStringLiteral("preset-1"), revision, QStringLiteral("applyZonePreset"), {
            {QStringLiteral("zoneName"), QStringLiteral("topLeft")},
            {QStringLiteral("items"), QVariantList{QVariantMap{
                {QStringLiteral("type"), QStringLiteral("profileName")},
                {QStringLiteral("id"), QStringLiteral("new-profile")}
            }}},
            {QStringLiteral("options"), QVariantMap{
                {QStringLiteral("distribution"), QStringLiteral("equalWidth")},
                {QStringLiteral("style"), QStringLiteral("accentBar")}
            }}
        });

        QCOMPARE(completed.size(), 1);
        QVERIFY(completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(3).toLongLong(), revision + 1);
        QCOMPARE(layoutChanged.size(), 1);
        const LayoutSnapshot result(settings.layoutConfiguration());
        QCOMPARE(result.zoneItems(QStringLiteral("topLeft")).first().toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("new-profile"));
        QCOMPARE(result.zoneOption(QStringLiteral("topLeft"), QStringLiteral("distribution"), QString()),
                 QStringLiteral("equalWidth"));
        QCOMPARE(result.zoneOption(QStringLiteral("topLeft"), QStringLiteral("style"), QString()),
                 QStringLiteral("accentBar"));
        QCOMPARE(result.zoneOption(QStringLiteral("topLeft"), QStringLiteral("alignment"), QString()),
                 QStringLiteral("right"));
        QCOMPARE(result.zoneItems(QStringLiteral("bottomLeft")).first().toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("keep"));
    }

    void invalidZonePresetDoesNotWriteEitherPart()
    {
        SettingsNetwork settings;
        settings.setLayoutConfiguration(QStringLiteral(
            R"({"zones":{"topLeft":[{"type":"custom","id":"old"}],"bottomLeft":[]},"offsets":{},"scales":{},"zoneOptions":{"topLeft":{"style":"surface"}}})"));
        NativeLayoutServiceSource source(&settings);
        QSignalSpy completed(&source, &LayoutStateSource::operationCompleted);
        QSignalSpy layoutChanged(&settings, &SettingsNetwork::layoutConfigurationChanged);
        const QString before = settings.layoutConfiguration();

        source.execute(QStringLiteral("bad-preset"), source.revision(), QStringLiteral("applyZonePreset"), {
            {QStringLiteral("zoneName"), QStringLiteral("topLeft")},
            {QStringLiteral("items"), QVariantList{QVariantMap{
                {QStringLiteral("type"), QStringLiteral("profileName")},
                {QStringLiteral("id"), QStringLiteral("new-profile")}
            }}},
            {QStringLiteral("options"), QVariantMap{
                {QStringLiteral("style"), QStringLiteral("not-a-style")}
            }}
        });

        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(5).toString(), QStringLiteral("invalidRequest"));
        QCOMPARE(settings.layoutConfiguration(), before);
        QCOMPARE(layoutChanged.size(), 0);
    }

    void invalidMultiFieldPatchIsRejectedAtomically()
    {
        SettingsNetwork settings;
        settings.setLayoutConfiguration(QStringLiteral(
            R"({"zones":{"topLeft":[{"type":"custom","id":"custom1","content":"before"}]},"offsets":{},"scales":{},"zoneOptions":{}})"));
        NativeLayoutServiceSource source(&settings);
        QSignalSpy completed(&source, &LayoutStateSource::operationCompleted);
        const QString before = settings.layoutConfiguration();

        source.execute(QStringLiteral("invalid-patch"), source.revision(), QStringLiteral("setItemProperties"), {
            {QStringLiteral("itemId"), QStringLiteral("custom1")},
            {QStringLiteral("properties"), QVariantMap{
                {QStringLiteral("content"), QStringLiteral("must not persist")},
                {QStringLiteral("url"), QUrl(QStringLiteral("https://example.invalid"))}
            }}
        });
        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(5).toString(), QStringLiteral("invalidRequest"));
        QCOMPARE(settings.layoutConfiguration(), before);
    }

    void facadeUsesExplicitEditorRevisionOverQtRemoteObjects()
    {
        SettingsNetwork settings;
        settings.setLayoutConfiguration(QStringLiteral(
            R"({"zones":{"topLeft":[{"type":"custom","id":"custom1"}]},"offsets":{},"scales":{},"zoneOptions":{}})"));
        NativeLayoutServiceSource source(&settings);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LayoutStateSourceAPI>(&source));
        QRemoteObjectNode clientNode;
        QVERIFY(clientNode.connectToNode(host.hostUrl()));
        LayoutService::setPublishedNode(&clientNode);
        QQmlEngine engine;
        auto* service = LayoutServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &LayoutService::operationCompleted);

        const qint64 openedAt = service->revision();
        settings.setZoneScale(QStringLiteral("topLeft"), 1.5);
        QTRY_VERIFY(service->revision() > openedAt);
        const QString requestId = service->execute(QStringLiteral("setZoneYOffset"), {
            {QStringLiteral("zoneName"), QStringLiteral("topLeft")},
            {QStringLiteral("offset"), 18}
        }, openedAt);
        QVERIFY(!requestId.isEmpty());
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), requestId);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(5).toString(), QStringLiteral("conflict"));
        QCOMPARE(settings.getZoneYOffset(QStringLiteral("topLeft")), 0);
    }

    void facadeDisconnectFailsPendingWithoutReplay()
    {
        PendingLayoutSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LayoutStateSourceAPI>(&source));
        QRemoteObjectNode clientNode;
        QVERIFY(clientNode.connectToNode(host.hostUrl()));
        LayoutService::setPublishedNode(&clientNode);
        QQmlEngine engine;
        auto* service = LayoutServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &LayoutService::operationCompleted);
        QSignalSpy stateChanged(service, &LayoutService::stateChanged);

        const QString requestId = service->execute(QStringLiteral("setZoneYOffset"), {
            {QStringLiteral("zoneName"), QStringLiteral("topLeft")},
            {QStringLiteral("offset"), 18}
        });
        QVERIFY(!requestId.isEmpty());
        QCOMPARE(service->pendingCount(), 1);
        QVERIFY(!stateChanged.isEmpty());
        QTRY_COMPARE(source.calls, 1);
        QCOMPARE(source.lastRevision, qint64(7));
        QCOMPARE(source.lastId, requestId);
        QVERIFY(service->pendingCount() == 1);

        emit source.operationCompleted(requestId, QStringLiteral("wrongOperation"), true,
                                       8, source.layoutConfiguration(), QString());
        source.setRevision(8);
        QTRY_COMPARE(service->revision(), qint64(8));
        QCOMPARE(completed.size(), 0);
        QCOMPARE(service->pendingCount(), 1);

        QVERIFY(host.disableRemoting(&source));
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), requestId);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(5).toString(), QStringLiteral("disconnected"));
        QTRY_VERIFY(!service->ready());
        QCOMPARE(source.calls, 1); // The disconnected mutation is never replayed.
        QCOMPARE(service->pendingCount(), 0);
    }
};

QTEST_GUILESS_MAIN(tst_LayoutService)
#include "tst_layoutservice.moc"
