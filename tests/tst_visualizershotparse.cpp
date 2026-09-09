// Regression tests for ShotFileParser::parseVisualizerShot — the JSON->ShotRecord
// path used by "Recover shots from Visualizer".
//
// The crux these lock down: visualizer.coffee's /api/shots/{id}/download
// string-encodes numeric DYE scalars (Tcl-huddle convention: "16.2", "0"), while
// a few (espresso_enjoyment) arrive as bare numbers. A naive QJsonValue::toDouble()
// returns 0 for a *string*, which would silently zero dose weight / TDS / EY on
// every recovered shot. These tests parse the real download-schema fixture
// (tests/data/shots/cremina_clean.json) and assert the scalars survive.

#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "history/shotfileparser.h"
#include "history/shothistorystorage.h"
#include "network/visualizeruploader.h"
#include "network/visualizerimporter.h"
#include "network/webdebuglogger.h"
#include "core/settings.h"
#include "helpers/controllednetwork.h"
#include "helpers/diagnosticcapture.h"
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QRegularExpression>

class TstVisualizerShotParse : public QObject
{
    Q_OBJECT

private:
    static void credentials(Settings& settings, bool present = true) {
        settings.setValue("visualizer/username", present ? "fixture-user" : "");
        settings.setValue("visualizer/password", present ? "fixture-password" : "");
    }
    static ShotProjection historyShot(qint64 id) {
        ShotProjection shot; shot.id = id; shot.durationSec = 30;
        shot.espressoNotes = "PRIVATE_SHOT_NOTES";
        return shot;
    }
    static QByteArray listPage(const QJsonArray& data, int pages) {
        return QJsonDocument(QJsonObject{{"data", data}, {"paging", QJsonObject{{"pages", pages}}}}).toJson();
    }
    static QByteArray profileBody(const QString& title) {
        Profile profile; profile.setTitle(title);
        ProfileFrame frame; frame.name = "Extraction"; profile.setSteps({frame});
        return profile.toJson().toJson();
    }
    static QString operationId(const QString& line) {
        return QRegularExpression("\\bop=([^ ]+)").match(line).captured(1);
    }

    static QJsonObject loadFixture(const QString& name)
    {
        const QString path = QStringLiteral(TST_VIS_PARSE_SOURCE_DIR)
                             + QStringLiteral("/data/shots/") + name;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        return doc.object();
    }

