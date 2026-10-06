// Decent account link and shot upload (add-decent-shot-upload): the ShotRecord
// payload, the shared response table, the login_test exchange, the uploaders
// against a real shot database with canned server replies, and the ShotUploads
// path every destination is reached through.

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
#include <QUuid>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>

#include "core/dbutils.h"
#include "core/settings.h"
#include "core/settings_decent.h"
#include "core/settings_upload.h"
#include "core/settings_visualizer.h"
#include "history/shothistorystorage.h"
#include "network/decentaccount.h"
#include "network/decentshotrecord.h"
#include "network/decentshotuploader.h"
#include "network/shotpayloadhelpers.h"
#include "network/shotserveruploadroute.h"
#include "network/shotuploads.h"
#include "network/visualizeruploader.h"

namespace {

// A destination that records the attempts ShotUploads makes. Each attempt
// answers `answer`; with `holding` set it stays out until finish(), like a
// request in flight.
struct FakeDestination : ShotUploadDestination {
    using Sent = QPair<qint64, Send>;
    QString label;
    bool active = true;
    bool holding = false;
    Outcome answer = Outcome::Sent;
    QList<Sent> sent;
    QList<qint64> edited;

    explicit FakeDestination(QString n) : label(std::move(n)) {}
    QString name() const override { return label; }
    bool isActive() const override { return active; }
    QString held = QStringLiteral("0"), unsent = QStringLiteral("0");   // SQL over shots, for Upload missing shots
    bool holdsShot(QSqlDatabase&, qint64) const override { return false; }
    QString heldCondition() const override { return held; }
    QString unsentEditCondition() const override { return unsent; }
    void attemptSavedShot(qint64 shotId, Send how) override {
        sent.append({shotId, how});
        if (!holding) finish();
    }
    void sendFinished(qint64, Attempt) override {}
    void noteEdited(qint64 shotId) override { edited.append(shotId); }
    void finish() { finishAttempt({answer, 0}); }
};

// Drains the queued hops ShotUploads posts (attempt -> finishSend -> pump).
void settle() {
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
}

// A worker still busy after 5 s surfaces as the queued-work warning on close.
void closeStorage(ShotHistoryStorage& storage) {
    (void)QTest::qWaitFor([&storage]() { return storage.isDbWorkIdle(); }, 5000);
    storage.close();
    (void)QTest::qWaitFor([&storage]() { return storage.isDbWorkIdle(); }, 5000);
}

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

    // The Decent account reached the way the app reaches it: through ShotUploads,
    // which makes the attempts and records the outcome.
    struct Rig {
        CannedNam nam;
        SettingsDecent settings;
        SettingsUpload upload;
        ShotHistoryStorage storage;
        DecentAccount account{&nam, &settings};
        DecentShotUploader uploader{&nam, &account, &storage};
        ShotUploads uploads{&upload, &storage, {&uploader}};
        QString serial = QStringLiteral("1234");
        qint64 shotId = 0;
        bool autoUpdate = upload.autoUpdate();

        explicit Rig(const QString& dbPath) {
            settings.setAccount(QStringLiteral("owner@example.com"), QStringLiteral("token"));
            settings.setEnabled(true);
            upload.setAutoUpdate(false);
            uploads.setRetryDelayMs(0);
            uploader.setMachineIdentityProvider([this]() {
                return DecentMachineIdentity{serial, QStringLiteral("1352"), QStringLiteral("DE1PRO")};
            });
            if (storage.initialize(dbPath)) shotId = storage.importShotRecord(makeShot(), false);
        }
        ~Rig() {
            settings.clearAccount();
            settings.setEnabled(false);
            upload.setAutoUpdate(autoUpdate);
            closeStorage(storage);
        }
        DecentShotUploader::Result send() {
            QSignalSpy finished(&uploader, &DecentShotUploader::uploadFinished);
            uploads.uploadNow(shotId);
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

    // The one table both destinations read an answer with (D15).
    void responseOutcome_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("transport");
        QTest::addColumn<int>("expected");
        using O = ShotUploadDestination::Outcome;
        QTest::newRow("created") << 200 << false << int(O::Sent);
        QTest::newRow("transport") << 0 << true << int(O::Transient);
        QTest::newRow("unauthorized") << 401 << false << int(O::AuthFailed);
        QTest::newRow("account refused") << 403 << false << int(O::AccountRefused);
        QTest::newRow("timeout") << 408 << false << int(O::Transient);
        QTest::newRow("endpoint gone") << 404 << false << int(O::Transient);
        QTest::newRow("rate limited") << 429 << false << int(O::Transient);
        QTest::newRow("bad document") << 400 << false << int(O::Rejected);
        QTest::newRow("invalid shot") << 422 << false << int(O::Rejected);
        QTest::newRow("server error") << 503 << false << int(O::Transient);
    }
    void responseOutcome() {
        QFETCH(int, status);
        QFETCH(bool, transport);
        QFETCH(int, expected);
        QCOMPARE(int(ShotUploadDestination::responseOutcome(status, transport)), expected);
    }

