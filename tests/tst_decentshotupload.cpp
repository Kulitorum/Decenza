// Decent account link and shot upload (add-decent-shot-upload): the ShotRecord
// payload, the response classes, the login_test exchange, and the uploader
// against a real shot database with canned server replies.

#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include "core/dbutils.h"
#include "core/settings_decent.h"
#include "history/shothistorystorage.h"
#include "network/decentaccount.h"
#include "network/decentshotrecord.h"
#include "network/decentshotuploader.h"
#include "network/shotpayloadhelpers.h"

namespace {

struct Canned {
    int status = 200;           // 0 = transport failure
    QByteArray body;
};

class CannedReply : public QNetworkReply {
public:
    CannedReply(QNetworkAccessManager::Operation op, const QNetworkRequest& request, const Canned& canned,
                QObject* parent)
        : QNetworkReply(parent), m_body(canned.body)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(op);
        open(QIODevice::ReadOnly);
        if (canned.status > 0) {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, canned.status);
            // Real QNAM sets an error for every status >= 400 as well
            // (qhttpthreaddelegate.cpp:539-544), so code must not read error() alone.
            if (canned.status >= 400)
                setError(canned.status >= 500 ? QNetworkReply::InternalServerError
                                              : QNetworkReply::ProtocolInvalidOperationError,
                         QStringLiteral("HTTP %1").arg(canned.status));
        } else {
            setError(QNetworkReply::HostNotFoundError, QStringLiteral("host not found"));
        }
        QTimer::singleShot(0, this, [this]() {
            setFinished(true);
            emit finished();
        });
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return m_body.size() - m_pos + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char* data, qint64 maxSize) override {
        const qint64 n = qMin(maxSize, qint64(m_body.size()) - m_pos);
        memcpy(data, m_body.constData() + m_pos, size_t(n));
        m_pos += n;
        return n;
    }

private:
    QByteArray m_body;
    qint64 m_pos = 0;
};

// Answers each request with the next canned reply (the last one repeats) and
// records what was sent.
class CannedNam : public QNetworkAccessManager {
public:
    QList<Canned> replies;
    QList<QNetworkRequest> requests;
    QList<QByteArray> bodies;

protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request, QIODevice* outgoing) override {
        requests.append(request);
        bodies.append(outgoing ? outgoing->readAll() : QByteArray());
        const Canned c = replies.size() > 1 ? replies.takeFirst() : replies.value(0);
        return new CannedReply(op, request, c, this);
    }
};

QVariantList series(std::initializer_list<QPointF> points) {
    QVariantList out;
    for (const QPointF& p : points) out.append(QVariantMap{{"x", p.x()}, {"y", p.y()}});
    return out;
}

// Every leaf path with its JSON type ("measurements[].machine.flow:number"),
// skipping the embedded profile, which is whatever snapshot the shot carried.
void jsonShape(const QJsonValue& v, const QString& path, QSet<QString>& out) {
    if (path == QLatin1String(".workflow.profile")) { out.insert(path + QStringLiteral(":object")); return; }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) jsonShape(it.value(), path + '.' + it.key(), out);
    } else if (v.isArray()) {
        for (const QJsonValue& x : v.toArray()) jsonShape(x, path + QStringLiteral("[]"), out);
    } else {
        out.insert(path + (v.isString() ? ":string" : v.isDouble() ? ":number" : v.isBool() ? ":bool" : ":null"));
    }
}

QByteArray basic(const QString& user, const QString& secret) {
    return "Basic " + (user + ':' + secret).toUtf8().toBase64();
}

}  // namespace

