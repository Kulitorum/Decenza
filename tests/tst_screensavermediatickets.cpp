#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSettings>
#include <QTimer>

#include <cstring>

#include "../src/core/appsettings.h"
#include "../src/core/settings.h"
#include "../src/remote/screensaverservicesource.h"
#include "../src/screensaver/screensavervideomanager.h"

namespace {

class ControlledReply final : public QNetworkReply
{
public:
    ControlledReply(const QNetworkRequest& request, const QByteArray& payload,
                    bool fail, bool hold, QObject* parent)
        : QNetworkReply(parent), m_payload(payload), m_fail(fail)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, fail ? 503 : 200);
        if (!hold)
            QTimer::singleShot(0, this, [this] { finishReply(); });
    }

    void abort() override
    {
        if (!isFinished())
            finishReply();
    }

    qint64 bytesAvailable() const override
    {
        return m_payload.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        if (m_offset >= m_payload.size())
            return -1;
        const qint64 count = qMin(maxSize, qint64(m_payload.size() - m_offset));
        memcpy(data, m_payload.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

private:
    void finishReply()
    {
        if (isFinished())
            return;
        if (m_fail)
            setError(QNetworkReply::UnknownNetworkError, QStringLiteral("synthetic network failure"));
        setFinished(true);
        if (!m_payload.isEmpty())
            emit readyRead();
        emit finished();
    }

    QByteArray m_payload;
    qint64 m_offset = 0;
    bool m_fail = false;
};

class ControlledNetworkAccessManager final : public QNetworkAccessManager
{
public:
    enum class Behavior { Success, Failure, Hold };
    explicit ControlledNetworkAccessManager(Behavior behavior, QObject* parent = nullptr)
        : QNetworkAccessManager(parent), m_behavior(behavior) {}

    int requestCount() const { return m_requestCount; }

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice* outgoingData) override
    {
        Q_UNUSED(operation);
        Q_UNUSED(outgoingData);
        ++m_requestCount;
        const bool categories = request.url().path().endsWith(QStringLiteral("/categories.json"));
        const QByteArray payload = categories
            ? QByteArrayLiteral("[{\"id\":\"espresso\",\"name\":\"Espresso\"}]")
            : QByteArrayLiteral("[]");
        return new ControlledReply(request, payload,
                                   m_behavior == Behavior::Failure,
                                   m_behavior == Behavior::Hold, this);
    }

private:
    Behavior m_behavior;
    int m_requestCount = 0;
};

void seedScreensaverSettings()
{
    AppSettings seed;
    seed.clear();
    seed.setValue(QStringLiteral("screensaver/enabled"), false);
    seed.setValue(QStringLiteral("screensaver/categoryId"), QStringLiteral("espresso"));
    seed.sync();
}

}

class tst_ScreensaverMediaTickets : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void ticketsStayUnderManagerRootExpireAndAreConsumed();
    void ticketsRejectSymlinkEscapes();
    void backgroundImageCatalogUsesOpaqueValidatedIds();
    void ticketRegistryIsBounded();
    void categoryRefreshCompletesAfterManagerProcessesSuccess();
    void categoryRefreshReportsCorrelatedNetworkFailure();
    void managerDestructionFailsPendingOperation();

private:
    static void isolateManagerCache(ScreensaverVideoManager& manager, const QString& path);
};

void tst_ScreensaverMediaTickets::isolateManagerCache(ScreensaverVideoManager& manager,
                                                      const QString& path)
{
    manager.m_cacheDir = QFileInfo(path).canonicalFilePath();
    manager.m_cacheIndex.clear();
    manager.m_catalog.clear();
    manager.m_personalCatalog.clear();
    manager.m_lastPlayedIndex = -1;
}

