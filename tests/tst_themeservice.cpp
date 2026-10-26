#include <QtTest>

#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QNetworkAccessManager>
#include <QImage>
#include <QDir>
#include <QFile>
#include <atomic>
#include <QJsonDocument>
#include <QUuid>
#include <memory>

#include "core/settings.h"
#include "core/appsettings.h"
#include "core/personalmediapreparation.h"
#include "screensaver/screensavervideomanager.h"
#include "core/settings_theme.h"
#include "core/themeeditoroptions.h"
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
               QStringLiteral("theme/lightThemeName"), QStringLiteral("theme/mode"),
               QStringLiteral("theme/activeShader"), QStringLiteral("theme/screenEffect"),
               QStringLiteral("theme/customFontSizes"),
               QStringLiteral("theme/glassChrome"), QStringLiteral("theme/backgroundPreset"),
               QStringLiteral("theme/backgroundPattern"), QStringLiteral("theme/backgroundSource"),
               QStringLiteral("theme/backgroundShotAdvanced"), QStringLiteral("theme/backgroundImagePath"),
               QStringLiteral("theme/backgroundImageReference"),
               QStringLiteral("screensaver/enabled"), QStringLiteral("screensaver/cacheEnabled")}
    {
        for (const auto& parameter : ThemeEditorOptions::shaderParameters())
            keys.append(QStringLiteral("shader/crt/") + QString::fromLatin1(parameter.id));
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
    void observeEditor(QString, bool) override {}
    void execute(QString, QString, QString, qint64) override { ++calls; }
    void setPaletteColor(QString, QString, QString, QString, qint64) override { ++calls; }
    void executeSetting(QString, QString, QVariantMap, qint64) override { ++calls; }
};

class ThemeServiceTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void cleanup() { ThemeManagementService::setPublishedNode(nullptr); }

    void admissionNotificationCanDetachFacade_data()
    {
        QTest::addColumn<int>("method");
        for (int method = 0; method < 3; ++method) {
            QTest::newRow(qPrintable(QStringLiteral("detach-%1").arg(method))) << method;
        }
    }

    void admissionNotificationCanDetachFacade()
    {
        QFETCH(int, method);
        PendingThemeSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ThemeManagementStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        ThemeManagementService::setPublishedNode(&node);
        QQmlEngine engine;
        auto owner = std::unique_ptr<ThemeManagementService>(ThemeManagementService::create(&engine));
        QPointer<ThemeManagementService> service = owner.get();
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &ThemeManagementService::operationCompleted);
        connect(service, &ThemeManagementService::busyChanged, this, [&] {
            if (!service || !service->busy()) return;
            service->setNode(nullptr);
        });
        QString id;
        if (method == 0) id = service->execute(QStringLiteral("apply"), QStringLiteral("Default Dark"), -1);
        else if (method == 1) id = service->setPaletteColor(QStringLiteral("dark"), QStringLiteral("text"), QStringLiteral("#ffffff"), -1);
        else id = service->executeSetting(QStringLiteral("setGlassChrome"), {}, -1);
        QVERIFY(!id.isEmpty());
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QVERIFY(!completed.first().at(2).toBool());
        QVERIFY(!service->busy());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(source.calls, 0);
    }

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

    void savedThemesUsePortableOfficialImageReferences()
    {
        const bool previousTestMode = QStandardPaths::isTestModeEnabled();
        QStandardPaths::setTestModeEnabled(true);
        const auto restorePaths = qScopeGuard([previousTestMode] { QStandardPaths::setTestModeEnabled(previousTestMode); });
        Settings settings;
        ThemeValuesRestore restore;
        AppSettings seed;
        seed.setValue(QStringLiteral("screensaver/enabled"), false);
        seed.setValue(QStringLiteral("screensaver/cacheEnabled"), false);
        QTemporaryDir cache;
        QVERIFY(cache.isValid());
        QNetworkAccessManager network;
        ScreensaverVideoManager manager(&network, &settings, nullptr);
        manager.m_cacheDir = cache.path();
        manager.m_cacheIndex.clear();
        manager.m_personalCatalog.clear();
        manager.m_catalog.clear();
        QImage image(12, 12, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        const QString first = cache.filePath(QStringLiteral("first.png"));
        const QString second = cache.filePath(QStringLiteral("second.png"));
        QVERIFY(image.save(first));
        QVERIFY(image.save(second));
        const QString reference = QStringLiteral("https://decent-de1-media.s3.eu-north-1.amazonaws.com/media/espresso/test.png");
        CachedVideo media;
        media.localPath = first;
        media.catalogId = 7;
        manager.m_cacheIndex.insert(reference, media);
        NativeThemeManagementSource source(&settings, &manager);
        settings.theme()->setBackgroundImagePath(first);
        const auto appearance = settings.theme()->savedAppearance();
        QCOMPARE(appearance.value("background").toObject().value("image").toString(), reference);
        QVERIFY(!QJsonDocument(appearance).toJson().contains(cache.path().toUtf8()));

        media.localPath = second;
        manager.m_cacheIndex.clear();
        settings.theme()->clearBackground();
        settings.theme()->applySavedAppearance(appearance);
        QCOMPARE(settings.theme()->backgroundSource(), QStringLiteral("none"));
        manager.m_cacheIndex.insert(reference, media);
        emit manager.cacheUsedBytesChanged();
        QCOMPARE(settings.theme()->backgroundImagePath(), second);

        settings.theme()->setBackgroundImagePath(first);
        QCOMPARE(settings.theme()->savedAppearance().value("background").toObject().value("source").toString(),
                 QStringLiteral("none"));
    }

    void preparedMediaRefreshesOpaqueCatalogAndRevisionGuardedBackgroundSelection()
    {
        const bool previousTestMode = QStandardPaths::isTestModeEnabled();
        QStandardPaths::setTestModeEnabled(true);
        const auto restorePaths = qScopeGuard([previousTestMode] { QStandardPaths::setTestModeEnabled(previousTestMode); });
        Settings settings;
        ThemeValuesRestore restore;
        AppSettings seed;
        seed.setValue(QStringLiteral("screensaver/enabled"), false);
        seed.setValue(QStringLiteral("screensaver/cacheEnabled"), false);
        QTemporaryDir cache;
        QTemporaryDir input;
        QVERIFY(cache.isValid());
        QVERIFY(input.isValid());
        QNetworkAccessManager network;
        ScreensaverVideoManager manager(&network, &settings, nullptr);
        manager.m_cacheDir = cache.path();
        manager.m_cacheIndex.clear();
        manager.m_personalCatalog.clear();
        manager.m_catalog.clear();
        settings.theme()->clearBackground();
        NativeThemeManagementSource source(&settings, &manager);
        const qint64 before = source.revision();
        QImage image(4, 3, QImage::Format_RGB32);
        image.fill(Qt::blue);
        const QString imagePath = input.filePath(QStringLiteral("Holiday.png"));
        QVERIFY(image.save(imagePath));
        QVERIFY(QDir().mkpath(manager.personalMediaDirectory()));
        QTemporaryDir stage(QDir(manager.personalMediaDirectory()).filePath(QStringLiteral(".import-XXXXXX")));
        QVERIFY(stage.isValid());
        const auto prepared = PersonalMediaPreparation::prepare(imagePath, QStringLiteral("Holiday.png"),
            stage.path(), std::make_shared<std::atomic_bool>(false));
        QVERIFY2(prepared.reason.isEmpty(), qPrintable(prepared.reason));
        QVERIFY(manager.adoptPreparedPersonalMedia(prepared.path, QStringLiteral("Holiday.png"), prepared.mediaDate));
        QVERIFY(source.revision() > before);
        QCOMPARE(source.backgroundImages().size(), 1);
        const QVariantMap safeRow = source.backgroundImages().first().toMap();
        QVERIFY(!safeRow.contains(QStringLiteral("path")));
        const QString imageId = safeRow.value(QStringLiteral("id")).toString();
        QCOMPARE(imageId.size(), 64);
        const qint64 catalogRevision = source.revision();
        QSignalSpy completed(&source, &ThemeManagementStateSimpleSource::operationCompleted);
        source.executeSetting(requestId(), QStringLiteral("setBackgroundSelection"),
            {{QStringLiteral("source"), QStringLiteral("image")}, {QStringLiteral("presetId"), QString()},
             {QStringLiteral("imageId"), imageId}, {QStringLiteral("shotAdvanced"), false},
             {QStringLiteral("patternId"), QString()}}, catalogRevision);
        QCOMPARE(completed.count(), 1);
        QVERIFY(completed.first().at(2).toBool());
        QCOMPARE(source.backgroundImageId(), imageId);
        QCOMPARE(settings.theme()->backgroundSource(), QStringLiteral("image"));
        const qint64 selectedRevision = source.revision();
        const QString selectedPath = settings.theme()->backgroundImagePath();
        QFile selected(selectedPath);
        QVERIFY(selected.open(QIODevice::ReadOnly));
        const QByteArray originalBytes = selected.readAll();
        selected.close();
        // Existing duplicate semantics reject replacement rather than changing
        // bytes behind the same opaque image ID or cached URL.
        image.fill(Qt::red);
        const QString duplicate = input.filePath(QStringLiteral("duplicate.png"));
        QVERIFY(image.save(duplicate));
        QVERIFY(!manager.addPersonalMedia(duplicate, QStringLiteral("HOLIDAY.PNG")));
        QCOMPARE(source.revision(), selectedRevision);
        QCOMPARE(source.backgroundImageId(), imageId);
        QVERIFY(selected.open(QIODevice::ReadOnly));
        QCOMPARE(selected.readAll(), originalBytes);
        selected.close();
        source.executeSetting(requestId(), QStringLiteral("setBackgroundSelection"),
            {{QStringLiteral("source"), QStringLiteral("none")}, {QStringLiteral("presetId"), QString()},
             {QStringLiteral("imageId"), QString()}, {QStringLiteral("shotAdvanced"), false},
             {QStringLiteral("patternId"), QString()}}, catalogRevision);
        QCOMPARE(completed.count(), 2);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("conflict"));
        QCOMPARE(source.backgroundImageId(), imageId);
        const int mediaId = manager.getPersonalMediaList().first().toMap().value(QStringLiteral("id")).toInt();
        QVERIFY(manager.deletePersonalMedia(mediaId));
        QVERIFY(source.backgroundImages().isEmpty());
        QVERIFY(source.backgroundImageId().isEmpty());
        QVERIFY(settings.theme()->backgroundImagePath().isEmpty());
        QVERIFY(source.revision() > selectedRevision);
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

    void advancedControlsUseCanonicalThemeSetters()
    {
        Settings settings;
        ThemeValuesRestore restore;
        NativeThemeManagementSource source(&settings);
        QSignalSpy completed(&source, &ThemeManagementStateSimpleSource::operationCompleted);
        const QVariant originalColor = source.activeColors().value(QStringLiteral("primaryColor"));
        for (auto it = ThemeFonts::roles().cbegin(); it != ThemeFonts::roles().cend(); ++it) {
            source.executeSetting(requestId(), QStringLiteral("setFontSize"),
                {{QStringLiteral("name"), it.key()}, {QStringLiteral("value"), it.value().min}}, source.revision());
            QVERIFY(completed.last().at(2).toBool());
            QCOMPARE(source.editorSettings().value(QStringLiteral("fontSizes")).toMap().value(it.key()).toInt(), it.value().min);
        }
        const qint64 staleRevision = source.revision();
        source.executeSetting(requestId(), QStringLiteral("resetFonts"), {}, source.revision());
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(settings.theme()->customFontSizes().isEmpty());
        QCOMPARE(source.activeColors().value(QStringLiteral("primaryColor")), originalColor);
        source.executeSetting(requestId(), QStringLiteral("setFontSize"),
            {{QStringLiteral("name"), QStringLiteral("bodySize")}, {QStringLiteral("value"), 20}}, staleRevision);
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("conflict"));
        source.executeSetting(requestId(), QStringLiteral("setFontSize"),
            {{QStringLiteral("name"), QStringLiteral("bodySize")}, {QStringLiteral("value"), 1000}}, source.revision());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidSetting"));

        source.executeSetting(requestId(), QStringLiteral("setShader"),
            {{QStringLiteral("value"), QStringLiteral("crt")}}, source.revision());
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(source.editorSettings().value(QStringLiteral("shader")).toString(), QStringLiteral("crt"));
        for (const auto& parameter : ThemeEditorOptions::shaderParameters()) {
            const QString name = QString::fromLatin1(parameter.id);
            source.executeSetting(requestId(), QStringLiteral("setShaderParam"),
                {{QStringLiteral("name"), name}, {QStringLiteral("value"), parameter.maximum}}, source.revision());
            QVERIFY(completed.last().at(2).toBool());
            QCOMPARE(source.editorSettings().value(QStringLiteral("shaderParams")).toMap().value(name).toDouble(), parameter.maximum);
        }
        source.executeSetting(requestId(), QStringLiteral("resetShader"), {}, source.revision());
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(source.editorSettings().value(QStringLiteral("shaderParams")).toMap(), ThemeEditorOptions::shaderDefaults());
        const qint64 beforeFlash = source.revision();
        source.executeSetting(requestId(), QStringLiteral("flashColor"),
            {{QStringLiteral("name"), QStringLiteral("primaryColor")}}, beforeFlash);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(source.revision(), beforeFlash); // Flash must not invalidate an unfinished colour edit.
        source.executeSetting(requestId(), QStringLiteral("resetTheme"), {}, source.revision());
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(settings.theme()->customFontSizes().isEmpty());
        QCOMPARE(source.activeColors().value(QStringLiteral("primaryColor")),
            (source.darkMode() ? SettingsTheme::darkDefaults() : SettingsTheme::lightDefaults()).value(QStringLiteral("primaryColor")));
    }

    void facadeEditsThroughRemoteObjects()
    {
        Settings settings;
        ThemeValuesRestore restore;
        NativeThemeManagementSource source(&settings);
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        const QString clientId = requestId();
        client.setProperty("decenzaClientId", clientId);
        source.openClient(clientId);
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ThemeManagementStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ThemeManagementService::setPublishedNode(&client);
        QQmlEngine engine;
        ThemeManagementService* service = ThemeManagementServiceForeign::create(&engine, &engine);
        QVERIFY(service);
        QTRY_VERIFY(service->ready());
        service->setEditorVisible(QStringLiteral("theme-page"), true);
        QTRY_VERIFY(source.editorObserved());
        service->setEditorVisible(QStringLiteral("theme-overlay"), true);
        service->setEditorVisible(QStringLiteral("theme-page"), false);
        QTRY_VERIFY(source.editorObserved());
        source.closeClient(clientId);
        QTRY_VERIFY(!source.editorObserved());
        source.observeEditor(clientId, true); // Disconnected clients cannot leave tracking enabled.
        QVERIFY(!source.editorObserved());
        QSignalSpy completed(service, &ThemeManagementService::operationCompleted);
        QVERIFY(service->palettes().contains(QStringLiteral("dark")));
        QVERIFY(service->palettes().contains(QStringLiteral("light")));
        const QString colorId = service->setPaletteColor(QStringLiteral("dark"),
            QStringLiteral("primaryColor"), QStringLiteral("#234567"), service->revision());
        QVERIFY(!colorId.isEmpty());
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), colorId);
        QVERIFY(completed.first().at(2).toBool());

        QString notifiedMode = service->themeMode();
        bool notifiedGlass = service->glassChrome();
        QObject observer;
        connect(service, &ThemeManagementService::stateChanged, &observer, [&] {
            notifiedMode = service->themeMode();
            notifiedGlass = service->glassChrome();
        });
        const QString newMode = service->themeMode() == QLatin1String("system")
            ? QStringLiteral("light") : QStringLiteral("system");
        service->executeSetting(QStringLiteral("setThemeMode"),
                                {{QStringLiteral("value"), newMode}}, service->revision());
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(notifiedMode, newMode);

        const bool newGlass = !service->glassChrome();
        service->executeSetting(QStringLiteral("setGlassChrome"),
                                {{QStringLiteral("value"), newGlass}}, service->revision());
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(notifiedGlass, newGlass);
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