    void uploadRecordsTheShotAndReplacesUnderItsSerial() {
        Rig rig(m_dir.filePath("upload.db"));
        QVERIFY(rig.shotId > 0);
        rig.nam.replies = {{200, R"({"ok":true,"stored":true,"id":"srv-1"})"}};
        // Recording an upload must never read as a user edit (ShotUploads re-sends on edits).
        QSignalSpy edits(&rig.storage, &ShotHistoryStorage::shotMetadataUpdated);

        QCOMPARE(rig.send(), DecentShotUploader::Result::Uploaded);
        const DecentUploadState first = rig.state();
        QCOMPARE(edits.count(), 0);
        QVERIFY(first.uploaded());
        QCOMPARE(first.serverShotId, QStringLiteral("srv-1"));
        QCOMPARE(first.serial, QStringLiteral("1234"));
        QCOMPARE(rig.nam.requests.at(0).url().query(), QString());
        QCOMPARE(rig.nam.requests.at(0).rawHeader("Authorization"), basic("owner@example.com", "token"));
        QCOMPARE(rig.nam.requests.at(0).header(QNetworkRequest::ContentTypeHeader).toString(), QStringLiteral("application/json"));
        QCOMPARE(rig.sentDocument(0)["machine"].toObject()["model"].toString(), QStringLiteral("DE1PRO"));

        // An edit made while an upload is out may not be in it: it stays pending.
        {
            rig.nam.replies = {{200, R"({"ok":true,"stored":true,"id":"srv-1"})"}};
            QSignalSpy finished(&rig.uploader, &DecentShotUploader::uploadFinished);
            rig.uploads.uploadNow(rig.shotId);
            rig.uploader.noteEdited(rig.shotId);
            QVERIFY(finished.wait(5000));
            QVERIFY(rig.state().replacePending);
            rig.nam.requests.removeLast();
            rig.nam.bodies.removeLast();
        }

        // Re-sending while another machine is connected replaces under the first serial.
        rig.serial = QStringLiteral("9999");
        rig.nam.replies = {{200, R"({"ok":true,"stored":true,"replaced":true,"id":"srv-1"})"}};
        QCOMPARE(rig.send(), DecentShotUploader::Result::Uploaded);
        QCOMPARE(rig.nam.requests.at(1).url().query(), QStringLiteral("replace=1"));
        QCOMPARE(rig.sentDocument(1)["machine"].toObject()["serialNumber"].toString(), QStringLiteral("1234"));

        // A replace the server answers with "duplicate" kept its old copy: not a success.
        rig.nam.replies = {{200, R"({"ok":true,"stored":false,"duplicate":true,"id":"srv-1"})"}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("answered a replace with \"duplicate\""));
        QCOMPARE(rig.send(), DecentShotUploader::Result::NotReplaced);
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
        QCOMPARE(rig.send(), DecentShotUploader::Result::Failed);

        // A 2xx that is not the API's answer (a captive portal) stored nothing,
        // and its body is logged once, not per attempt.
        rig.nam.replies = {{200, "<html>Sign in to Wi-Fi</html>"}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("not the upload API's answer <html>"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("not uploaded after 3 attempts \\(HTTP 200 that is not"));
        QCOMPARE(rig.send(), DecentShotUploader::Result::Failed);

        rig.nam.replies = {{403, R"({"ok":false,"error":"not your machine"})"}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("serial 1234 is not registered"));
        QCOMPARE(rig.send(), DecentShotUploader::Result::NotRegistered);
        QCOMPARE(rig.uploader.lastSerial(), QStringLiteral("1234"));

        rig.nam.replies = {{400, R"({"ok":false,"error":"no workflow.profile"})"}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejected \\(HTTP 400"));
        QCOMPARE(rig.send(), DecentShotUploader::Result::Rejected);
        const DecentUploadState rejected = rig.state();
        QCOMPARE(rejected.rejectedStatus, 400);
        QVERIFY(!rejected.uploaded());

        // Uploading it after all clears the rejection.
        rig.nam.replies = {{200, R"({"ok":true,"stored":true,"id":"srv-2"})"}};
        QCOMPARE(rig.send(), DecentShotUploader::Result::Uploaded);
        const DecentUploadState stored = rig.state();
        QVERIFY(stored.uploaded());
        QVERIFY(!stored.rejected());

        rig.nam.replies = {{401, {}}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejected the stored credentials"));
        QCOMPARE(rig.send(), DecentShotUploader::Result::NeedsSignIn);
        QCOMPARE(rig.account.state(), DecentAccount::State::NeedsSignIn);

        // Credentials the server refused are not sent again: nothing is attempted.
        const qsizetype sent = rig.nam.requests.size();
        QVERIFY(rig.uploads.activeDestinations().isEmpty());
        rig.uploads.uploadNow(rig.shotId);
        settle();
        QCOMPARE(rig.nam.requests.size(), sent);

        // Signing in again clears it, and uploads resume.
        rig.nam.replies = {{200, "fresh-token"}};
        QSignalSpy linked(&rig.account, &DecentAccount::linkFinished);
        rig.account.link(QStringLiteral("owner@example.com"), QStringLiteral("new-password"));
        QVERIFY(linked.wait(2000));
        QCOMPARE(rig.account.state(), DecentAccount::State::Linked);
        rig.nam.replies = {{200, R"({"ok":true,"stored":true,"replaced":true,"id":"srv-2"})"}};
        QCOMPARE(rig.send(), DecentShotUploader::Result::Uploaded);
    }

    void firstUploadAnsweredDuplicateIsStored() {
        // After a reinstall or a restore that lost the upload state, the server
        // already holds the shot: that is an upload, not a failure.
        Rig rig(m_dir.filePath("duplicate.db"));
        QVERIFY(rig.shotId > 0);
        rig.nam.replies = {{200, R"({"ok":true,"stored":false,"duplicate":true,"id":"srv-9"})"}};
        QCOMPARE(rig.send(), DecentShotUploader::Result::Uploaded);
        QCOMPARE(rig.state().serverShotId, QStringLiteral("srv-9"));
    }

    void ineligibleShotsAreNotSent() {
        Rig rig(m_dir.filePath("ineligible.db"));
        QVERIFY(rig.shotId > 0);
        rig.uploader.setMinDurationProvider([]() { return 60.0; });
        QCOMPARE(rig.send(), DecentShotUploader::Result::TooShort);
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

        // Exactly one answer, the cancel: a caller waiting on it is never left hanging,
        // and the late reply does not answer again or re-link.
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).value<AccountLink::Error>(), AccountLink::Error::Cancelled);
        QCOMPARE(account.state(), DecentAccount::State::NotLinked);
        QVERIFY(!settings.enabled());
    }

    // ShotUploads: the shared settings decide, each active destination gets the
    // shot, one at a time, never queued twice.
    void shotUploadsAppliesTheSharedSettingsOnce() {
        using Send = ShotUploadDestination::Send;
        using Sent = FakeDestination::Sent;
        SettingsUpload upload;
        const bool autoUpload = upload.autoUpload(), autoUpdate = upload.autoUpdate();
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(m_dir.filePath("shared.db")));
        FakeDestination visualizer(QStringLiteral("visualizer")), decent(QStringLiteral("decent"));
        ShotUploads uploads(&upload, &storage, {&visualizer, &decent});
        decent.active = false;
        QCOMPARE(uploads.activeDestinations(), QStringList{QStringLiteral("visualizer")});

        upload.setAutoUpload(false);
        uploads.shotSaved(1);
        QVERIFY(visualizer.sent.isEmpty());
        upload.setAutoUpload(true);
        uploads.shotSaved(1);
        QCOMPARE(visualizer.sent, QList<Sent>{Sent(1, Send::UploadOrUpdate)});
        QVERIFY(decent.sent.isEmpty());
        settle();

        // An edit is noted everywhere, and sent only while automatic update is on.
        decent.active = true;
        upload.setAutoUpdate(false);
        emit storage.shotMetadataUpdated(2, true);
        QCOMPARE(decent.edited, QList<qint64>{2});
        QCOMPARE(visualizer.sent.size(), 1);
        upload.setAutoUpdate(true);
        emit storage.shotMetadataUpdated(2, false);
        QCOMPARE(visualizer.sent.size(), 1);
        emit storage.shotMetadataUpdated(2, true);
        QCOMPARE(visualizer.sent.last(), Sent(2, Send::UpdateOnly));
        QCOMPARE(decent.sent, QList<Sent>{Sent(2, Send::UpdateOnly)});

        // While a request is out the rest wait, and a shot is queued once, as an
        // upload if either request was one.
        settle();
        visualizer.sent.clear();
        visualizer.holding = true;
        uploads.uploadNow(3);
        emit storage.shotMetadataUpdated(4, true);
        uploads.uploadNow(4);
        uploads.uploadNow(4);
        QCOMPARE(visualizer.sent, QList<Sent>{Sent(3, Send::UploadOrUpdate)});
        visualizer.finish();
        settle();
        QCOMPARE(visualizer.sent.last(), Sent(4, Send::UploadOrUpdate));
        visualizer.finish();
        settle();
        QCOMPARE(visualizer.sent.size(), 2);

        // Switched off with shots waiting: they are dropped, not sent later.
        uploads.uploadNow(5);
        uploads.uploadNow(6);
        visualizer.active = false;
        visualizer.finish();
        settle();
        visualizer.active = true;
        visualizer.holding = false;
        uploads.uploadNow(7);
        QCOMPARE(visualizer.sent.last(), Sent(7, Send::UploadOrUpdate));
        QVERIFY(!std::any_of(visualizer.sent.cbegin(), visualizer.sent.cend(),
                             [](const Sent& s) { return s.first == 6; }));

        upload.setAutoUpload(autoUpload);
        upload.setAutoUpdate(autoUpdate);
        closeStorage(storage);
    }

    // The review page holds its shot: field-by-field saves go out once, on close.
    void shotUploadsSendsAHeldShotsEditsOnRelease() {
        using Send = ShotUploadDestination::Send;
        using Sent = FakeDestination::Sent;
        SettingsUpload upload;
        const bool autoUpdate = upload.autoUpdate();
        upload.setAutoUpdate(true);
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(m_dir.filePath("held.db")));
        FakeDestination destination(QStringLiteral("visualizer"));
        ShotUploads uploads(&upload, &storage, {&destination});

        // The page's own saves are held and go out once, on release.
        uploads.holdUpdates(8);
        uploads.expectHeldEdit(8);
        uploads.expectHeldEdit(8);
        emit storage.shotMetadataUpdated(8, true);
        emit storage.shotMetadataUpdated(8, true);
        QVERIFY(destination.sent.isEmpty());
        QCOMPARE(destination.edited.size(), 2);
        // An edit from elsewhere (MCP, ShotServer) is not the page's: it goes out now.
        emit storage.shotMetadataUpdated(8, true);
        QCOMPARE(destination.sent, QList<Sent>{Sent(8, Send::UpdateOnly)});
        uploads.releaseUpdates(8);
        settle();
        QCOMPARE(destination.sent, (QList<Sent>{Sent(8, Send::UpdateOnly), Sent(8, Send::UpdateOnly)}));

        // Upload takes the page's saves so far, even one whose write is still out;
        // closing then sends nothing more.
        settle();
        destination.sent.clear();
        uploads.holdUpdates(9);
        uploads.expectHeldEdit(9);
        uploads.uploadNow(9);
        emit storage.shotMetadataUpdated(9, true);
        uploads.releaseUpdates(9);
        QCOMPARE(destination.sent, QList<Sent>{Sent(9, Send::UploadOrUpdate)});

        // Closing without an edit sends nothing.
        uploads.holdUpdates(10);
        uploads.releaseUpdates(10);
        QCOMPARE(destination.sent.size(), 1);

        upload.setAutoUpdate(autoUpdate);
        closeStorage(storage);
    }