void tst_ScreensaverMediaTickets::ticketsStayUnderManagerRootExpireAndAreConsumed()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();

    QTemporaryDir cache;
    QTemporaryDir input;
    QVERIFY(cache.isValid());
    QVERIFY(input.isValid());
    const QString imagePath = input.filePath(QStringLiteral("sample.png"));
    QFile image(imagePath);
    QVERIFY(image.open(QIODevice::WriteOnly));
    QCOMPARE(image.write(QByteArrayLiteral("local media fixture")), qint64(19));
    image.close();

    Settings settings;
    QNetworkAccessManager network;
    ScreensaverVideoManager manager(&network, &settings, nullptr);
    isolateManagerCache(manager, cache.path());
    QVERIFY(manager.addPersonalMedia(imagePath, QStringLiteral("sample.png")));
    manager.setSelectedCategoryId(QStringLiteral("personal"));

    const QVariantMap selected = manager.getNextMediaTicket();
    const QString ticket = selected.value(QStringLiteral("ticket")).toString();
    QVERIFY(!ticket.isEmpty());
    QCOMPARE(selected.size(), 6);
    QCOMPARE(selected.value(QStringLiteral("isImage")).toBool(), true);
    QCOMPARE(selected.value(QStringLiteral("author")).metaType().id(), QMetaType::QString);
    QCOMPARE(selected.value(QStringLiteral("sourceUrl")).metaType().id(), QMetaType::QString);
    QCOMPARE(selected.value(QStringLiteral("mediaDate")).metaType().id(), QMetaType::QString);
    QVERIFY(selected.value(QStringLiteral("durationSeconds")).toInt() >= 0);

    ScreensaverMediaTicket resolved;
    QVERIFY(manager.resolveMediaTicket(ticket, &resolved));
    QCOMPARE(resolved.contentType, QStringLiteral("image/png"));
    QVERIFY(resolved.path.startsWith(QFileInfo(manager.personalMediaDirectory()).canonicalFilePath()
                                     + QDir::separator()));

    const QDateTime expiredAt = QDateTime::currentDateTimeUtc().addSecs(
        ScreensaverVideoManager::mediaTicketLifetimeSeconds() + 1);
    QVERIFY(!manager.resolveMediaTicket(ticket, &resolved, expiredAt));

    // Personal media cannot be marked corrupt, so the ticket remains usable
    // until the manager records it as played and consumes it.
    QVERIFY(!manager.markMediaTicketCorrupt(ticket));
    QVERIFY(manager.resolveMediaTicket(ticket, &resolved));
    QVERIFY(manager.markMediaTicketPlayed(ticket));
    QVERIFY(!manager.resolveMediaTicket(ticket, &resolved));
}

void tst_ScreensaverMediaTickets::ticketsRejectSymlinkEscapes()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();

    QTemporaryDir cache;
    QTemporaryDir input;
    QVERIFY(cache.isValid());
    QVERIFY(input.isValid());
    const QString imagePath = input.filePath(QStringLiteral("selected.png"));
    QFile image(imagePath);
    QVERIFY(image.open(QIODevice::WriteOnly));
    QCOMPARE(image.write(QByteArrayLiteral("selected fixture")), qint64(16));
    image.close();

    const QString outsidePath = input.filePath(QStringLiteral("outside.png"));
    QFile outside(outsidePath);
    QVERIFY(outside.open(QIODevice::WriteOnly));
    QCOMPARE(outside.write(QByteArrayLiteral("outside fixture!")), qint64(16));
    outside.close();

    Settings settings;
    QNetworkAccessManager network;
    ScreensaverVideoManager manager(&network, &settings, nullptr);
    isolateManagerCache(manager, cache.path());
    QVERIFY(manager.addPersonalMedia(imagePath, QStringLiteral("selected.png")));
    manager.setSelectedCategoryId(QStringLiteral("personal"));
    const QString ticket = manager.getNextMediaTicket().value(QStringLiteral("ticket")).toString();
    QVERIFY(!ticket.isEmpty());

    ScreensaverMediaTicket resolved;
    QVERIFY(manager.resolveMediaTicket(ticket, &resolved));
    QVERIFY(QFile::remove(resolved.path));
    QVERIFY(QFile::link(outsidePath, resolved.path));
    QVERIFY(!manager.resolveMediaTicket(ticket, &resolved));
}

void tst_ScreensaverMediaTickets::backgroundImageCatalogUsesOpaqueValidatedIds()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();

    QTemporaryDir cache;
    QTemporaryDir input;
    QVERIFY(cache.isValid());
    QVERIFY(input.isValid());
    const QString imagePath = input.filePath(QStringLiteral("private-background.png"));
    QFile image(imagePath);
    QVERIFY(image.open(QIODevice::WriteOnly));
    QCOMPARE(image.write(QByteArrayLiteral("local media fixture")), qint64(19));
    image.close();

    Settings settings;
    QNetworkAccessManager network;
    ScreensaverVideoManager manager(&network, &settings, nullptr);
    isolateManagerCache(manager, cache.path());
    QVERIFY(manager.addPersonalMedia(imagePath, QStringLiteral("private-background.png")));

    const QVariantList rows = manager.backgroundPickerImages();
    QCOMPARE(rows.size(), 1);
    const QVariantMap row = rows.first().toMap();
    const QString token = row.value(QStringLiteral("id")).toString();
    QCOMPARE(token.size(), 64);
    QCOMPARE(row.value(QStringLiteral("filename")).toString(), QStringLiteral("1_private-background.png"));
    QVERIFY(!row.contains(QStringLiteral("path")));
    QVERIFY(!token.contains(QStringLiteral("private-background")));

    const QString resolved = manager.resolveBackgroundImageToken(token);
    QVERIFY(!resolved.isEmpty());
    QCOMPARE(manager.backgroundImageTokenForPath(resolved), token);
    QVERIFY(manager.resolveBackgroundImageToken(QString(64, QLatin1Char('0'))).isEmpty());

    const QVariantMap localRow = manager.backgroundPickerImages(true).first().toMap();
    QVERIFY(localRow.value(QStringLiteral("path")).toString().startsWith(
        QFileInfo(manager.personalMediaDirectory()).canonicalFilePath() + QDir::separator()));
}

