#include <QtTest>

#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QSettings>
#include <QJsonDocument>
#include <QUuid>

#include "core/settings.h"
#include "core/settings_theme.h"
#include "remote/thememanagementservice.h"
#include "remote/thememanagementservice_qml.h"
#include "remote/thememanagementservicesource.h"

namespace {
QString requestId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}
struct ThemeValuesRestore final
{
    ThemeValuesRestore()
        : store(Settings::testQSettingsPath(), QSettings::IniFormat),
          keys{QStringLiteral("theme/userThemes"), QStringLiteral("theme/customColorsDark"),
               QStringLiteral("theme/customColorsLight"), QStringLiteral("theme/darkThemeName"),
               QStringLiteral("theme/lightThemeName"), QStringLiteral("theme/themeMode"),
               QStringLiteral("theme/activeShader"), QStringLiteral("theme/screenEffect"),
               QStringLiteral("theme/glassChrome"), QStringLiteral("theme/backgroundPreset"),
               QStringLiteral("theme/backgroundPattern"), QStringLiteral("theme/backgroundSource"),
               QStringLiteral("theme/backgroundShotAdvanced"), QStringLiteral("theme/backgroundImagePath")}
    {
        for (const QString& key : keys) {
            present.insert(key, store.contains(key));
            values.insert(key, store.value(key));
        }
    }
    ~ThemeValuesRestore()
    {
        for (const QString& key : keys) {
            if (present.value(key)) store.setValue(key, values.value(key));
            else store.remove(key);
        }
    }
    QSettings store;
    QStringList keys;
    QHash<QString, QVariant> values;
    QHash<QString, bool> present;
};
}

class PendingThemeSource final : public ThemeManagementStateSimpleSource
{
    Q_OBJECT
public:
    PendingThemeSource() { setReady(true); }
    int calls = 0;
    void refresh() override {}
    void execute(QString, QString, QString, qint64) override { ++calls; }
    void setPaletteColor(QString, QString, QString, QString, qint64) override {}
    void executeSetting(QString, QString, QVariantMap, qint64) override { ++calls; }
};

class ThemeServiceTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void cleanup() { ThemeManagementService::setPublishedNode(nullptr); }

    void projectsSafePresetRowsAndEnforcesRevisionCas()
    {
        Settings settings;
        ThemeValuesRestore restore;
        const QString legacyLongName(100, QLatin1Char('L'));
        settings.theme()->saveCurrentTheme(legacyLongName);
        NativeThemeManagementSource source(&settings);
        QSignalSpy completed(&source, &ThemeManagementStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());
        QVERIFY(source.ready());
        QVERIFY(source.presets().size() >= 2);
        const QVariantMap first = source.presets().first().toMap();
        QCOMPARE(first.size(), 3);
        QVERIFY(first.contains(QStringLiteral("name")));
        QVERIFY(first.contains(QStringLiteral("primaryColor")));
        QVERIFY(first.contains(QStringLiteral("isBuiltIn")));
        bool retainedLongName = false;
        for (const QVariant& row : source.presets())
            retainedLongName |= row.toMap().value(QStringLiteral("name")).toString() == legacyLongName;
        QVERIFY(retainedLongName);

        const qint64 revision = source.revision();
        const QString staleId = requestId();
        source.execute(staleId, QStringLiteral("apply"), QStringLiteral("Default Dark"), revision - 1);
        QCOMPARE(completed.count(), 1);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("conflict"));

        const QString applyId = requestId();
        source.execute(applyId, QStringLiteral("apply"), QStringLiteral("Default Dark"), revision);
        QCOMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(source.revision() > revision);

        const QString saveId = requestId();
        const QString savedName = QStringLiteral("Test ") + requestId();
        source.execute(saveId, QStringLiteral("save"), savedName, source.revision());
        QCOMPARE(completed.count(), 3);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(source.revision() > revision);
        const QVariantMap result = completed.last().at(4).toMap();
        QVERIFY(result.contains(QStringLiteral("presets")));
        QVERIFY(result.value(QStringLiteral("presets")).toList().last().toMap()
                    .value(QStringLiteral("name")).toString() == savedName);

        const qint64 updatedRevision = source.revision();
        const QString builtInDeleteId = requestId();
        source.execute(builtInDeleteId, QStringLiteral("delete"),
                       QStringLiteral("Default Dark"), updatedRevision);
        QCOMPARE(completed.count(), 4);
        QVERIFY(!completed.last().at(2).toBool());
        const QString deleteId = requestId();
        source.execute(deleteId, QStringLiteral("delete"), savedName,
                       updatedRevision);
        QCOMPARE(completed.count(), 5);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(!settings.theme()->themeNames().contains(savedName));
    }

    void rejectsInvalidNamesAndUnknownOperations()
    {
        Settings settings;
        ThemeValuesRestore restore;
        NativeThemeManagementSource source(&settings);
        QSignalSpy completed(&source, &ThemeManagementStateSimpleSource::operationCompleted);
        source.execute(requestId(), QStringLiteral("save"), QStringLiteral("Default Dark"), source.revision());
        source.execute(requestId(), QStringLiteral("save"), QStringLiteral(" name "), source.revision());
        source.execute(requestId(), QStringLiteral("delete"), QStringLiteral("../theme"), source.revision());
        source.execute(requestId(), QStringLiteral("writeFile"), QStringLiteral("x"), source.revision());
        QCOMPARE(completed.count(), 4);
        for (const QList<QVariant>& completion : completed)
            QVERIFY(!completion.at(2).toBool());
        QVERIFY(!settings.theme()->themeNames().contains(QStringLiteral(" name ")));
    }

    void editsBothPalettesThroughCanonicalThemeSetters()
    {
        Settings settings;
        ThemeValuesRestore restore;
        settings.theme()->setEditingPalette(QStringLiteral("dark"));
        NativeThemeManagementSource source(&settings);
        QSignalSpy completed(&source, &ThemeManagementStateSimpleSource::operationCompleted);
        QVERIFY(source.ready());
        const QVariantMap darkBefore = source.palettes().value(QStringLiteral("dark")).toMap();
        const qint64 revision = source.revision();

        const QString lightId = requestId();
        source.setPaletteColor(lightId, QStringLiteral("light"), QStringLiteral("primaryColor"),
                              QStringLiteral("#123456"), revision);
        QCOMPARE(completed.count(), 1);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(settings.theme()->editingPalette(), QStringLiteral("dark"));
        QCOMPARE(source.palettes().value(QStringLiteral("light")).toMap()
                     .value(QStringLiteral("primaryColor")).toString(), QStringLiteral("#123456"));
        QCOMPARE(source.palettes().value(QStringLiteral("dark")).toMap()
                     .value(QStringLiteral("primaryColor")).toString(),
                 darkBefore.value(QStringLiteral("primaryColor")).toString());

        const qint64 nextRevision = source.revision();
        source.setPaletteColor(requestId(), QStringLiteral("dark"), QStringLiteral("primaryColor"),
                              QStringLiteral("#654321"), revision);
        QCOMPARE(completed.count(), 2);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("conflict"));

        source.setPaletteColor(requestId(), QStringLiteral("dark"), QStringLiteral("primaryColor"),
                              QStringLiteral("not-a-color"), nextRevision);
        QCOMPARE(completed.count(), 3);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidColor"));

        source.execute(requestId(), QStringLiteral("random"), QString(), source.revision());
        QCOMPARE(completed.count(), 4);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(source.activeThemeName(), QStringLiteral("Custom"));
        QVERIFY(source.revision() > nextRevision);
    }

    void appliesSharedMachineThemeAndBackgroundSettingsWithCas()
    {
        Settings settings;
        ThemeValuesRestore restore;
        NativeThemeManagementSource source(&settings);
        QSignalSpy completed(&source, &ThemeManagementStateSimpleSource::operationCompleted);
        QVERIFY(source.ready());
        QVERIFY(source.themeNames().contains(QStringLiteral("Default Dark")));
        QVERIFY(source.backgroundPresets().size() > 0);
        QVERIFY(source.backgroundPatterns().size() > 0);
        const qint64 initialRevision = source.revision();

        source.executeSetting(requestId(), QStringLiteral("setThemeMode"),
                              {{QStringLiteral("value"), QStringLiteral("system")}},
                              initialRevision);
        QCOMPARE(completed.count(), 1);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(settings.theme()->themeMode(), QStringLiteral("system"));
        const qint64 modeRevision = source.revision();

        source.executeSetting(requestId(), QStringLiteral("setGlassChrome"),
                              {{QStringLiteral("value"), true}}, modeRevision);
        QCOMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(settings.theme()->glassChrome());

        const QString preset = source.backgroundPresets().first().toMap()
                                   .value(QStringLiteral("id")).toString();
        const QString pattern = source.backgroundPatterns().first().toMap()
                                    .value(QStringLiteral("id")).toString();
        const qint64 backgroundRevision = source.revision();
        source.executeSetting(requestId(), QStringLiteral("setBackgroundSelection"),
            {{QStringLiteral("source"), QStringLiteral("colour")},
             {QStringLiteral("presetId"), preset}, {QStringLiteral("imageId"), QString()},
             {QStringLiteral("shotAdvanced"), false}, {QStringLiteral("patternId"), pattern}},
            backgroundRevision);
        QCOMPARE(completed.count(), 3);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(settings.theme()->backgroundPreset(), preset);
        QCOMPARE(settings.theme()->backgroundPattern(), pattern);
        QCOMPARE(settings.theme()->backgroundSource(), QStringLiteral("colour"));

        source.executeSetting(requestId(), QStringLiteral("setBackgroundSelection"),
            {{QStringLiteral("source"), QStringLiteral("image")},
             {QStringLiteral("imageId"), QString(64, QLatin1Char('a'))}}, source.revision());
        QCOMPARE(completed.count(), 4);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidSetting"));

        const QString selectedPreset = settings.theme()->backgroundPreset();
        const QString selectedPattern = settings.theme()->backgroundPattern();
        const qint64 unchangedRevision = source.revision();
        source.executeSetting(requestId(), QStringLiteral("setBackgroundSelection"),
            {{QStringLiteral("source"), QByteArrayLiteral("colour")},
             {QStringLiteral("presetId"), preset}, {QStringLiteral("imageId"), QString()},
             {QStringLiteral("shotAdvanced"), false}, {QStringLiteral("patternId"), pattern}},
            unchangedRevision);
        QCOMPARE(completed.count(), 5);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidSetting"));

        source.executeSetting(requestId(), QStringLiteral("setBackgroundSelection"),
            {{QStringLiteral("source"), QStringLiteral("colour")},
             {QStringLiteral("presetId"), preset}, {QStringLiteral("imageId"), QString()},
             {QStringLiteral("shotAdvanced"), false}, {QStringLiteral("patternId"), pattern},
             {QStringLiteral("ignored"), QStringLiteral("extra field")}},
            unchangedRevision);
        QCOMPARE(completed.count(), 6);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidSetting"));

        source.executeSetting(requestId(), QStringLiteral("setBackgroundSelection"),
            {{QStringLiteral("source"), QStringLiteral("colour")},
             {QStringLiteral("presetId"), QString(129, QLatin1Char('x'))},
             {QStringLiteral("imageId"), QString()}, {QStringLiteral("shotAdvanced"), false},
             {QStringLiteral("patternId"), pattern}}, unchangedRevision);
        QCOMPARE(completed.count(), 7);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidSetting"));
        QCOMPARE(source.revision(), unchangedRevision);
        QCOMPARE(settings.theme()->backgroundPreset(), selectedPreset);
        QCOMPARE(settings.theme()->backgroundPattern(), selectedPattern);

        source.executeSetting(requestId(), QStringLiteral("setThemeMode"),
                              {{QStringLiteral("value"), QStringLiteral("light")}},
                              initialRevision);
        QCOMPARE(completed.count(), 8);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("conflict"));
    }

    void machineTabDarkAndLightPresetOperationsUseRevisionGuardedSetters()
    {
        Settings settings;
        ThemeValuesRestore restore;
        settings.setValue(QStringLiteral("theme/customColorsDark"), QByteArrayLiteral("{\"sentinel\":true}"));
        settings.setValue(QStringLiteral("theme/customColorsLight"), QByteArrayLiteral("{\"sentinel\":true}"));
        NativeThemeManagementSource source(&settings);
        QSignalSpy completed(&source, &ThemeManagementStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());
        QVERIFY(source.ready());
        QVERIFY(source.themeNames().contains(QStringLiteral("Default Light")));
        QVERIFY(source.themeNames().contains(QStringLiteral("Default Dark")));

        const qint64 initialRevision = source.revision();
        source.executeSetting(requestId(), QStringLiteral("applyDarkTheme"),
                              {{QStringLiteral("value"), QStringLiteral("Default Light")}},
                              initialRevision);
        QCOMPARE(completed.count(), 1);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(settings.theme()->darkThemeName(), QStringLiteral("Default Light"));
        QCOMPARE(QJsonDocument::fromJson(settings.value(QStringLiteral("theme/customColorsDark")).toByteArray())
                     .toVariant().toMap(), SettingsTheme::lightDefaults());
        const qint64 darkRevision = source.revision();
        QVERIFY(darkRevision > initialRevision);

        source.executeSetting(requestId(), QStringLiteral("applyLightTheme"),
                              {{QStringLiteral("value"), QStringLiteral("Default Dark")}},
                              darkRevision);
        QCOMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(settings.theme()->lightThemeName(), QStringLiteral("Default Dark"));
        QCOMPARE(QJsonDocument::fromJson(settings.value(QStringLiteral("theme/customColorsLight")).toByteArray())
                     .toVariant().toMap(), SettingsTheme::darkDefaults());
        const qint64 lightRevision = source.revision();
        QVERIFY(lightRevision > darkRevision);

        source.executeSetting(requestId(), QStringLiteral("applyDarkTheme"),
                              {{QStringLiteral("value"), QStringLiteral("Default Dark")}},
                              initialRevision);
        QCOMPARE(completed.count(), 3);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("conflict"));
        QCOMPARE(settings.theme()->darkThemeName(), QStringLiteral("Default Light"));
        QCOMPARE(source.revision(), lightRevision);
    }

    void facadeEditsThroughRemoteObjects()
    {
        Settings settings;
        ThemeValuesRestore restore;
        NativeThemeManagementSource source(&settings);
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ThemeManagementStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ThemeManagementService::setPublishedNode(&client);
        QQmlEngine engine;
        ThemeManagementService* service = ThemeManagementServiceForeign::create(&engine, &engine);
        QVERIFY(service);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &ThemeManagementService::operationCompleted);
        QVERIFY(service->palettes().contains(QStringLiteral("dark")));
        QVERIFY(service->palettes().contains(QStringLiteral("light")));
        const QString colorId = service->setPaletteColor(QStringLiteral("dark"),
            QStringLiteral("primaryColor"), QStringLiteral("#234567"), service->revision());
        QVERIFY(!colorId.isEmpty());
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), colorId);
        QVERIFY(completed.first().at(2).toBool());
    }

    void facadeCompletesPendingMutationWhenReplicaDisconnects()
    {
        PendingThemeSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ThemeManagementStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ThemeManagementService::setPublishedNode(&client);
        QQmlEngine engine;
        auto* service = ThemeManagementServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &ThemeManagementService::operationCompleted);
        const QString id = service->execute(QStringLiteral("save"),
                                             QStringLiteral("Disconnect test ") + requestId(),
                                             service->revision());
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(source.calls, 1);
        QVERIFY(service->busy());
        QVERIFY(host.disableRemoting(&source));
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(5).toString(), QStringLiteral("disconnected"));
        QVERIFY(!service->busy());
        QVERIFY(host.enableRemoting<ThemeManagementStateSourceAPI>(&source));
        QTRY_VERIFY(service->ready());
        QCOMPARE(source.calls, 1);
    }
};

QTEST_MAIN(ThemeServiceTest)
#include "tst_themeservice.moc"