    // D15: the same answers from either server lead to the same attempts and the
    // same record. Both real destinations, sent through one ShotUploads.
    void bothDestinationsBehaveTheSame_data() {
        QTest::addColumn<QList<int>>("statuses");   // per attempt; the last one repeats
        QTest::addColumn<bool>("held");              // already on both: Decent replaces, Visualizer PATCHes
        QTest::addColumn<int>("attempts");
        QTest::addColumn<QString>("record");         // failed, rejected or nothing
        QTest::addColumn<QStringList>("warnings");   // Decent's, then Visualizer's
        const QString gaveUp = QStringLiteral("not uploaded after 3 attempts");
        QTest::newRow("server error") << QList<int>{503} << false << 3 << "failed" << QStringList{gaveUp, gaveUp};
        QTest::newRow("offline") << QList<int>{0} << false << 3 << "failed" << QStringList{gaveUp, gaveUp};
        QTest::newRow("rate limited") << QList<int>{429} << false << 3 << "failed" << QStringList{gaveUp, gaveUp};
        QTest::newRow("recovers") << QList<int>{503, 200} << false << 2 << "" << QStringList{};
        QTest::newRow("refused shot") << QList<int>{400} << false << 1 << "rejected"
                                      << QStringList{"rejected \\(HTTP 400", "Upload failed: shotId=\\d+ httpStatus=400"};
        QTest::newRow("account refused") << QList<int>{403} << false << 1 << ""
                                         << QStringList{"is not registered", "Upload failed: shotId=\\d+ httpStatus=403"};
        QTest::newRow("sign-in refused") << QList<int>{401} << false << 1 << ""
                                         << QStringList{"rejected the stored credentials", "Upload failed: shotId=\\d+ httpStatus=401"};
        QTest::newRow("update: server error") << QList<int>{503} << true << 3 << "failed" << QStringList{gaveUp, gaveUp};
        QTest::newRow("update: recovers") << QList<int>{503, 200} << true << 2 << "" << QStringList{};
        QTest::newRow("update: refused") << QList<int>{422} << true << 1 << "rejected"
                                         << QStringList{"rejected \\(HTTP 422", "Update failed: remoteShotId=.* httpStatus=422"};
    }
    void bothDestinationsBehaveTheSame() {
        QFETCH(QList<int>, statuses);
        QFETCH(bool, held);
        QFETCH(int, attempts);
        QFETCH(QString, record);
        QFETCH(QStringList, warnings);
        for (const QString& warning : std::as_const(warnings))
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(warning));