void tst_ScreensaverMediaTickets::ticketRegistryIsBounded()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();

    QTemporaryDir cache;
    QTemporaryDir input;
    QVERIFY(cache.isValid());
    QVERIFY(input.isValid());
    QFile image(input.filePath(QStringLiteral("bounded.png")));
    QVERIFY(image.open(QIODevice::WriteOnly));
    QCOMPARE(image.write(QByteArrayLiteral("local media fixture")), qint64(19));
    image.close();

    Settings settings;
    QNetworkAccessManager network;
    ScreensaverVideoManager manager(&network, &settings, nullptr);
    isolateManagerCache(manager, cache.path());
    QVERIFY(manager.addPersonalMedia(image.fileName(), QStringLiteral("bounded.png")));
    manager.setSelectedCategoryId(QStringLiteral("personal"));

    QString oldest;
    QString newest;
    for (int i = 0; i < 9; ++i) {
        const QString ticket = manager.getNextMediaTicket().value(QStringLiteral("ticket")).toString();
        QVERIFY(!ticket.isEmpty());
        if (i == 0)
            oldest = ticket;
        newest = ticket;
    }

    ScreensaverMediaTicket resolved;
    QVERIFY(!manager.resolveMediaTicket(oldest, &resolved));
    QVERIFY(manager.resolveMediaTicket(newest, &resolved));
}

void tst_ScreensaverMediaTickets::categoryRefreshCompletesAfterManagerProcessesSuccess()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();

    Settings settings;
    ControlledNetworkAccessManager network(ControlledNetworkAccessManager::Behavior::Success);
    auto manager = std::make_unique<ScreensaverVideoManager>(&network, &settings, nullptr);
    NativeScreensaverServiceSource source(manager.get());
    QSignalSpy completed(&source, &ScreensaverStateSimpleSource::operationCompleted);
    QVERIFY(completed.isValid());
    bool categoryStateWasPublishedAtCompletion = false;
    connect(&source, &ScreensaverStateSimpleSource::operationCompleted, &source,
            [&manager, &categoryStateWasPublishedAtCompletion](const QString&, const QString& operation,
                                                               bool, const QVariantMap&, const QString&) {
        if (operation == QLatin1String("refresh-categories"))
            categoryStateWasPublishedAtCompletion = manager->categories().size() == 1;
    });

    source.execute(QStringLiteral("request-success"), QStringLiteral("refresh-categories"), {});
    QCOMPARE(completed.count(), 0);
    QTRY_COMPARE(completed.count(), 1);
    QCOMPARE(network.requestCount(), 2); // category response starts the catalog refresh
    QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("request-success"));
    QCOMPARE(completed.at(0).at(1).toString(), QStringLiteral("refresh-categories"));
    QVERIFY(completed.at(0).at(2).toBool());
    QVERIFY(categoryStateWasPublishedAtCompletion);
    QVERIFY(manager->categories().size() == 1);
    QCOMPARE(manager->categories().first().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("espresso"));
}

void tst_ScreensaverMediaTickets::categoryRefreshReportsCorrelatedNetworkFailure()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();

    Settings settings;
    ControlledNetworkAccessManager network(ControlledNetworkAccessManager::Behavior::Failure);
    auto manager = std::make_unique<ScreensaverVideoManager>(&network, &settings, nullptr);
    NativeScreensaverServiceSource source(manager.get());
    QSignalSpy completed(&source, &ScreensaverStateSimpleSource::operationCompleted);
    QVERIFY(completed.isValid());

    source.execute(QStringLiteral("request-failure"), QStringLiteral("refresh-categories"), {});
    QCOMPARE(completed.count(), 0);
    QTRY_COMPARE(completed.count(), 1);
    QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("request-failure"));
    QCOMPARE(completed.at(0).at(1).toString(), QStringLiteral("refresh-categories"));
    QVERIFY(!completed.at(0).at(2).toBool());
    QVERIFY(completed.at(0).at(4).toString().contains(QStringLiteral("synthetic network failure")));
}

void tst_ScreensaverMediaTickets::managerDestructionFailsPendingOperation()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();

    Settings settings;
    ControlledNetworkAccessManager network(ControlledNetworkAccessManager::Behavior::Hold);
    auto manager = std::make_unique<ScreensaverVideoManager>(&network, &settings, nullptr);
    NativeScreensaverServiceSource source(manager.get());
    QSignalSpy completed(&source, &ScreensaverStateSimpleSource::operationCompleted);
    QVERIFY(completed.isValid());

    source.execute(QStringLiteral("request-pending"), QStringLiteral("refresh-categories"), {});
    QCOMPARE(network.requestCount(), 1);
    manager.reset();
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("request-pending"));
    QCOMPARE(completed.at(0).at(1).toString(), QStringLiteral("refresh-categories"));
    QVERIFY(!completed.at(0).at(2).toBool());
    QVERIFY(completed.at(0).at(4).toString().contains(QStringLiteral("unavailable")));
}

QTEST_GUILESS_MAIN(tst_ScreensaverMediaTickets)
#include "tst_screensavermediatickets.moc"