    // Minimal valid download body: enough to clear parseVisualizerShot's guards
    // (non-empty timeframe + pressure), plus whatever extra fields a test sets.
    static QJsonObject minimalShot(const QJsonObject& extra)
    {
        QJsonArray tf;   tf.append(0.0);  tf.append(0.5);  tf.append(1.0);
        QJsonArray pres; pres.append(1.0); pres.append(6.0); pres.append(9.0);
        QJsonObject data;
        data.insert(QStringLiteral("espresso_pressure"), pres);
        QJsonObject shot = extra;
        shot.insert(QStringLiteral("timeframe"), tf);
        shot.insert(QStringLiteral("data"), data);
        return shot;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QStringLiteral("visualizer-log-tests-%1")
                                            .arg(QCoreApplication::applicationPid()));
    }

    void cleanupTestCase()
    {
        QDir(ProfileSaveHelper::downloadedProfilesPath()).removeRecursively();
        QDir(Settings::testQSettingsPath() + ".visualizer").removeRecursively();
    }

    void uploadRetriesAndChildOutcome()
    {
        DiagnosticCapture log({"[Visualizer]"});
        ControlledNetwork net;
        Settings settings;
        credentials(settings);
        VisualizerUploader uploader(&net, &settings);
        QSignalSpy success(&uploader, &VisualizerUploader::uploadSucceededForShot);
        QSignalSpy failed(&uploader, &VisualizerUploader::uploadFailed);
        uploader.uploadShotFromHistory(historyShot(41));
        QCOMPARE(net.requests.size(), 1);
        net.complete(0, 503, R"({"error":"PRIVATE_SERVER_BODY"})", QNetworkReply::ServiceUnavailableError);
        QCOMPARE(net.requests.size(), 2);
        QVERIFY(log.terminals().isEmpty());
        net.complete(1, 0, {}, QNetworkReply::TimeoutError);
        QCOMPARE(net.requests.size(), 3);
        net.complete(2, 200, R"({"id":"cloud-41"})");
        QCOMPARE(success.size(), 1);
        QCOMPARE(success.front()[0].toLongLong(), 41);
        QCOMPARE(failed.size(), 0);
        const auto terminal = log.terminals().filter("kind=upload");
        QCOMPARE(terminal.size(), 1);
        QVERIFY(terminal.front().contains("outcome=success"));
        QVERIFY(terminal.front().contains("shotId=41"));
        QVERIFY(terminal.front().contains("retry=2"));
        const auto child = log.terminals().filter("kind=coffeeSync");
        QCOMPARE(child.size(), 1);
        QVERIFY(child.front().contains("outcome=skipped"));
        QVERIFY(child.front().contains("parentOp=" + operationId(terminal.front())));
        QCOMPARE(log.levels().count(QtWarningMsg), 0);
        QVERIFY(!log.lines().join('\n').contains("PRIVATE_SERVER_BODY"));
        // Multipart boundaries are fresh; the JSON payload inside stays identical.
        for (const auto& request : net.requests)
            QVERIFY(request.body.contains("PRIVATE_SHOT_NOTES"));
        QFile artifact(Settings::testQSettingsPath() + ".visualizer/last_upload.json");
        QVERIFY(artifact.open(QIODevice::ReadOnly));
        QVERIFY(artifact.readAll().contains("PRIVATE_SHOT_NOTES"));
    }

    void uploadFailures_data()
    {
        QTest::addColumn<int>("status"); QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("attempts"); QTest::addColumn<QString>("reason");
        QTest::newRow("exhausted") << 503 << QByteArray("PRIVATE_SERVER_BODY") << 3 << QString("requestFailed");
        QTest::newRow("credentials") << 401 << QByteArray("PRIVATE_SERVER_BODY") << 1 << QString("requestFailed");
        QTest::newRow("validation") << 422 << QByteArray(R"({"error":"PRIVATE_SERVER_BODY"})") << 1 << QString("requestFailed");
        QTest::newRow("rate-limit") << 429 << QByteArray("PRIVATE_SERVER_BODY") << 1 << QString("requestFailed");
        QTest::newRow("missing-id") << 200 << QByteArray("{}") << 1 << QString("missingReturnedShotId");
        QTest::newRow("malformed") << 200 << QByteArray("PRIVATE_SERVER_BODY") << 1 << QString("missingReturnedShotId");
    }
    void uploadFailures()
    {
        QFETCH(int, status); QFETCH(QByteArray, body); QFETCH(int, attempts); QFETCH(QString, reason);
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerUploader uploader(&net, &settings);
        QSignalSpy failure(&uploader, &VisualizerUploader::uploadFailed);
        uploader.uploadShotFromHistory(historyShot(42));
        for (int i = 0; i < attempts; ++i) {
            QCOMPARE(net.requests.size(), i + 1);
            net.complete(i, status, body, status == 200 ? QNetworkReply::NoError : QNetworkReply::UnknownServerError,
                         "PRIVATE_TRANSPORT_TEXT");
        }
        QCOMPARE(net.requests.size(), attempts);
        QCOMPARE(failure.size(), 1);
        QCOMPARE(log.terminals().size(), 1);
        QVERIFY(log.terminals().front().contains("outcome=failed"));
        QVERIFY(log.terminals().front().contains("reason=" + reason));
        QCOMPARE(log.levels().count(QtWarningMsg), 1);
        QVERIFY(!log.lines().join('\n').contains("PRIVATE_"));
        if (status == 422) QCOMPARE(failure.front()[0].toString(), QString("PRIVATE_SERVER_BODY"));
    }

    void preflightAndConnection()
    {
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings;
        credentials(settings, false);
        VisualizerUploader uploader(&net, &settings);
        QSignalSpy tested(&uploader, &VisualizerUploader::connectionTestResult);
        uploader.testConnection();
        uploader.uploadShotFromHistory(historyShot(43));
        uploader.fetchShotListSince(1);
        QCOMPARE(net.requests.size(), 0);
        QCOMPARE(log.terminals().filter("reason=missingCredentials").size(), 3);
        credentials(settings);
        auto shortShot = historyShot(43); shortShot.durationSec = 1;
        uploader.uploadShotFromHistory(shortShot);
        QVERIFY(log.terminals().last().contains("outcome=skipped"));
        uploader.testConnection();
        net.complete(0, 401, "PRIVATE_SERVER_BODY", QNetworkReply::AuthenticationRequiredError);
        QVERIFY(log.terminals().last().contains("outcome=failed"));
        uploader.testConnection();
        net.complete(1, 200, "[]");
        QCOMPARE(tested.size(), 3);
        QVERIFY(tested.last()[0].toBool());
        QVERIFY(log.terminals().last().contains("outcome=success"));
    }

    void listOutcomes_data()
    {
        QTest::addColumn<QByteArray>("body"); QTest::addColumn<QString>("outcome");
        QTest::newRow("malformed") << QByteArray("PRIVATE_SERVER_BODY") << QString("failed");
        QTest::newRow("missing-paging") << QByteArray("{}") << QString("failed");
        QTest::newRow("empty") << listPage({}, 1) << QString("empty");
        QTest::newRow("one-shot") << listPage({QJsonObject{{"id", "cloud-1"}, {"clock", 20}}}, 1) << QString("success");
    }
    void listOutcomes()
    {
        QFETCH(QByteArray, body); QFETCH(QString, outcome);
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerUploader uploader(&net, &settings);
        QSignalSpy success(&uploader, &VisualizerUploader::shotListFetched);
        QSignalSpy failed(&uploader, &VisualizerUploader::shotListFailed);
        uploader.fetchShotListSince(1);
        net.complete(0, 200, body);
        QCOMPARE(net.requests.size(), 1);
        QCOMPARE(log.terminals().size(), 1);
        QVERIFY(log.lines().filter(" response").front().contains("page=1"));
        QVERIFY(log.terminals().front().contains("outcome=" + outcome));
        QCOMPARE(failed.size(), outcome == "failed" ? 1 : 0);
        QCOMPARE(success.size(), outcome == "failed" ? 0 : 1);
    }

    void overlappingListsAndPageCeiling()
    {
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerUploader uploader(&net, &settings);
        uploader.fetchShotListSince(1);
        uploader.fetchShotListSince(2);
        net.complete(1, 200, listPage({}, 1));
        const QString completed = operationId(log.terminals().front());
        for (int i = 0; i < 50; ++i) {
            const qsizetype index = i == 0 ? 0 : i + 1;
            net.complete(index, 200, listPage({}, 51));
        }
        QCOMPARE(net.requests.size(), 51);
        QCOMPARE(log.terminals().size(), 2);
        QVERIFY(log.terminals().last().contains("reason=pageLimit"));
        QVERIFY(operationId(log.terminals().last()) != completed);
    }

    void overlappingUpdateOutcomes()
    {
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerUploader uploader(&net, &settings);
        QSignalSpy failed(&uploader, &VisualizerUploader::updateFailed);
        QSignalSpy succeeded(&uploader, &VisualizerUploader::updateSuccess);
        uploader.updateShotOnVisualizer("cloud-51", historyShot(51));
        uploader.updateShotOnVisualizer("cloud-52", historyShot(52));
        // Metadata updates already permit overlap. Complete them in reverse order.
        QCOMPARE(net.requests.size(), 2);
        net.complete(1, 200, "{}");
        QCOMPARE(succeeded.size(), 1);
        QVERIFY(log.terminals().front().contains("shotId=52"));
        net.complete(0, 404, "PRIVATE_SERVER_BODY", QNetworkReply::ContentNotFoundError);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.front()[0].toString(), QString("cloud-51"));
        QVERIFY(failed.front()[1].toBool());
        QCOMPARE(log.terminals().size(), 2);
        QVERIFY(log.terminals().last().contains("shotId=51"));
        QVERIFY(operationId(log.terminals().first()) != operationId(log.terminals().last()));
    }

    void importFailureAndShareCodePrivacy()
    {
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerImporter importer(&net, nullptr, &settings);
        QSignalSpy failed(&importer, &VisualizerImporter::importFailed);
        importer.importFromShareCode("S3CR");
        importer.importFromShotId("busy-shot");
        QCOMPARE(log.terminals().filter("reason=busy").size(), 1);
        net.complete(0, 200, R"([{"id":"shared-shot"}])");
        QCOMPARE(net.requests.size(), 2);
        net.complete(1, 200, "PRIVATE_SERVER_BODY");
        QCOMPARE(failed.size(), 1);
        QCOMPARE(log.terminals().size(), 2);
        QVERIFY(log.terminals().last().contains("reason=invalidResponse"));
        QVERIFY(log.terminals().last().contains("remoteId=shared-shot"));
        QVERIFY(!log.lines().join('\n').contains("S3CR"));
        QVERIFY(!log.lines().join('\n').contains("PRIVATE_SERVER_BODY"));
        importer.importFromShotId("invalid-profile");
        net.complete(2, 200, R"({"title":"Empty","steps":[]})");
        QVERIFY(log.terminals().last().contains("reason=invalidProfile"));
        importer.importFromShotId("unavailable");
        net.complete(3, 503, "PRIVATE_SERVER_BODY", QNetworkReply::ServiceUnavailableError, "PRIVATE_TRANSPORT_TEXT");
        QVERIFY(log.terminals().last().contains("reason=requestFailed"));
    }

    void importDuplicateDecision_data()
    {
        QTest::addColumn<QString>("action");
        for (const auto* name : {"overwrite", "rename", "saveAsNew", "cancel"}) QTest::newRow(name) << QString(name);
    }
    void importDuplicateDecision()
    {
        QFETCH(QString, action);
        DiagnosticCapture log({"[Visualizer]", "[Profiles][ProfileSaveHelper]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerImporter importer(&net, nullptr, &settings);
        const QString title = "Log Test " + action;
        const QByteArray profile = profileBody(title);
        QSignalSpy saved(&importer, &VisualizerImporter::importSuccess);
        QSignalSpy duplicate(&importer, &VisualizerImporter::duplicateFound);
        importer.importFromShotId("first"); net.complete(0, 200, profile);
        QCOMPARE(saved.size(), 1);
        net.complete(1, 200, "[]"); // Existing post-save shared-list refresh.
        importer.importFromShotId("duplicate"); net.complete(2, 200, profile);
        QCOMPARE(duplicate.size(), 1);
        QCOMPARE(log.terminals().filter("kind=profileImport").size(), 1); // The duplicate remains pending.
        if (action == "overwrite") importer.saveOverwrite();
        else if (action == "rename") importer.saveWithNewName(title + " Renamed");
        else if (action == "saveAsNew") importer.saveAsNew();
        else importer.cancelPending();
        QCOMPARE(log.terminals().filter("kind=profileImport").size(), 2);
        QVERIFY(log.terminals().filter("kind=profileImport").last().contains("remoteId=duplicate"));
        QVERIFY(log.terminals().filter("kind=profileImport").last().contains(action == "cancel" ? "outcome=cancelled" : "outcome=success"));
        QCOMPARE(saved.size(), action == "cancel" ? 1 : 2);
    }

    void batchPartialPreservesUiCounters()
    {
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerImporter importer(&net, nullptr, &settings);
        QSignalSpy completed(&importer, &VisualizerImporter::batchImportComplete);
        importer.importSelectedShots({"offline", "broken"}, false);
        net.complete(0, 503, "PRIVATE_SERVER_BODY", QNetworkReply::ServiceUnavailableError);
        net.complete(1, 200, R"({"steps":[]})");
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.front(), QVariantList({0, 2, 0}));
        QCOMPARE(log.terminals().size(), 1);
        QVERIFY(log.terminals().front().contains("outcome=partial"));
        QVERIFY(log.terminals().front().contains("diagnosticFailures=2"));
        QVERIFY(log.terminals().front().contains("skipped=2"));
    }

    void sharedDetailsOutOfOrder()
    {
        // With no application controller, only the existing profile-status lookup
        // warns; capture that owner explicitly while driving the real callbacks.
        DiagnosticCapture log({"[Visualizer]", "[Profiles]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerImporter importer(&net, nullptr, &settings);
        QSignalSpy completed(&importer, &VisualizerImporter::sharedShotsChanged);
        importer.fetchSharedShots();
        net.complete(0, 200, R"([{"id":"a","profile_title":"A"},{"id":"b","profile_title":"B"}])");
        QCOMPARE(net.requests.size(), 3);
        net.complete(2, 503, {}, QNetworkReply::ServiceUnavailableError);
        QVERIFY(log.terminals().isEmpty());
        net.complete(1, 200, profileBody("Shared test"));
        QCOMPARE(completed.size(), 1);
        QCOMPARE(importer.sharedShots().size(), 2);
        QCOMPARE(log.terminals().size(), 1);
        QVERIFY(log.terminals().front().contains("outcome=partial"));
        QVERIFY(log.terminals().front().contains("failed=1"));
    }

    void recoveryRetryParseAndDatabase()
    {
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        QTemporaryDir tmp;
        ShotHistoryStorage history;
        QVERIFY(history.initialize(tmp.filePath("shots.db")));
        VisualizerImporter importer(&net, nullptr, &settings);
        importer.m_testHistory = &history;
        QSignalSpy completed(&importer, &VisualizerImporter::recoveryComplete);
        const QJsonArray entries{QJsonObject{{"id", "good"}, {"clock", 200}},
                                 QJsonObject{{"id", "bad"}, {"clock", 210}}};
        importer.recoverShots(100, 300);
        net.complete(0, 200, listPage(entries, 1));
        QCOMPARE(net.requests.size(), 2);
        net.complete(1, 503, {}, QNetworkReply::ServiceUnavailableError);
        net.complete(2, 200, QJsonDocument(minimalShot({})).toJson());
        net.complete(3, 200, profileBody("Recovered test"));
        QTRY_COMPARE(net.requests.size(), 5);
        net.complete(4, 200, "PRIVATE_SERVER_BODY");
        net.complete(5, 503, {}, QNetworkReply::ServiceUnavailableError);
        QTRY_COMPARE(completed.size(), 1);
        const auto badItem = log.lines().filter("remoteId=bad");
        QVERIFY(!badItem.isEmpty());
        for (const auto& line : badItem) {
            QVERIFY(line.contains("shotId=0"));
            QVERIFY(line.contains("retry=0"));
        }
        QCOMPARE(completed.front(), QVariantList({2, 1, 0, 1}));
        QCOMPARE(log.terminals().size(), 1);
        QVERIFY(log.terminals().front().contains("outcome=partial"));
        QVERIFY(log.terminals().front().contains("imported=1"));
        QVERIFY(log.terminals().front().contains("failed=1"));
        QTRY_VERIFY(history.isDbWorkIdle());
        // The successful row is genuinely persisted; a second recovery skips it.
        importer.recoverShots(100, 300);
        net.complete(6, 200, listPage({entries[0]}, 1));
        net.complete(7, 200, QJsonDocument(minimalShot({})).toJson());
        net.complete(8, 200, profileBody("Recovered test"));
        QTRY_COMPARE(completed.size(), 2);
        QCOMPARE(completed.last(), QVariantList({1, 0, 1, 0}));
        QVERIFY(log.terminals().last().contains("outcome=success"));
        QTRY_VERIFY(history.isDbWorkIdle());
    }

    void coffeeOutcomes_data()
    {
        QTest::addColumn<int>("status"); QTest::addColumn<QString>("outcome");
        QTest::newRow("accepted") << 200 << QString("success");
        QTest::newRow("capability") << 403 << QString("skipped");
        QTest::newRow("gone") << 404 << QString("skipped");
        QTest::newRow("validation") << 422 << QString("rejected");
        QTest::newRow("retry") << 503 << QString("failed");
    }
    void coffeeOutcomes()
    {
        QFETCH(int, status); QFETCH(QString, outcome);
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerUploader uploader(&net, &settings);
        const auto upload = VisualizerOperationLog::begin(VisualizerOperationLog::Emitter::Uploader, "upload", 61);
        upload->finish("success", "uploaded");
        const auto op = VisualizerOperationLog::begin(VisualizerOperationLog::Emitter::Uploader, "bagUpdate", 61, 7, {}, upload);
        QSignalSpy rejected(&uploader, &VisualizerUploader::bagPushRejected);
        uploader.patchRemoteBag({{"id", 7}, {"visualizerBagId", "remote-bag"},
                                 {"name", "PRIVATE_BAG_NAME"}, {"notes", "PRIVATE_NOTES"}}, {}, op);
        net.complete(0, status, R"({"error":"PRIVATE_SERVER_BODY"})",
                     status == 200 ? QNetworkReply::NoError : QNetworkReply::UnknownServerError);
        QCOMPARE(log.terminals().size(), 2);
        QVERIFY(log.terminals().front().contains("outcome=success"));
        QVERIFY(log.terminals().last().contains("outcome=" + outcome));
        QVERIFY(log.terminals().last().contains("parentOp=" + upload->id));
        QCOMPARE(rejected.size(), status == 422 ? 1 : 0);
        if (status == 422) QCOMPARE(rejected.front()[2].toString(), QString("PRIVATE_SERVER_BODY"));
        QVERIFY(!log.lines().join('\n').contains("PRIVATE_"));
    }

    void coffeeReadBackLinkAndCapability()
    {
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerUploader uploader(&net, &settings);
        const QVariantMap bag{{"id", 7}, {"beanBaseId", "canonical"}, {"notes", "PRIVATE_NOTES"}};
        auto canonical = VisualizerOperationLog::begin(VisualizerOperationLog::Emitter::Uploader, "coffeeSync", 71, 7, "cloud-71");
        uploader.reconcileShotBag("cloud-71", bag, canonical);
        net.complete(0, 200, "{}");
        QCOMPARE(net.requests.size(), 2);
        QVERIFY(net.requests[1].body.contains("canonical_coffee_bag_id"));
        net.complete(1, 200, "{}");
        QVERIFY(log.terminals().last().contains("reason=canonicalLinked"));

        auto enrich = VisualizerOperationLog::begin(VisualizerOperationLog::Emitter::Uploader, "coffeeSync", 72, 7, "cloud-72");
        uploader.reconcileShotBag("cloud-72", bag, enrich);
        net.complete(2, 200, R"({"coffee_bag_id":"server-bag"})");
        net.complete(3, 200, "{}");
        QCOMPARE(net.requests.size(), 5);
        net.complete(4, 403, "PRIVATE_SERVER_BODY", QNetworkReply::ContentAccessDenied);
        QCOMPARE(log.terminals().size(), 2);
        QVERIFY(log.terminals().last().contains("remoteBagId=server-bag"));
        QVERIFY(log.terminals().last().contains("reason=capabilityUnavailable"));
        QCOMPARE(uploader.cmState(), VisualizerUploader::CmState::NoCoffeeManagement);
        QVERIFY(!log.lines().join('\n').contains("PRIVATE_"));
    }

    void roasterResolutionFailureRemainsPartial()
    {
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerUploader uploader(&net, &settings);
        auto op = VisualizerOperationLog::begin(VisualizerOperationLog::Emitter::Uploader, "bagUpdate", 0, 8);
        QString resolvedId = "not-called";
        uploader.resolveRoasterId("Fixture roaster", {}, [&](const QString& id) {
            resolvedId = id;
            uploader.patchRemoteBag({{"id", 8}, {"visualizerBagId", "remote-bag"}}, id, op);
        }, op);
        net.complete(0, 200, R"({"data":[]})");
        net.complete(1, 201, "{}");
        QCOMPARE(resolvedId, QString()); // Existing fallback still dispatches the bag PATCH.
        QCOMPARE(net.requests.size(), 3);
        net.complete(2, 200, "{}");
        QCOMPARE(log.terminals().size(), 1);
        QVERIFY(log.terminals().last().contains("outcome=partial"));
        QVERIFY(log.lines().join('\n').contains("reason=missingRoasterId"));
        uploader.enrichRemoteRoaster("roaster", "canonical", op);
        net.complete(3, 200, R"({"canonical_roaster_id":null})");
        net.complete(4, 503, "PRIVATE_SERVER_BODY", QNetworkReply::ServiceUnavailableError);
        QCOMPARE(log.terminals().size(), 2);
        QVERIFY(log.terminals().last().contains("kind=roasterEnrich"));
        QVERIFY(log.terminals().last().contains("parentOp=" + op->id));
        QVERIFY(log.terminals().last().contains("outcome=failed"));
    }

    void beanRepairKeepsPendingOnAccountFailure()
    {
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerUploader uploader(&net, &settings);
        QSignalSpy complete(&uploader, &VisualizerUploader::beanRepairFinished);
        QSignalSpy settled(&uploader, &VisualizerUploader::beanRepairSettled);
        BeanRepair repair; repair.shotId = 81; repair.visualizerId = "cloud-81";
        uploader.repairShotBeans({repair});
        uploader.repairShotBeans({repair});
        QVERIFY(uploader.beanRepairMissedWork());
        QCOMPARE(net.requests.size(), 1);
        net.complete(0, 429, "PRIVATE_SERVER_BODY", QNetworkReply::UnknownContentError);
        QTRY_COMPARE_WITH_TIMEOUT(complete.size(), 1, 6000);
        QCOMPARE(net.requests.size(), 1);
        QCOMPARE(settled.size(), 0);
        QCOMPARE(complete.front(), QVariantList({0, false}));
        QCOMPARE(log.terminals().size(), 1);
        QVERIFY(log.terminals().front().contains("outcome=partial"));
        QVERIFY(log.terminals().front().contains("reason=pendingRetry"));
        QVERIFY(!log.lines().join('\n').contains("PRIVATE_SERVER_BODY"));
    }

    void urlSummaryDropsCredentialsAndQuery()
    {
        DiagnosticCapture log({"[Visualizer]"});
        auto op = VisualizerOperationLog::begin(VisualizerOperationLog::Emitter::Importer, "profileImport");
        ControlledReply reply(QNetworkRequest(QUrl("https://PRIVATE_USER:PRIVATE_PASSWORD@example.test/profile?code=PRIVATE_CODE#PRIVATE_FRAGMENT")), nullptr);
        reply.complete(503, "PRIVATE_BODY", QNetworkReply::UnknownServerError, "PRIVATE_ERROR");
        op->response("fetch", &reply); op->finish("failed", "requestFailed");
        QVERIFY(log.terminals().front().contains("url=https://example.test/profile"));
        QVERIFY(!log.lines().join('\n').contains("PRIVATE_"));
    }

    void cancellationAndLateDiagnostics()
    {
        DiagnosticCapture log({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        {
            VisualizerImporter importer(&net, nullptr, &settings);
            importer.importFromShotId("cancelled-fetch");
        }
        QCOMPARE(log.terminals().size(), 1);
        QVERIFY(log.terminals().front().contains("outcome=cancelled"));
        net.complete(0, 200, profileBody("Never imported"));
        QCOMPARE(log.terminals().size(), 1);
        auto op = VisualizerOperationLog::begin(VisualizerOperationLog::Emitter::Uploader, "test");
        op->set("remoteId", QString(400, 'x') + "\nforged");
        op->finish("success", "done");
        const qsizetype count = log.lines().size();
        op->response("late", net.requests[0].reply);
        op->problem("late", "failed"); op->finish("failed", "late");
        QCOMPARE(log.lines().size(), count);
        QVERIFY(log.terminals().last().size() < 400);
        QVERIFY(!log.terminals().last().contains('\n'));
    }

    void persistedVisualizerFilters()
    {
        DiagnosticCapture capture({"[Visualizer]"}); ControlledNetwork net; Settings settings; credentials(settings);
        VisualizerUploader uploader(&net, &settings);
        uploader.testConnection(); net.complete(0, 503, "PRIVATE_SERVER_BODY", QNetworkReply::ServiceUnavailableError);
        uploader.testConnection(); net.complete(1, 200, "[]");
        uploader.fetchShotListSince(1); net.complete(2, 200, listPage({}, 1));
        const QString filePath = QDir::tempPath() + "/decenza-visualizer-validation.log";
        QFile seeded(filePath);
        QVERIFY(seeded.open(QIODevice::WriteOnly | QIODevice::Text));
        seeded.write("[   0.001] WARN  [Visualizer] historical tagged entry\n"
                     "[   0.002] WARN  Visualizer: historical unformatted entry\n"); seeded.close();
        WebDebugLogger logger(filePath);
        const auto lines = capture.lines(); const auto levels = capture.levels();
        for (qsizetype i = 0; i < lines.size(); ++i) logger.handleMessage(levels[i], lines[i]);
        const auto all = logger.sessionLinesMatching({"[Visualizer]"}, "DEBUG");
        const auto info = logger.sessionLinesMatching({"[Visualizer]"}, "INFO");
        const auto warn = logger.sessionLinesMatching({"[Visualizer]"}, "WARN");
        QCOMPARE(all.size(), lines.size() + 1);
        QCOMPARE(info.size(), levels.count(QtInfoMsg) + levels.count(QtWarningMsg) + 1);
        QCOMPARE(warn.size(), 2); // One old-format entry and one current failure.
        QVERIFY(warn.front().contains("historical tagged entry"));
        QCOMPARE(capture.terminals().size(), 3);
        QSet<QString> ids;
        for (const auto& line : capture.terminals()) ids.insert(operationId(line));
        QCOMPARE(ids.size(), 3);
        QFile persisted(filePath); QVERIFY(persisted.open(QIODevice::ReadOnly));
        const QByteArray complete = persisted.readAll();
        QVERIFY(complete.contains("historical unformatted entry"));
        QVERIFY(!all.join('\n').contains("historical unformatted entry"));
        QVERIFY(!complete.contains("PRIVATE_SERVER_BODY"));
        QVERIFY(!complete.contains("[Runtime]"));
        QVERIFY(complete.size() < 10000);
    }

    // The download string-encodes scalars — they must NOT come back as 0.
    void scalars_survive_string_encoding()
    {
        const QJsonObject shot = loadFixture(QStringLiteral("cremina_clean.json"));
        QVERIFY2(!shot.isEmpty(), "cremina_clean.json fixture missing/unreadable");

        // Sanity: confirm the fixture really is string-encoded (guards against a
        // future fixture swap silently defeating the regression).
        QVERIFY2(shot.value("bean_weight").isString(),
                 "fixture no longer string-encodes bean_weight — test is moot");

        const ShotFileParser::ParseResult res = ShotFileParser::parseVisualizerShot(
            shot, QString(), QStringLiteral("test-vis-id"), 1751000000);

        QVERIFY2(res.success, qPrintable(res.errorMessage));

        // bean_weight="16.2", drink_weight="34.8" — string-encoded, must parse.
        QVERIFY2(qAbs(res.record.summary.doseWeight - 16.2) < 0.01,
                 qPrintable(QStringLiteral("doseWeight zeroed/wrong: %1")
                            .arg(res.record.summary.doseWeight)));
        QVERIFY2(qAbs(res.record.summary.finalWeight - 34.8) < 0.01,
                 qPrintable(QStringLiteral("finalWeight zeroed/wrong: %1")
                            .arg(res.record.summary.finalWeight)));
        // duration=32.515 (bare number in this fixture) — bidirectional read.
        QVERIFY2(qAbs(res.record.summary.duration - 32.515) < 0.01,
                 qPrintable(QStringLiteral("duration wrong: %1")
                            .arg(res.record.summary.duration)));
        // espresso_enjoyment=69 (bare int) — must not be lost by the string branch.
        QCOMPARE(res.record.summary.enjoyment, 69);

        // Telemetry present and aligned.
        QVERIFY2(!res.record.pressure.isEmpty(), "pressure series empty");
        QVERIFY2(!res.record.flow.isEmpty(), "flow series empty");

        // Water dispensed is scaled x10 (tenths-of-ml on the wire -> real ml in
        // the DB). The fixture's series peaks at raw 8.1685, so the recovered
        // peak must be ~81.7 ml (not 8.2, and not double-scaled).
        double maxWater = 0;
        for (const auto& pt : res.record.waterDispensed)
            if (pt.y() > maxWater) maxWater = pt.y();
        QVERIFY2(qAbs(maxWater - 81.685) < 0.1,
                 qPrintable(QStringLiteral("water-dispensed x10 wrong: %1").arg(maxWater)));

        // Frame detection: a frame-0 boundary at extraction start, then one
        // marker per sign change (the fixture flips sign 4 times) → 5 markers
        // numbered 0..4, each with a non-empty label.
        QCOMPARE(res.record.phases.size(), qsizetype(5));
        // A frameNumber==0 marker MUST lead: ShotAnalysis::detectSkipFirstFrame
        // keys off it, else recovered shots are spuriously badged "skip first
        // frame".
        QCOMPARE(res.record.phases.first().frameNumber, 0);
        for (qsizetype i = 0; i < res.record.phases.size(); ++i) {
            QCOMPARE(res.record.phases[i].frameNumber, int(i));
            // shot_phases.label is NOT NULL — a null/empty label would make the
            // phase INSERT fail and silently drop every frame line.
            QVERIFY2(!res.record.phases[i].label.isEmpty(),
                     qPrintable(QStringLiteral("phase %1 has empty label").arg(i)));
        }
        // The leading 0.0 samples must not spawn a spurious extra boundary: every
        // transition marker (after frame 0) advances well past t≈0.
        for (qsizetype i = 1; i < res.record.phases.size(); ++i)
            QVERIFY2(res.record.phases[i].time > 0.1,
                     qPrintable(QStringLiteral("transition marker %1 at t=%2")
                                .arg(i).arg(res.record.phases[i].time)));
    }

    // Taste taps round-trip: the uploader maps tasteBalance/tasteBody to the CVA
    // attributes acidity/bitterness/mouthfeel; recovery must map them back so a
    // device-swap recovery keeps the shot's taste dial-in.
    void taste_axes_round_trip_from_cva()
    {
        // sour = (acidity 12, bitterness 4); heavy = (mouthfeel 12) — the values
        // the uploader writes for those taps.
        const QJsonObject sour = minimalShot({
            {QStringLiteral("acidity"), 12},
            {QStringLiteral("bitterness"), 4},
            {QStringLiteral("mouthfeel"), 12},
        });
        const ShotFileParser::ParseResult r1 = ShotFileParser::parseVisualizerShot(
            sour, QString(), QStringLiteral("t-sour"), 1751000000);
        QVERIFY2(r1.success, qPrintable(r1.errorMessage));
        QCOMPARE(r1.record.tasteBalance, QStringLiteral("sour"));
        QCOMPARE(r1.record.tasteBody, QStringLiteral("heavy"));

        // bitter = (acidity 4, bitterness 12); thin = (mouthfeel 4).
        const QJsonObject bitter = minimalShot({
            {QStringLiteral("acidity"), 4},
            {QStringLiteral("bitterness"), 12},
            {QStringLiteral("mouthfeel"), 4},
        });
        const ShotFileParser::ParseResult r2 = ShotFileParser::parseVisualizerShot(
            bitter, QString(), QStringLiteral("t-bitter"), 1751000000);
        QCOMPARE(r2.record.tasteBalance, QStringLiteral("bitter"));
        QCOMPARE(r2.record.tasteBody, QStringLiteral("thin"));
    }

    // An untapped / hand-unscored shot (all CVA attrs 0, or absent) must NOT
    // invent a taste value — the taps stay empty so upload doesn't clobber a
    // Visualizer-scored assessment.
    void taste_axes_unset_when_cva_absent()
    {
        const ShotFileParser::ParseResult r = ShotFileParser::parseVisualizerShot(
            minimalShot({}), QString(), QStringLiteral("t-none"), 1751000000);
        QVERIFY2(r.success, qPrintable(r.errorMessage));
        QVERIFY2(r.record.tasteBalance.isEmpty(),
                 qPrintable(QStringLiteral("tasteBalance invented: %1").arg(r.record.tasteBalance)));
        QVERIFY2(r.record.tasteBody.isEmpty(),
                 qPrintable(QStringLiteral("tasteBody invented: %1").arg(r.record.tasteBody)));
    }

    // A shot with a timeframe but no pressure telemetry must FAIL, not import a
    // hollow record that would still be counted as "imported".
    void missing_pressure_fails_not_hollow()
    {
        QJsonObject shot;
        QJsonArray tf; tf.append(QStringLiteral("0.0")); tf.append(QStringLiteral("0.5"));
        shot.insert(QStringLiteral("timeframe"), tf);
        shot.insert(QStringLiteral("data"), QJsonObject{});  // no espresso_pressure
        shot.insert(QStringLiteral("profile_title"), QStringLiteral("X"));

        const ShotFileParser::ParseResult res = ShotFileParser::parseVisualizerShot(
            shot, QString(), QStringLiteral("test-empty"), 1751000000);

        QVERIFY2(!res.success, "hollow shot (no pressure) was accepted");
        QVERIFY(res.errorMessage.contains(QStringLiteral("pressure"), Qt::CaseInsensitive));
    }
};

QTEST_GUILESS_MAIN(TstVisualizerShotParse)
#include "tst_visualizershotparse.moc"