        CannedNam decentNam, visualizerNam;
        for (int status : std::as_const(statuses)) {
            decentNam.replies.append({status, status == 200 ? QByteArray(R"({"ok":true,"stored":true,"replaced":true,"id":"srv-1"})") : QByteArray()});
            visualizerNam.replies.append({status, status == 200 ? QByteArray(R"({"id":"viz-1"})") : QByteArray()});
        }
        Settings settings;
        SettingsVisualizer* vz = settings.visualizer();
        const QString vzUser = vz->visualizerUsername(), vzPassword = vz->visualizerPassword();
        const bool vzEnabled = vz->visualizerEnabled(), autoUpdate = settings.upload()->autoUpdate();
        vz->setVisualizerUsername(QStringLiteral("owner"));
        vz->setVisualizerPassword(QStringLiteral("secret"));
        vz->setVisualizerEnabled(true);
        settings.decent()->setAccount(QStringLiteral("owner@example.com"), QStringLiteral("token"));
        settings.decent()->setEnabled(true);
        settings.upload()->setAutoUpdate(false);
        // Restored however the function ends: later tests in this process read the same settings.
        const auto restore = qScopeGuard([&]() {
            settings.decent()->clearAccount();
            settings.decent()->setEnabled(false);
            settings.upload()->setAutoUpdate(autoUpdate);
            vz->setVisualizerEnabled(vzEnabled);
            vz->setVisualizerUsername(vzUser);
            vz->setVisualizerPassword(vzPassword);
        });

        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(m_dir.filePath(QStringLiteral("both-%1.db").arg(QTest::currentDataTag()).replace(':', '-'))));
        const qint64 shotId = storage.importShotRecord(makeShot(), false);
        QVERIFY(shotId > 0);
        if (held) {
            (void)QTest::qWaitFor([&storage]() { return storage.isDbWorkIdle(); }, 5000);
            withTempDb(storage.databasePath(), "tst_both_held", [&](QSqlDatabase& db) {
                QSqlQuery q(db);
                QVERIFY(q.exec(QStringLiteral("UPDATE shots SET visualizer_id = 'viz-1', decent_uploaded_at = 1, "
                                              "decent_shot_id = 'srv-1', decent_serial = '1234' WHERE id = %1").arg(shotId)));
            });
        }
        DecentAccount account(&decentNam, settings.decent());
        DecentShotUploader decent(&decentNam, &account, &storage);
        decent.setMachineIdentityProvider([]() { return DecentMachineIdentity{QStringLiteral("1234"), {}, {}}; });
        VisualizerUploader visualizer(&visualizerNam, &settings);
        visualizer.setStorage(&storage);
        ShotUploads uploads(settings.upload(), &storage, {&decent, &visualizer});
        uploads.setRetryDelayMs(0);