class tst_DecentShotUpload : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    static ShotRecord makeShot() {
        ShotRecord r;
        r.summary.uuid = QStringLiteral("0b6f7c1e-5d2a-4c1e-9a77-3f1d2e4b5a60");
        r.summary.timestamp = 1790000000;
        r.summary.profileName = QStringLiteral("Blooming espresso");
        r.summary.beverageType = QStringLiteral("espresso");
        r.summary.duration = 30;
        r.profileJson = QStringLiteral(R"({"version":"2","title":"Blooming espresso","steps":[]})");
        r.pressure = {QPointF(0.0, 1.0), QPointF(0.25, 2.0), QPointF(0.5, 3.0)};
        r.flow = {QPointF(0.0, 0.5), QPointF(0.25, 1.0), QPointF(0.5, 1.5)};
        return r;
    }

    struct Rig {
        CannedNam nam;
        SettingsDecent settings;
        ShotHistoryStorage storage;
        DecentAccount account{&nam, &settings};
        DecentShotUploader uploader{&nam, &account, &storage};
        QString serial = QStringLiteral("1234");
        qint64 shotId = 0;

        explicit Rig(const QString& dbPath) {
            settings.setAccount(QStringLiteral("owner@example.com"), QStringLiteral("token"));
            uploader.setRetryDelayMs(0);
            uploader.setMachineIdentityProvider([this]() {
                return DecentMachineIdentity{serial, QStringLiteral("1352"), QStringLiteral("DE1PRO")};
            });
            if (storage.initialize(dbPath)) shotId = storage.importShotRecord(makeShot(), false);
        }
        ~Rig() {
            settings.clearAccount();
            settings.setEnabled(false);
            // A worker still busy after 5 s surfaces as the queued-work warning on close.
            (void)QTest::qWaitFor([this]() { return storage.isDbWorkIdle(); }, 5000);
            storage.close();
            (void)QTest::qWaitFor([this]() { return storage.isDbWorkIdle(); }, 5000);
        }
        DecentShotUploader::Result upload() {
            QSignalSpy finished(&uploader, &DecentShotUploader::uploadFinished);
            uploader.uploadNow(shotId);
            if (finished.isEmpty() && !finished.wait(5000)) return DecentShotUploader::Result::None;
            return finished.first().at(1).value<DecentShotUploader::Result>();
        }
        // The uploader posts its state write before it reports, so once the DB
        // worker is idle the newest write has landed.
        DecentUploadState state() {
            // If it never idles, the caller's assertions on the state fail instead.
            (void)QTest::qWaitFor([this]() { return storage.isDbWorkIdle(); }, 5000);
            DecentUploadState s;
            withTempDb(storage.databasePath(), "tst_decent", [&](QSqlDatabase& db) {
                ShotHistoryStorage::loadDecentUploadStateStatic(db, shotId, &s);
            });
            return s;
        }
        QJsonObject sentDocument(qsizetype i) const { return QJsonDocument::fromJson(nam.bodies.at(i)).object(); }
    };

