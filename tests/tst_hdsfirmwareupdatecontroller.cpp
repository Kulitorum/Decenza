#include <QtTest>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>

#include <cstring>
#include <utility>

#include "ble/scaledevice.h"
#include "core/hdsfirmwareupdatecontroller.h"

namespace {

QByteArray manifest(const char* version)
{
    return QByteArray("{\"model\":\"hds\",\"version\":\"") + version
        + QByteArray("\",\"releases\":[{\"model\":\"hds\",\"version\":\"") + version
        + QByteArray("\"}]}");
}

class ScriptedReply final : public QNetworkReply {
public:
    ScriptedReply(const QNetworkRequest& request, QByteArray body, QNetworkReply::NetworkError error,
                  int delayMs, QObject* parent)
        : QNetworkReply(parent)
        , m_body(std::move(body))
        , m_error(error)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(delayMs, this, [this] { finish(); });
    }

    void abort() override
    {
        if (m_finished || m_aborted)
            return;
        m_aborted = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Request canceled"));
        // QNetworkReply::abort() reports finished asynchronously in the paths the
        // controller cares about. Keeping that ordering makes stale-reply handling testable.
        QTimer::singleShot(0, this, [this] { finish(); });
    }

    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const qint64 count = qMin(maxSize, qint64(m_body.size() - m_offset));
        if (count <= 0)
            return -1;
        memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

private:
    void finish()
    {
        if (m_finished)
            return;
        m_finished = true;
        if (m_aborted) {
            emit finished();
            return;
        }
        if (m_error != QNetworkReply::NoError)
            setError(m_error, QStringLiteral("Scripted network failure"));
        else if (!m_body.isEmpty())
            emit readyRead();
        setFinished(true);
        emit finished();
    }

    QByteArray m_body;
    QNetworkReply::NetworkError m_error = QNetworkReply::NoError;
    qint64 m_offset = 0;
    bool m_aborted = false;
    bool m_finished = false;
};

class ScriptedNam final : public QNetworkAccessManager {
public:
    struct Response {
        QByteArray body;
        QNetworkReply::NetworkError error = QNetworkReply::NoError;
        int delayMs = 0;
    };

    QList<QNetworkRequest> requests;
    QList<Response> responses;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice* outgoingData) override
    {
        Q_UNUSED(operation);
        Q_UNUSED(outgoingData);
        requests.append(request);
        Q_ASSERT(!responses.isEmpty());
        const Response response = responses.takeFirst();
        return new ScriptedReply(request, response.body, response.error, response.delayMs, this);
    }
};

class FakeHdsScale final : public ScaleDevice {
public:
    explicit FakeHdsScale(QString version)
        : m_version(std::move(version))
    {
    }

    void connectToDevice(const QBluetoothDeviceInfo&) override {}
    void tare() override {}
    QString name() const override { return QStringLiteral("Half Decent Scale"); }
    QString firmwareVersion() const override { return m_version; }
    bool supportsFirmwareUpdate() const override { return true; }
    void startFirmwareUpdate(const QString& targetVersion) override {
        ++updateRequests;
        requestedVersions.append(targetVersion);
    }
    void setTestConnected(bool connected) { setConnected(connected); }

    int updateRequests = 0;
    QStringList requestedVersions;

private:
    QString m_version;
};

} // namespace

class tst_HdsFirmwareUpdateController : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void launchCheckCachesManifestAndFollowsActiveScale();
    void manifestRequestUsesSharedGithubPolicy();
    void resumeRefreshesTheCatalog();
    void cancellationIgnoresTheSupersededReply();
    void failedRefreshRetainsTheLastKnownCatalog();
    void startUpdateNamesTheResolvedRelease();
};

void tst_HdsFirmwareUpdateController::launchCheckCachesManifestAndFollowsActiveScale()
{
    ScriptedNam nam;
    nam.responses = {{manifest("3.1.14")}};
    HdsFirmwareUpdateController controller(&nam);
    FakeHdsScale scale(QStringLiteral("3.1.13"));
    scale.setTestConnected(true);
    controller.setScaleDevice(&scale);

    QTRY_COMPARE(nam.requests.size(), 1);
    QTRY_VERIFY(controller.updateAvailable());
    QCOMPARE(controller.availableVersion(), QStringLiteral("3.1.14"));

    FakeHdsScale otherScale(QStringLiteral("3.1.13"));
    otherScale.setTestConnected(true);
    controller.setScaleDevice(&otherScale);
    QCOMPARE(nam.requests.size(), 1); // The parsed manifest is a session cache.
    QVERIFY(controller.updateAvailable());

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Half Decent Scale DISCONNECTED"));
    otherScale.setTestConnected(false);
    QVERIFY(!controller.updateAvailable());
    controller.startUpdate();
    QCOMPARE(otherScale.updateRequests, 0);
}