        // An edit to a shot neither holds sends nothing, and each destination's queue moves on.
        if (!held) {
            settings.upload()->setAutoUpdate(true);
            emit storage.shotMetadataUpdated(shotId, true);
            QTRY_VERIFY(!decent.uploading() && !visualizer.isUploading());
            settle();
            QVERIFY(decentNam.requests.isEmpty() && visualizerNam.requests.isEmpty());
            settings.upload()->setAutoUpdate(false);
        }

        QSignalSpy decentDone(&decent, &DecentShotUploader::uploadFinished);
        QSignalSpy visualizerDone(&visualizer, &VisualizerUploader::savedShotFinished);
        uploads.uploadNow(shotId);
        QTRY_VERIFY_WITH_TIMEOUT(!decentDone.isEmpty() && !visualizerDone.isEmpty(), 5000);
        QVERIFY(!decent.uploading());
        QVERIFY(!visualizer.isUploading());

        const auto uploadsTo = [](const CannedNam& nam, const char* path) {
            return std::count_if(nam.requests.cbegin(), nam.requests.cend(),
                                 [path](const QNetworkRequest& r) { return r.url().path() == QLatin1String(path); });
        };
        QCOMPARE(uploadsTo(decentNam, "/support/api/shot_upload"), attempts);
        QCOMPARE(uploadsTo(visualizerNam, held ? "/api/shots/viz-1" : "/api/shots/upload"), attempts);