private slots:
    // Each rig runs the whole migration chain; its debug lines would bury the
    // ones that matter.
    void initTestCase() {
        QLoggingCategory::setFilterRules(QStringLiteral("default.debug=false"));
        // Uploads write last_decent_upload.json to Documents; keep it out of the
        // developer's real one.
        QStandardPaths::setTestModeEnabled(true);
    }
    void init() { QTest::failOnWarning(); }

    void payloadKeepsTextAndAlignsSeries() {
        ShotRecord record = makeShot();
        record.summary.id = 1;   // convertShotRecord returns an empty projection for id 0
        ShotProjection shot = ShotHistoryStorage::convertShotRecord(record);
        QCOMPARE(shot.uuid, record.summary.uuid);
        shot.beanType = QStringLiteral("Café Allongé");
        shot.roastDate = QStringLiteral("last Tuesday");
        shot.weight = series({QPointF(0.3, 0.4)});   // a scale series on its own timeline

        const QByteArray body = DecentShotRecord::build(shot, {QStringLiteral("1234"), QString(), QString()});
        QVERIFY2(body.contains("Caf\xc3\xa9 Allong\xc3\xa9"), "accented text must be UTF-8 encoded exactly once");

        const QJsonObject doc = QJsonDocument::fromJson(body).object();
        QCOMPARE(doc["id"].toString(), shot.uuid);
        QCOMPARE(doc["machine"].toObject()["serialNumber"].toString(), QStringLiteral("1234"));
        QVERIFY(!doc["machine"].toObject().contains("model"));   // empty fields are omitted
        QCOMPARE(doc["workflow"].toObject()["profile"].toObject()["title"].toString(), QStringLiteral("Blooming espresso"));
        const QJsonObject context = doc["workflow"].toObject()["context"].toObject();
        QCOMPARE(context["coffeeName"].toString(), QStringLiteral("Café Allongé"));
        QVERIFY(!context.contains("extras"));   // an unparseable roast date is not sent
        const QJsonArray m = doc["measurements"].toArray();
        QCOMPARE(m.size(), shot.pressure.size());
        for (const QJsonValue& sample : m) {
            QVERIFY(sample.toObject()["machine"].toObject().contains("targetGroupTemperature"));
            QVERIFY(sample.toObject().contains("scale"));
        }
        QCOMPARE(m[1].toObject()["machine"].toObject()["timestamp"].toString(),
                 QStringLiteral("2026-09-21T14:13:20.250Z"));
    }

    // tests/data/decent/accepted_shotrecord.json is a body decentespresso.com
    // accepted on 2026-10-04 (personal values replaced, trimmed to three samples).
    // A field renamed, dropped or retyped here fails this before it fails there.
    void payloadHasTheShapeTheServerAccepted() {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/tests/data/decent/accepted_shotrecord.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QSet<QString> accepted;
        jsonShape(QJsonDocument::fromJson(file.readAll()).object(), QString(), accepted);

        ShotRecord record = makeShot();
        record.summary.id = 1;
        ShotProjection shot = ShotHistoryStorage::convertShotRecord(record);
        shot.doseWeightG = 18;
        shot.targetWeightG = 36;
        shot.finalWeightG = 35.5;
        shot.grinderModel = QStringLiteral("Grinder");
        shot.grinderSetting = QStringLiteral("10");
        shot.rpm = 1000;
        shot.beanType = QStringLiteral("House Espresso");
        shot.beanBrand = QStringLiteral("Roaster");
        shot.roastDate = QStringLiteral("2026-01-01");
        shot.roastLevel = QStringLiteral("Medium");
        shot.barista = QStringLiteral("Barista");
        shot.weight = series({QPointF(0.0, 0.0), QPointF(0.5, 1.0)});
        QSet<QString> built;
        jsonShape(QJsonDocument::fromJson(DecentShotRecord::build(shot, {QStringLiteral("1234"), {}, {}})).object(),
                  QString(), built);

        QVERIFY(!accepted.isEmpty());
        QCOMPARE(QSet<QString>(built - accepted), QSet<QString>());
        QCOMPARE(QSet<QString>(accepted - built), QSet<QString>());
    }

    // The shared eligibility policy (Visualizer, MCP, and Decent's uploads).
    void uploadIneligibility_data() {
        QTest::addColumn<QString>("beverageType");
        QTest::addColumn<double>("durationSec");
        QTest::addColumn<int>("expected");
        QTest::newRow("espresso") << "espresso" << 30.0 << int(UploadIneligible::None);
        QTest::newRow("exactly the minimum") << "espresso" << 6.0 << int(UploadIneligible::None);
        QTest::newRow("just short") << "espresso" << 5.9 << int(UploadIneligible::TooShort);
        QTest::newRow("cleaning") << "Cleaning" << 300.0 << int(UploadIneligible::Maintenance);
        QTest::newRow("descale") << "descale" << 300.0 << int(UploadIneligible::Maintenance);
        QTest::newRow("short cleaning is maintenance") << "cleaning" << 2.0 << int(UploadIneligible::Maintenance);
    }
    void uploadIneligibility() {
        QFETCH(QString, beverageType);
        QFETCH(double, durationSec);
        QFETCH(int, expected);
        QCOMPARE(int(::uploadIneligibility(beverageType, durationSec, 6.0)), expected);
    }

    void classify_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("transport");
        QTest::addColumn<int>("expected");
        using C = DecentShotUploader::ResponseClass;
        QTest::newRow("created") << 200 << false << int(C::Success);
        QTest::newRow("transport") << 0 << true << int(C::Transient);
        QTest::newRow("unauthorized") << 401 << false << int(C::AuthFailed);
        QTest::newRow("not your machine") << 403 << false << int(C::NotRegistered);
        QTest::newRow("timeout") << 408 << false << int(C::Transient);
        QTest::newRow("endpoint gone") << 404 << false << int(C::Transient);
        QTest::newRow("rate limited") << 429 << false << int(C::Transient);
        QTest::newRow("bad document") << 400 << false << int(C::Permanent);
        QTest::newRow("server error") << 503 << false << int(C::Transient);
    }
    void classify() {
        QFETCH(int, status);
        QFETCH(bool, transport);
        QFETCH(int, expected);
        QCOMPARE(int(DecentShotUploader::classify(status, transport)), expected);
    }

    void uploadRecordsTheShotAndReplacesUnderItsSerial() {
        Rig rig(m_dir.filePath("upload.db"));
        QVERIFY(rig.shotId > 0);
        rig.nam.replies = {{200, R"({"ok":true,"stored":true,"id":"srv-1"})"}};
        // Recording an upload must never read as a user edit (Stage 2 replaces on edits).
        QSignalSpy edits(&rig.storage, &ShotHistoryStorage::shotMetadataUpdated);

        QCOMPARE(rig.upload(), DecentShotUploader::Result::Uploaded);
        const DecentUploadState first = rig.state();
        QCOMPARE(edits.count(), 0);
        QVERIFY(first.uploaded());
        QCOMPARE(first.serverShotId, QStringLiteral("srv-1"));
        QCOMPARE(first.serial, QStringLiteral("1234"));
        QCOMPARE(rig.nam.requests.at(0).url().query(), QString());
        QCOMPARE(rig.nam.requests.at(0).rawHeader("Authorization"), basic("owner@example.com", "token"));
        QCOMPARE(rig.nam.requests.at(0).header(QNetworkRequest::ContentTypeHeader).toString(), QStringLiteral("application/json"));
        QCOMPARE(rig.sentDocument(0)["machine"].toObject()["model"].toString(), QStringLiteral("DE1PRO"));

        // Re-sending while another machine is connected replaces under the first serial.
        rig.serial = QStringLiteral("9999");
        rig.nam.replies = {{200, R"({"ok":true,"stored":true,"replaced":true,"id":"srv-1"})"}};
        QCOMPARE(rig.upload(), DecentShotUploader::Result::Uploaded);
        QCOMPARE(rig.nam.requests.at(1).url().query(), QStringLiteral("replace=1"));
        QCOMPARE(rig.sentDocument(1)["machine"].toObject()["serialNumber"].toString(), QStringLiteral("1234"));

        // A replace the server answers with "duplicate" kept its old copy: not a success.
        rig.nam.replies = {{200, R"({"ok":true,"stored":false,"duplicate":true,"id":"srv-1"})"}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("answered a replace with \"duplicate\""));
        QCOMPARE(rig.upload(), DecentShotUploader::Result::NotReplaced);
        // The edit did not land: it stays pending, and the earlier upload stands.
        const DecentUploadState kept = rig.state();
        QVERIFY(kept.replacePending);
        QCOMPARE(kept.serverShotId, QStringLiteral("srv-1"));
    }

    void linkKeepsOnlyTheEncryptedPassword_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("error");
        QTest::newRow("accepted") << 200 << QByteArray("11c393223f0d8f7b\n") << int(AccountLink::Error::None);
        QTest::newRow("wrong password") << 200 << QByteArray("0") << int(AccountLink::Error::Rejected);
        QTest::newRow("offline") << 0 << QByteArray() << int(AccountLink::Error::Unreachable);
        QTest::newRow("server error") << 500 << QByteArray() << int(AccountLink::Error::ServerError);
        QTest::newRow("captive portal") << 200 << QByteArray("<html><body>Sign in to Wi-Fi</body></html>")
                                        << int(AccountLink::Error::ServerError);
    }
    void linkKeepsOnlyTheEncryptedPassword() {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(int, error);
        CannedNam nam;
        nam.replies = {{status, body}};
        SettingsDecent settings;
        settings.clearAccount();
        settings.setEnabled(false);
        DecentAccount account(&nam, &settings);
        QSignalSpy finished(&account, &DecentAccount::linkFinished);
        if (error == int(AccountLink::Error::Unreachable))
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("server unreachable"));
        if (error == int(AccountLink::Error::ServerError))
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("link failed"));

        account.link(QStringLiteral(" owner@example.com "), QStringLiteral("plain-password"));
        QVERIFY(finished.wait(2000));

        QCOMPARE(int(finished.first().at(0).value<AccountLink::Error>()), error);
        QCOMPARE(nam.requests.first().rawHeader("Authorization"), basic("owner@example.com", "plain-password"));
        const bool accepted = error == int(AccountLink::Error::None);
        QCOMPARE(account.state() == DecentAccount::State::Linked, accepted);
        QCOMPARE(settings.encryptedPassword(), accepted ? QStringLiteral("11c393223f0d8f7b") : QString());
        QCOMPARE(settings.enabled(), accepted);   // connecting switches Decent on
        settings.setEnabled(false);
        settings.clearAccount();
    }

    void failuresAreClassifiedAndOnlyPermanentOnesRecorded() {
        Rig rig(m_dir.filePath("failures.db"));
        QVERIFY(rig.shotId > 0);

        rig.nam.replies = {{503, {}}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("not uploaded after 3 attempts"));
        QCOMPARE(rig.upload(), DecentShotUploader::Result::Failed);
        QCOMPARE(rig.nam.requests.size(), DecentShotUploader::kAttempts);

        // A 2xx that is not the API's answer (a captive portal) stored nothing,
        // and its body is logged once, not per attempt.
        rig.nam.replies = {{200, "<html>Sign in to Wi-Fi</html>"}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("not the upload API's answer <html>"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("not uploaded after 3 attempts \\(HTTP 200 that is not"));
        QCOMPARE(rig.upload(), DecentShotUploader::Result::Failed);

        rig.nam.replies = {{403, R"({"ok":false,"error":"not your machine"})"}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("serial 1234 is not registered"));
        QCOMPARE(rig.upload(), DecentShotUploader::Result::NotRegistered);
        QCOMPARE(rig.uploader.lastSerial(), QStringLiteral("1234"));

        rig.nam.replies = {{400, R"({"ok":false,"error":"no workflow.profile"})"}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejected \\(HTTP 400"));
        QCOMPARE(rig.upload(), DecentShotUploader::Result::Rejected);
        const DecentUploadState rejected = rig.state();
        QCOMPARE(rejected.rejectedStatus, 400);
        QVERIFY(!rejected.uploaded());

        // Uploading it after all clears the rejection.
        rig.nam.replies = {{200, R"({"ok":true,"stored":true,"id":"srv-2"})"}};
        QCOMPARE(rig.upload(), DecentShotUploader::Result::Uploaded);
        const DecentUploadState stored = rig.state();
        QVERIFY(stored.uploaded());
        QVERIFY(!stored.rejected());

        rig.nam.replies = {{401, {}}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejected the stored credentials"));
        QCOMPARE(rig.upload(), DecentShotUploader::Result::NeedsSignIn);
        QCOMPARE(rig.account.state(), DecentAccount::State::NeedsSignIn);

        // Credentials the server refused are not sent again.
        const qsizetype sent = rig.nam.requests.size();
        QCOMPARE(rig.upload(), DecentShotUploader::Result::NeedsSignIn);
        QCOMPARE(rig.nam.requests.size(), sent);

        // Signing in again clears it, and uploads resume.
        rig.nam.replies = {{200, "fresh-token"}};
        QSignalSpy linked(&rig.account, &DecentAccount::linkFinished);
        rig.account.link(QStringLiteral("owner@example.com"), QStringLiteral("new-password"));
        QVERIFY(linked.wait(2000));
        QCOMPARE(rig.account.state(), DecentAccount::State::Linked);
        rig.nam.replies = {{200, R"({"ok":true,"stored":true,"replaced":true,"id":"srv-2"})"}};
        QCOMPARE(rig.upload(), DecentShotUploader::Result::Uploaded);
    }

    void firstUploadAnsweredDuplicateIsStored() {
        // After a reinstall or a restore that lost the upload state, the server
        // already holds the shot: that is an upload, not a failure.
        Rig rig(m_dir.filePath("duplicate.db"));
        QVERIFY(rig.shotId > 0);
        rig.nam.replies = {{200, R"({"ok":true,"stored":false,"duplicate":true,"id":"srv-9"})"}};
        QCOMPARE(rig.upload(), DecentShotUploader::Result::Uploaded);
        QCOMPARE(rig.state().serverShotId, QStringLiteral("srv-9"));
    }

    void ineligibleShotsAreNotSent() {
        Rig rig(m_dir.filePath("ineligible.db"));
        QVERIFY(rig.shotId > 0);
        rig.uploader.setMinDurationProvider([]() { return 60.0; });
        QCOMPARE(rig.upload(), DecentShotUploader::Result::TooShort);
        QVERIFY(rig.nam.requests.isEmpty());
    }

    void disconnectingCancelsASignInInFlight() {
        CannedNam nam;
        nam.replies = {{200, "11c393223f0d8f7b"}};
        SettingsDecent settings;
        settings.clearAccount();
        settings.setEnabled(false);
        DecentAccount account(&nam, &settings);
        QSignalSpy finished(&account, &DecentAccount::linkFinished);

        account.link(QStringLiteral("owner@example.com"), QStringLiteral("plain-password"));
        account.unlink();
        QVERIFY(!account.busy());
        QCoreApplication::processEvents();   // the canned reply would finish now

        QVERIFY(finished.isEmpty());
        QCOMPARE(account.state(), DecentAccount::State::NotLinked);
        QVERIFY(!settings.enabled());
    }

    void firstUploadNeedsAConnectedMachine() {
        Rig rig(m_dir.filePath("nomachine.db"));
        QVERIFY(rig.shotId > 0);
        rig.serial.clear();
        QCOMPARE(rig.upload(), DecentShotUploader::Result::NoMachine);
        QVERIFY(rig.nam.requests.isEmpty());
    }
};

QTEST_GUILESS_MAIN(tst_DecentShotUpload)
#include "tst_decentshotupload.moc"