// The whole point of the targeted flow: the release the user was shown is the
// release the scale is told to install. Sending no version is not a milder
// version of this — it starts the scale's own picker, which is the behaviour
// this replaced.
void tst_HdsFirmwareUpdateController::startUpdateNamesTheResolvedRelease()
{
    ScriptedNam nam;
    nam.responses = {{manifest("3.1.14")}};
    HdsFirmwareUpdateController controller(&nam);
    FakeHdsScale scale(QStringLiteral("3.1.13"));
    scale.setTestConnected(true);
    controller.setScaleDevice(&scale);

    QTRY_VERIFY(controller.updateAvailable());
    QVERIFY(!controller.updateStarted());

    controller.startUpdate();
    QCOMPARE(scale.updateRequests, 1);
    QCOMPARE(scale.requestedVersions, QStringList{controller.availableVersion()});
    QCOMPARE(scale.requestedVersions.first(), QStringLiteral("3.1.14"));
    QVERIFY(controller.updateStarted());

    // A second confirmation must not queue a second install; the scale refuses
    // one anyway, but it should never be asked.
    controller.startUpdate();
    QCOMPARE(scale.updateRequests, 1);
}

void tst_HdsFirmwareUpdateController::manifestRequestUsesSharedGithubPolicy()
{
    ScriptedNam nam;
    nam.responses = {{manifest("3.1.14")}};
    HdsFirmwareUpdateController controller(&nam);

    QTRY_COMPARE(nam.requests.size(), 1);
    const QNetworkRequest request = nam.requests.first();
    QCOMPARE(request.header(QNetworkRequest::UserAgentHeader).toString(), QStringLiteral("Decenza"));
    QCOMPARE(request.rawHeader("Accept"), QByteArray("application/vnd.github.v3+json"));
    QCOMPARE(request.attribute(QNetworkRequest::ConnectionCacheExpiryTimeoutSecondsAttribute).toInt(), 0);
}

void tst_HdsFirmwareUpdateController::resumeRefreshesTheCatalog()
{
    ScriptedNam nam;
    nam.responses = {{manifest("3.1.14")}, {manifest("3.1.15")}};
    HdsFirmwareUpdateController controller(&nam);
    FakeHdsScale scale(QStringLiteral("3.1.13"));
    scale.setTestConnected(true);
    controller.setScaleDevice(&scale);

    QTRY_COMPARE(controller.availableVersion(), QStringLiteral("3.1.14"));
    controller.checkForUpdates();
    QTRY_COMPARE(nam.requests.size(), 2);
    QTRY_COMPARE(controller.availableVersion(), QStringLiteral("3.1.15"));
}

void tst_HdsFirmwareUpdateController::cancellationIgnoresTheSupersededReply()
{
    ScriptedNam nam;
    nam.responses = {{manifest("3.1.14"), QNetworkReply::NoError, 100}, {manifest("3.1.15")}};
    HdsFirmwareUpdateController controller(&nam);
    FakeHdsScale scale(QStringLiteral("3.1.13"));
    scale.setTestConnected(true);
    controller.setScaleDevice(&scale);

    controller.checkForUpdates();
    QTRY_COMPARE(nam.requests.size(), 2);
    QTRY_COMPARE(controller.availableVersion(), QStringLiteral("3.1.15"));
    QTest::qWait(120);
    QCOMPARE(controller.availableVersion(), QStringLiteral("3.1.15"));
}

void tst_HdsFirmwareUpdateController::failedRefreshRetainsTheLastKnownCatalog()
{
    ScriptedNam nam;
    nam.responses = {{manifest("3.1.14")}, {{}, QNetworkReply::HostNotFoundError}};
    HdsFirmwareUpdateController controller(&nam);
    FakeHdsScale scale(QStringLiteral("3.1.13"));
    scale.setTestConnected(true);
    controller.setScaleDevice(&scale);

    QTRY_VERIFY(controller.updateAvailable());
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Manifest check failed.*"));
    controller.checkForUpdates();
    QTRY_COMPARE(nam.requests.size(), 2);
    QTRY_VERIFY(!controller.checking());
    QTRY_VERIFY(controller.updateAvailable());
    QCOMPARE(controller.availableVersion(), QStringLiteral("3.1.14"));
}

QTEST_GUILESS_MAIN(tst_HdsFirmwareUpdateController)
#include "tst_hdsfirmwareupdatecontroller.moc"