        // Recorded the same way for both, in their own columns.
        (void)QTest::qWaitFor([&storage]() { return storage.isDbWorkIdle(); }, 5000);
        const auto recorded = [&storage, shotId](const QString& destination) {
            QString what;
            withTempDb(storage.databasePath(), "tst_both", [&](QSqlDatabase& db) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral("SELECT %1_failed_at, %1_rejected_status FROM shots WHERE id = :id").arg(destination));
                q.bindValue(":id", shotId);
                if (!q.exec() || !q.next()) { what = QStringLiteral("unreadable"); return; }
                what = !q.value(0).isNull() ? QStringLiteral("failed")
                       : !q.value(1).isNull() ? QStringLiteral("rejected") : QString();
            });
            return what;
        };
        QCOMPARE(recorded(QStringLiteral("decent")), record);
        QCOMPARE(recorded(QStringLiteral("visualizer")), record);

        // An edit may make a refused shot acceptable: it clears the rejection everywhere.
        if (record == QLatin1String("rejected")) {
            emit storage.shotMetadataUpdated(shotId, true);
            (void)QTest::qWaitFor([&storage]() { return storage.isDbWorkIdle(); }, 5000);
            QCOMPARE(recorded(QStringLiteral("decent")), QString());
            QCOMPARE(recorded(QStringLiteral("visualizer")), QString());
        }

        closeStorage(storage);
    }

    // A shot deleted on visualizer.coffee: the PATCH's 404 drops the dead link and
    // the same attempt uploads it again, once, with nothing recorded as failed.
    void visualizerReuploadsAShotDeletedThere() {
        CannedNam nam;
        nam.replies = {{404, {}}, {200, R"({"id":"viz-2"})"}};
        Settings settings;
        SettingsVisualizer* vz = settings.visualizer();
        const QString vzUser = vz->visualizerUsername(), vzPassword = vz->visualizerPassword();
        const bool vzEnabled = vz->visualizerEnabled();
        vz->setVisualizerUsername(QStringLiteral("owner"));
        vz->setVisualizerPassword(QStringLiteral("secret"));
        vz->setVisualizerEnabled(true);
        const auto restore = qScopeGuard([&]() {
            vz->setVisualizerEnabled(vzEnabled);
            vz->setVisualizerUsername(vzUser);
            vz->setVisualizerPassword(vzPassword);
        });
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(m_dir.filePath("relink.db")));
        const qint64 shotId = storage.importShotRecord(makeShot(), false);
        (void)QTest::qWaitFor([&storage]() { return storage.isDbWorkIdle(); }, 5000);
        withTempDb(storage.databasePath(), "tst_relink", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("UPDATE shots SET visualizer_id = 'gone' WHERE id = %1").arg(shotId)));
        });
        VisualizerUploader visualizer(&nam, &settings);
        visualizer.setStorage(&storage);
        ShotUploads uploads(settings.upload(), &storage, {&visualizer});
        uploads.setRetryDelayMs(0);
        QSignalSpy finished(&visualizer, &VisualizerUploader::savedShotFinished);
        QSignalSpy linked(&visualizer, &VisualizerUploader::uploadSucceededForShot);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Update failed: remoteShotId=.* httpStatus=404"));

        uploads.uploadNow(shotId);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(1).toString(), QString());   // no error
        const auto requestsTo = [&nam](const char* path) {
            return std::count_if(nam.requests.cbegin(), nam.requests.cend(),
                                 [path](const QNetworkRequest& r) { return r.url().path() == QLatin1String(path); });
        };
        QCOMPARE(requestsTo("/api/shots/gone"), 1);
        QCOMPARE(requestsTo("/api/shots/upload"), 1);
        QCOMPARE(linked.size(), 1);
        QCOMPARE(linked.first().at(1).toString(), QStringLiteral("viz-2"));

        (void)QTest::qWaitFor([&storage]() { return storage.isDbWorkIdle(); }, 5000);
        withTempDb(storage.databasePath(), "tst_relink", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("SELECT visualizer_id, visualizer_failed_at, visualizer_rejected_at FROM shots WHERE id = %1").arg(shotId)));
            QVERIFY(q.next());
            QVERIFY(q.value(0).toString().isEmpty());   // the dead link is gone; MainController writes the new one
            QVERIFY(q.value(1).isNull());
            QVERIFY(q.value(2).isNull());
        });
        closeStorage(storage);
    }

    // D14: what each destination is missing, chosen by one selection over each
    // destination's own columns.
    void missingShotsAreChosenTheSameWayForBoth_data() {
        QTest::addColumn<QString>("destination");
        QTest::addColumn<QString>("markHeld");      // SET clause
        QTest::addColumn<QString>("markUnsent");    // SET clause, on a held shot
        QTest::newRow("decent") << "decent" << "decent_uploaded_at = 1" << "decent_replace_pending = 1";
        QTest::newRow("visualizer") << "visualizer" << "visualizer_id = 'v'" << "visualizer_dirty = 1";
    }
    void missingShotsAreChosenTheSameWayForBoth() {
        QFETCH(QString, destination);
        QFETCH(QString, markHeld);
        QFETCH(QString, markUnsent);
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(m_dir.filePath(QStringLiteral("missing-%1.db").arg(destination))));
        // Oldest first, so ids and timestamps rise together.
        auto add = [&storage](qint64 timestamp, double duration, const QString& beverage) {
            ShotRecord r = makeShot();
            r.summary.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            r.summary.timestamp = timestamp;
            r.summary.duration = duration;
            r.summary.beverageType = beverage;
            return storage.importShotRecord(r, false);
        };
        const qint64 failed = add(1000, 30, QStringLiteral("espresso"));
        const qint64 held = add(2000, 30, QStringLiteral("espresso"));
        const qint64 rejected = add(3000, 30, QStringLiteral("espresso"));
        const qint64 cleaning = add(4000, 300, QStringLiteral("cleaning"));
        const qint64 tooShort = add(5000, 2, QStringLiteral("espresso"));
        const qint64 unsent = add(6000, 30, QStringLiteral("espresso"));
        const qint64 newest = add(7000, 30, QStringLiteral("espresso"));
        Q_UNUSED(cleaning);
        Q_UNUSED(tooShort);
        (void)QTest::qWaitFor([&storage]() { return storage.isDbWorkIdle(); }, 5000);

        CannedNam nam;
        SettingsDecent decentSettings;
        Settings settings;
        DecentAccount account(&nam, &decentSettings);
        DecentShotUploader decent(&nam, &account, &storage);
        VisualizerUploader visualizer(&nam, &settings);
        const ShotUploadDestination& target = destination == QLatin1String("decent")
            ? static_cast<const ShotUploadDestination&>(decent) : visualizer;

        ShotUploads::Missing missing;
        withTempDb(storage.databasePath(), "tst_missing", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            const auto set = [&q](const QString& clause, qint64 id) {
                QVERIFY2(q.exec(QStringLiteral("UPDATE shots SET %1 WHERE id = %2").arg(clause).arg(id)),
                         qPrintable(q.lastError().text()));
            };
            set(QStringLiteral("%1_failed_at = 1").arg(destination), failed);
            set(markHeld, held);
            set(QStringLiteral("%1_rejected_at = 1, %1_rejected_status = 400").arg(destination), rejected);
            set(markHeld + QStringLiteral(", ") + markUnsent, unsent);
            missing = ShotUploads::findMissing(db, target, 6.0);
        });

        // Unsent edits first, then the missing shots newest first; held, rejected,
        // maintenance and too-short shots are not offered.
        QCOMPARE(missing.shotIds, (QList<qint64>{unsent, newest, failed}));
        QCOMPARE(missing.unsentEdits, 1);
        QCOMPARE(missing.failed, 1);
        closeStorage(storage);
    }

    // D14: Upload missing shots sends newest first, kBatchSize at a time, only
    // while the machine is idle; moves past a failure, ends on a sign-in refusal,
    // resumes after a restart without the shots that already failed.
    void missingShotsRunInBatchesWhileIdle() {
        using Outcome = ShotUploadDestination::Outcome;
        SettingsUpload upload;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(m_dir.filePath("run.db")));
        QList<qint64> newestFirst;
        for (int i = 1; i <= 7; ++i) {
            ShotRecord r = makeShot();
            r.summary.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            r.summary.timestamp = 1000 * i;
            newestFirst.prepend(storage.importShotRecord(r, false));
        }
        FakeDestination decent(QStringLiteral("decent"));
        decent.holding = true;
        ShotUploads uploads(&upload, &storage, {&decent});
        uploads.setRetryDelayMs(0);
        uploads.setBatchSpacingMs(0);
        upload.setMissingRunStartedAt(QStringLiteral("decent"), 0);
        const auto sentIds = [&decent]() {
            QList<qint64> ids;
            for (const auto& sent : std::as_const(decent.sent)) ids.append(sent.first);
            return ids;
        };
        const auto step = [&decent]() { decent.finish(); settle(); };

        // Counted for the button, and never sent on its own.
        QTRY_COMPARE(uploads.missing().value(QStringLiteral("decent")).toMap().value("count").toInt(), 7);
        uploads.resumeMissingRuns();
        settle();
        QVERIFY(decent.sent.isEmpty());

        uploads.uploadMissing(QStringLiteral("decent"));
        QTRY_COMPARE(decent.sent.size(), 1);
        QCOMPARE(uploads.missing().value(QStringLiteral("decent")).toMap().value("total").toInt(), 7);
        QVERIFY(upload.missingRunStartedAt(QStringLiteral("decent")) > 0);
        for (int i = 0; i < 4; ++i) step();
        QCOMPARE(sentIds(), newestFirst.mid(0, 5));

        // The batch in flight finishes; the next waits for the machine to be idle,
        // then for the batch spacing.
        uploads.setMachineOperating(true);
        step();
        QCOMPARE(decent.sent.size(), 5);
        QCOMPARE(uploads.missing().value(QStringLiteral("decent")).toMap().value("done").toInt(), 5);
        uploads.setBatchSpacingMs(30);
        uploads.setMachineOperating(false);
        QTRY_COMPARE(sentIds().last(), newestFirst.at(5));

        // A shot that fails its 3 attempts is recorded and the run moves on.
        decent.answer = Outcome::Transient;
        step();
        step();
        step();
        QCOMPARE(sentIds().mid(5), (QList<qint64>{newestFirst.at(5), newestFirst.at(5), newestFirst.at(5), newestFirst.at(6)}));
        QCOMPARE(uploads.missing().value(QStringLiteral("decent")).toMap().value("done").toInt(), 6);
        decent.answer = Outcome::Sent;
        step();
        QCOMPARE(uploads.missing().value(QStringLiteral("decent")).toMap().value("running").toBool(), false);
        QCOMPARE(upload.missingRunStartedAt(QStringLiteral("decent")), 0);
        // The fake holds nothing, so all 7 are still missing, and the failure is counted.
        QTRY_COMPARE(uploads.missing().value(QStringLiteral("decent")).toMap().value("failed").toInt(), 1);
        uploads.setBatchSpacingMs(0);

        // After a restart a run resumes, leaving out what already failed in it.
        (void)QTest::qWaitFor([&storage]() { return storage.isDbWorkIdle(); }, 5000);
        upload.setMissingRunStartedAt(QStringLiteral("decent"), QDateTime::currentSecsSinceEpoch() - 60);
        decent.sent.clear();
        uploads.resumeMissingRuns();
        QTRY_COMPARE(decent.sent.size(), 1);
        QCOMPARE(uploads.missing().value(QStringLiteral("decent")).toMap().value("total").toInt(), 6);
        QCOMPARE(sentIds().first(), newestFirst.first());

        // A sign-in refusal, or an account refusal, ends the run.
        for (Outcome refusal : {Outcome::AuthFailed, Outcome::AccountRefused}) {
            if (refusal == Outcome::AccountRefused) {
                upload.setMissingRunStartedAt(QStringLiteral("decent"), QDateTime::currentSecsSinceEpoch() - 60);
                decent.sent.clear();
                uploads.resumeMissingRuns();
                QTRY_COMPARE(decent.sent.size(), 1);
            }
            decent.answer = refusal;
            step();
            QCOMPARE(uploads.missing().value(QStringLiteral("decent")).toMap().value("running").toBool(), false);
            QCOMPARE(upload.missingRunStartedAt(QStringLiteral("decent")), 0);
            settle();
            QCOMPARE(decent.sent.size(), 1);
        }

        closeStorage(storage);
    }

    // A run sends unsent edits as updates, never repeats the shot being sent,
    // ends when a send outside it is refused, and ends when switched off.
    void missingShotsRunAlongsideOtherSends() {
        using Send = ShotUploadDestination::Send;
        using Sent = FakeDestination::Sent;
        using Outcome = ShotUploadDestination::Outcome;
        SettingsUpload upload;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(m_dir.filePath("alongside.db")));
        auto add = [&storage](qint64 timestamp, double duration) {
            ShotRecord r = makeShot();
            r.summary.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            r.summary.timestamp = timestamp;
            r.summary.duration = duration;
            return storage.importShotRecord(r, false);
        };
        const qint64 edited = add(1000, 30), older = add(2000, 30), newer = add(3000, 30);
        const qint64 tooShort = add(4000, 2);   // never offered, so outside any run
        (void)QTest::qWaitFor([&storage]() { return storage.isDbWorkIdle(); }, 5000);
        withTempDb(storage.databasePath(), "tst_alongside", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("UPDATE shots SET decent_uploaded_at = 1, decent_replace_pending = 1 WHERE id = %1").arg(edited)));
        });
        FakeDestination decent(QStringLiteral("decent"));
        decent.held = QStringLiteral("decent_uploaded_at IS NOT NULL");
        decent.unsent = QStringLiteral("decent_replace_pending = 1");
        decent.holding = true;
        ShotUploads uploads(&upload, &storage, {&decent});
        uploads.setBatchSpacingMs(0);
        const auto running = [&uploads]() {
            return uploads.missing().value(QStringLiteral("decent")).toMap().value("running").toBool();
        };
        const auto step = [&decent]() { decent.finish(); settle(); };

        // Started while `newer` is already being sent: it goes once, and the unsent
        // edit goes first, as an update.
        uploads.uploadNow(newer);
        QVERIFY(uploads.uploadMissing(QStringLiteral("decent")));
        QTRY_COMPARE(uploads.missing().value(QStringLiteral("decent")).toMap().value("total").toInt(), 3);
        for (int i = 0; i < 3; ++i) step();
        QCOMPARE(decent.sent, (QList<Sent>{Sent(newer, Send::UploadOrUpdate), Sent(edited, Send::UpdateOnly),
                                           Sent(older, Send::UploadOrUpdate)}));
        QVERIFY(!running());

        // A refusal on a send outside the run drops the run's queued batch, so it ends.
        decent.sent.clear();
        uploads.uploadNow(tooShort);
        QVERIFY(uploads.uploadMissing(QStringLiteral("decent")));
        QTRY_VERIFY(uploads.missing().value(QStringLiteral("decent")).toMap().value("total").toInt() == 3);
        decent.answer = Outcome::AuthFailed;
        step();
        QVERIFY(!running());
        QCOMPARE(upload.missingRunStartedAt(QStringLiteral("decent")), 0);
        QCOMPARE(decent.sent, QList<Sent>{Sent(tooShort, Send::UploadOrUpdate)});

        // Switched off mid-run: the run ends and its batch is not sent later.
        decent.answer = Outcome::Sent;
        decent.sent.clear();
        QVERIFY(uploads.uploadMissing(QStringLiteral("decent")));
        QTRY_COMPARE(decent.sent.size(), 1);
        decent.active = false;
        step();
        QVERIFY(!running());
        QCOMPARE(upload.missingRunStartedAt(QStringLiteral("decent")), 0);
        decent.active = true;
        settle();
        QCOMPARE(decent.sent.size(), 1);

        closeStorage(storage);
    }

    // The Upload missing shots endpoints must not be taken for an APK upload: a
    // substring match did that, PackageInstaller was handed their 2-byte body, and
    // the web server stopped for the install handover.
    void streamedUploadsMatchExactPaths_data() {
        QTest::addColumn<QString>("requestLine");
        QTest::addColumn<int>("expected");
        QTest::newRow("apk") << "POST /upload HTTP/1.1" << int(StreamedUpload::Apk);
        QTest::newRow("apk, query") << "POST /upload?x=1 HTTP/1.1" << int(StreamedUpload::Apk);
        QTest::newRow("media") << "POST /upload/media HTTP/1.1" << int(StreamedUpload::Media);
        QTest::newRow("restore") << "POST /api/backup/restore HTTP/1.1" << int(StreamedUpload::BackupRestore);
        QTest::newRow("decent missing") << "POST /api/settings/decent/upload-missing HTTP/1.1" << int(StreamedUpload::None);
        QTest::newRow("visualizer missing") << "POST /api/settings/visualizer/upload-missing HTTP/1.1" << int(StreamedUpload::None);
        QTest::newRow("get upload page") << "GET /upload HTTP/1.1" << int(StreamedUpload::None);
    }
    void streamedUploadsMatchExactPaths() {
        QFETCH(QString, requestLine);
        QFETCH(int, expected);
        QCOMPARE(int(streamedUploadKind(requestLine)), expected);
    }

    void onlyAZipIsTakenForAnApk() {
        QVERIFY(startsLikeApk(QByteArrayLiteral("PK\x03\x04rest")));
        QVERIFY(!startsLikeApk(QByteArrayLiteral("{}")));
        QVERIFY(!startsLikeApk(QByteArray()));
    }

    void firstUploadNeedsAConnectedMachine() {
        Rig rig(m_dir.filePath("nomachine.db"));
        QVERIFY(rig.shotId > 0);
        rig.serial.clear();
        QCOMPARE(rig.send(), DecentShotUploader::Result::NoMachine);
        QVERIFY(rig.nam.requests.isEmpty());
    }
};

QTEST_GUILESS_MAIN(tst_DecentShotUpload)
#include "tst_decentshotupload.moc"
