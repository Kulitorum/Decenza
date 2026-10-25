#include <QtTest>

#include <QDir>
#include <QBuffer>
#include <QThreadPool>
#include <QUuid>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSettings>
#include <QTimer>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <memory>
#include "remote/screensaverservice_qml.h"

#include <cstring>

#include "remote/importservicesource.h"
#include "remote/filebundlepolicy.h"
#include "history/shotimporter.h"
#include "profile/profileimporter.h"
#include "core/personalmediapreparation.h"

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

class PendingScreensaverSource final : public ScreensaverStateSimpleSource
{
    Q_OBJECT
public:
    PendingScreensaverSource() { setReady(true); }
    int calls = 0;
    void execute(QString, QString, QString) override { ++calls; }
    QString lastImportId;
    QString lastImportClient;
    QString lastTransfer;
    QString lastCancelTarget;
    void importMedia(QString id, QString transfer, QString client) override
    { ++calls; lastImportId = id; lastTransfer = transfer; lastImportClient = client; }
    void cancelMediaImport(QString id, QString target, QString client) override
    {
        ++calls; lastCancelTarget = target; lastImportClient = client;
        emit operationCompleted(target, QStringLiteral("import-media"), false, {}, QStringLiteral("cancelled"));
        emit operationCompleted(id, QStringLiteral("cancel-media-import"), true, {}, {});
    }
};

class tst_ScreensaverMediaTickets : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void cleanup() { ScreensaverService::setPublishedNode(nullptr); }
    void joiningClientReceivesHostSleepStateAndWakeUpdates()
    {
        QStandardPaths::setTestModeEnabled(true);
        seedScreensaverSettings();
        Settings settings;
        ControlledNetworkAccessManager network(ControlledNetworkAccessManager::Behavior::Success);
        ScreensaverVideoManager manager(&network, &settings, nullptr);
        manager.setScreensaverActive(true);
        NativeScreensaverServiceSource source(&manager);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ScreensaverStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        ScreensaverService::setPublishedNode(&node);
        QQmlEngine engine;
        std::unique_ptr<ScreensaverService> service(ScreensaverServiceForeign::create(&engine, &engine));
        QTRY_VERIFY(service->ready());
        QVERIFY(service->state()->screensaverActive());
        manager.setScreensaverActive(false);
        QTRY_VERIFY(!service->state()->screensaverActive());
        manager.setScreensaverActive(true);
        QTRY_VERIFY(service->state()->screensaverActive());
        service->setNode(nullptr);
        QVERIFY(!service->ready());
        manager.setScreensaverActive(false);
        service->setNode(&node);
        QTRY_VERIFY(service->ready());
        QTRY_VERIFY(!service->state()->screensaverActive());
    }
    void admissionNotificationCanDetachFacade()
    {
        PendingScreensaverSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ScreensaverStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        ScreensaverService::setPublishedNode(&node);
        QQmlEngine engine;
        auto owner = std::unique_ptr<ScreensaverService>(ScreensaverServiceForeign::create(&engine, &engine));
        QPointer<ScreensaverService> service = owner.get();
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &ScreensaverService::completed);
        connect(service, &ScreensaverService::busyChanged, this, [&] {
            if (!service || !service->busy()) return;
            service->setNode(nullptr);
        });
        const QString id = service->nextMedia();
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QVERIFY(!completed.first().at(2).toBool());
        QVERIFY(!service->busy());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(source.calls, 0);
    }
    void sharedMediaImportUsesACompleteLeaseAndRejectsDuplicateOrInvalidContent();
    void mediaImportCancellationKeepsPhysicalAdmissionUntilCompletionDrains();
    void mediaImportDestructionRetiresStages_data();
    void mediaImportDestructionRetiresStages();
    void mediaImportRejectsCatalogChangesBeforeFinalization();
    void mediaFacadeUsesTransportIdentityAndCancelsOnDetachWithoutReplay();
    void nativeImportCancellationCanRebindFacade();
    void ticketsStayUnderManagerRootExpireAndAreConsumed();
    void ticketsRejectSymlinkEscapes();
    void backgroundImageCatalogUsesOpaqueValidatedIds();
    void personalOriginalNameSurvivesProcessingAndReload();
    void ticketRegistryIsBounded();
    void categoryRefreshCompletesAfterManagerProcessesSuccess();
    void categoryRefreshReportsCorrelatedNetworkFailure();
    void managerDestructionFailsPendingOperation();

private:
    static void isolateManagerCache(ScreensaverVideoManager& manager, const QString& path);
    static QByteArray mediaPng();
    static QString stageMedia(NativeImportServiceSource& transfer, const QByteArray& bytes,
                              const QString& name = QStringLiteral("Holiday.png"));
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

QByteArray tst_ScreensaverMediaTickets::mediaPng()
{
    QImage image(4, 3, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QString tst_ScreensaverMediaTickets::stageMedia(NativeImportServiceSource& transfer,
                                                const QByteArray& bytes, const QString& name)
{
    const auto id = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
    QSignalSpy completed(&transfer, &ImportStateSimpleSource::operationCompleted);
    transfer.execute(id(), QStringLiteral("beginTransfer"),
        {{QStringLiteral("kind"), QStringLiteral("personalMediaFile")},
         {QStringLiteral("files"), QVariantList{QVariantMap{{QStringLiteral("name"), name},
                                                         {QStringLiteral("size"), bytes.size()}}}}});
    if (completed.isEmpty() || !completed.last().at(2).toBool()) {
        QTest::qFail("media fixture could not begin a real upload", __FILE__, __LINE__); return {};
    }
    const QString ticket = completed.last().at(3).toMap().value(QStringLiteral("transferId")).toString();
    transfer.execute(id(), QStringLiteral("appendTransferChunk"),
        {{QStringLiteral("transferId"), ticket}, {QStringLiteral("fileIndex"), 0},
         {QStringLiteral("offset"), 0}, {QStringLiteral("data"), bytes}});
    if (!completed.last().at(2).toBool()) {
        QTest::qFail("media fixture could not append the upload", __FILE__, __LINE__); return {};
    }
    transfer.execute(id(), QStringLiteral("commitTransfer"), {{QStringLiteral("transferId"), ticket}});
    if (!completed.last().at(2).toBool()) {
        QTest::qFail("media fixture could not complete the upload", __FILE__, __LINE__); return {};
    }
    return ticket;
}

void tst_ScreensaverMediaTickets::sharedMediaImportUsesACompleteLeaseAndRejectsDuplicateOrInvalidContent()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    Settings settings;
    QNetworkAccessManager network;
    ScreensaverVideoManager manager(&network, &settings, nullptr);
    isolateManagerCache(manager, cache.path());
    ShotImporter shots(nullptr);
    ProfileImporter profiles(nullptr, nullptr);
    NativeImportServiceSource transfer(&shots, nullptr);
    transfer.m_profiles = &profiles;
    NativeScreensaverServiceSource source(&manager);
    source.setImportService(&transfer);
    const QString owner = QUuid::createUuid().toString(QUuid::WithoutBraces);
    source.openClient(owner);
    QSignalSpy completed(&source, &ScreensaverStateSimpleSource::operationCompleted);
    const QString originalCategory = manager.selectedCategoryId();
    const QString ticket = stageMedia(transfer, mediaPng());
    QVERIFY(!ticket.isEmpty());
    source.importMedia(QUuid::createUuid().toString(QUuid::WithoutBraces), ticket, owner);
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY2(completed.last().at(2).toBool(), qPrintable(completed.last().at(4).toString()));
    QCOMPARE(manager.personalMediaCount(), 1);
    QCOMPARE(source.personalMediaCount(), 1);
    QCOMPARE(manager.selectedCategoryId(), originalCategory);
    const QVariantMap row = manager.getPersonalMediaList().first().toMap();
    QCOMPARE(row.value(QStringLiteral("filename")).toString(), QStringLiteral("Holiday.png"));
    const QString storedPath = row.value(QStringLiteral("path")).toString();
    QCOMPARE(QImage(storedPath).size(), QSize(1280, 800));
    QFile stored(storedPath);
    QVERIFY(stored.open(QIODevice::ReadOnly));
    const QByteArray preserved = stored.readAll();
    stored.close();
    const QString token = manager.backgroundImageTokenForPath(storedPath);
    QVERIFY(!token.isEmpty());
    const QString duplicate = stageMedia(transfer, mediaPng(), QStringLiteral("HOLIDAY.PNG"));
    source.importMedia(QUuid::createUuid().toString(QUuid::WithoutBraces), duplicate, owner);
    QCOMPARE(completed.count(), 2);
    QCOMPARE(completed.last().at(4).toString(), QStringLiteral("alreadyExists"));
    QCOMPARE(manager.personalMediaCount(), 1);
    QCOMPARE(manager.backgroundImageTokenForPath(storedPath), token);
    QVERIFY(stored.open(QIODevice::ReadOnly));
    QCOMPARE(stored.readAll(), preserved);
    stored.close();
    source.importMedia(QUuid::createUuid().toString(QUuid::WithoutBraces), ticket, owner);
    QCOMPARE(completed.last().at(4).toString(), QStringLiteral("unknownTransfer"));
    const QString bad = stageMedia(transfer, QByteArrayLiteral("not an image"), QStringLiteral("invalid.png"));
    source.importMedia(QUuid::createUuid().toString(QUuid::WithoutBraces), bad, owner);
    QTRY_COMPARE(completed.count(), 4);
    QVERIFY(!completed.last().at(2).toBool());
    QCOMPARE(completed.last().at(4).toString(), QStringLiteral("invalidMedia"));
    QCOMPARE(manager.personalMediaCount(), 1);
}

void tst_ScreensaverMediaTickets::mediaImportCancellationKeepsPhysicalAdmissionUntilCompletionDrains()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();
    QTemporaryDir cache;
    Settings settings;
    QNetworkAccessManager network;
    ScreensaverVideoManager manager(&network, &settings, nullptr);
    isolateManagerCache(manager, cache.path());
    ShotImporter shots(nullptr);
    ProfileImporter profiles(nullptr, nullptr);
    NativeImportServiceSource transfer(&shots, nullptr);
    transfer.m_profiles = &profiles;
    NativeScreensaverServiceSource source(&manager);
    source.setImportService(&transfer);
    const auto id = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
    const QString owner = id();
    const QString observer = id();
    source.openClient(owner);
    source.openClient(observer);
    QSignalSpy completed(&source, &ScreensaverStateSimpleSource::operationCompleted);
    const QString request = id();
    source.importMedia(request, stageMedia(transfer, mediaPng()), owner);
    QThreadPool::globalInstance()->waitForDone();
    source.cancelMediaImport(id(), request, observer);
    QCOMPARE(completed.count(), 1);
    QVERIFY(!completed.last().at(3).toMap().value(QStringLiteral("cancelled")).toBool());
    source.cancelMediaImport(id(), request, owner);
    QCOMPARE(completed.count(), 3);
    QCOMPARE(completed.at(1).at(0).toString(), request);
    QCOMPARE(completed.at(1).at(4).toString(), QStringLiteral("cancelled"));
    const QString next = stageMedia(transfer, mediaPng(), QStringLiteral("Next.png"));
    source.importMedia(id(), next, owner);
    QCOMPARE(completed.last().at(4).toString(), QStringLiteral("busy"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(manager.personalMediaCount(), 0);
    QVERIFY(!transfer.takeCompletedTransfer(next, QStringLiteral("personalMediaFile")));
    source.importMedia(id(), stageMedia(transfer, mediaPng(), QStringLiteral("Next.png")), owner);
    QTRY_COMPARE(completed.count(), 5);
    QVERIFY(completed.last().at(2).toBool());
    QCOMPARE(manager.personalMediaCount(), 1);
    QCOMPARE(manager.getPersonalMediaList().first().toMap().value(QStringLiteral("filename")).toString(), QStringLiteral("Next.png"));
}

void tst_ScreensaverMediaTickets::mediaImportDestructionRetiresStages_data()
{
    QTest::addColumn<QString>("retire");
    QTest::newRow("client-disconnect") << QStringLiteral("client");
    QTest::newRow("source-destruction") << QStringLiteral("source");
    QTest::newRow("manager-destruction") << QStringLiteral("manager");
}

void tst_ScreensaverMediaTickets::mediaImportDestructionRetiresStages()
{
    QFETCH(QString, retire);
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();
    QTemporaryDir cache;
    Settings settings;
    QNetworkAccessManager network;
    auto manager = std::make_unique<ScreensaverVideoManager>(&network, &settings, nullptr);
    isolateManagerCache(*manager, cache.path());
    ShotImporter shots(nullptr);
    ProfileImporter profiles(nullptr, nullptr);
    NativeImportServiceSource transfer(&shots, nullptr);
    transfer.m_profiles = &profiles;
    auto source = std::make_unique<NativeScreensaverServiceSource>(manager.get());
    source->setImportService(&transfer);
    const auto id = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
    const QString owner = id();
    source->openClient(owner);
    QSignalSpy completed(source.get(), &ScreensaverStateSimpleSource::operationCompleted);
    source->importMedia(id(), stageMedia(transfer, mediaPng()), owner);
    QThreadPool::globalInstance()->waitForDone();
    const QString personalRoot = manager->personalMediaDirectory();
    QVERIFY(!QDir(personalRoot).entryList({QStringLiteral(".import-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    if (retire == QLatin1String("client")) source->closeClient(owner);
    else if (retire == QLatin1String("source")) source.reset();
    else manager.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QVERIFY(QDir(personalRoot).entryList({QStringLiteral(".import-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    if (manager) QCOMPARE(manager->personalMediaCount(), 0);
    if (source) {
        QCOMPARE(completed.count(), 1);
        QVERIFY(!completed.first().at(2).toBool());
    }
}

void tst_ScreensaverMediaTickets::mediaImportRejectsCatalogChangesBeforeFinalization()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();
    QTemporaryDir cache;
    Settings settings;
    QNetworkAccessManager network;
    ScreensaverVideoManager manager(&network, &settings, nullptr);
    isolateManagerCache(manager, cache.path());
    ShotImporter shots(nullptr);
    ProfileImporter profiles(nullptr, nullptr);
    NativeImportServiceSource transfer(&shots, nullptr);
    transfer.m_profiles = &profiles;
    NativeScreensaverServiceSource source(&manager);
    source.setImportService(&transfer);
    const QString owner = QUuid::createUuid().toString(QUuid::WithoutBraces);
    source.openClient(owner);
    QSignalSpy completed(&source, &ScreensaverStateSimpleSource::operationCompleted);
    source.importMedia(QUuid::createUuid().toString(QUuid::WithoutBraces), stageMedia(transfer, mediaPng()), owner);
    QThreadPool::globalInstance()->waitForDone();
    manager.clearPersonalMedia();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(4).toString(), QStringLiteral("mediaCatalogChanged"));
    QCOMPARE(manager.personalMediaCount(), 0);
}

void tst_ScreensaverMediaTickets::mediaFacadeUsesTransportIdentityAndCancelsOnDetachWithoutReplay()
{
    PendingScreensaverSource source;
    QRemoteObjectHost host(QUrl(QStringLiteral("tcp://127.0.0.1:0")));
    QVERIFY(host.enableRemoting<ScreensaverStateSourceAPI>(&source));
    QRemoteObjectNode node;
    const QString client = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node.setProperty("decenzaClientId", client);
    QVERIFY(node.connectToNode(host.hostUrl()));
    ScreensaverService::setPublishedNode(&node);
    QQmlEngine engine;
    std::unique_ptr<ScreensaverService> service(ScreensaverServiceForeign::create(&engine, &engine));
    QTRY_VERIFY(service->ready());
    QSignalSpy completed(service.get(), &ScreensaverService::completed);
    const QString transfer = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString request = service->importMedia(transfer);
    QTRY_COMPARE(source.lastImportId, request);
    QCOMPARE(source.lastImportClient, client);
    QCOMPARE(source.lastTransfer, transfer);
    service->setNode(nullptr);
    QTRY_COMPARE(source.lastCancelTarget, request);
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY(!completed.first().at(2).toBool());
    service->setNode(&node);
    QTRY_VERIFY(service->ready());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(source.calls, 2);
    const QString retry = service->importMedia(transfer);
    QTRY_COMPARE(source.lastImportId, retry);
    QVERIFY(retry != request);
    service.reset();
    QTRY_COMPARE(source.lastCancelTarget, retry);
}

void tst_ScreensaverMediaTickets::nativeImportCancellationCanRebindFacade()
{
    PendingScreensaverSource source, replacement;
    QRemoteObjectHost host, replacementHost;
    QVERIFY(host.setHostUrl(QUrl(QStringLiteral("screensaver-test:///cancel-origin")),
                            QRemoteObjectHost::AllowExternalRegistration));
    QVERIFY(replacementHost.setHostUrl(QUrl(QStringLiteral("screensaver-test:///cancel-replacement")),
                                       QRemoteObjectHost::AllowExternalRegistration));
    QVERIFY(host.enableRemoting<ScreensaverStateSourceAPI>(&source));
    QVERIFY(replacementHost.enableRemoting<ScreensaverStateSourceAPI>(&replacement));
    host.setProperty("decenzaClientId", QUuid::createUuid().toString(QUuid::WithoutBraces));
    replacementHost.setProperty("decenzaClientId", QUuid::createUuid().toString(QUuid::WithoutBraces));
    ScreensaverService::setPublishedNode(&host);
    QQmlEngine engine;
    auto owner = std::unique_ptr<ScreensaverService>(ScreensaverServiceForeign::create(&engine, &engine));
    const QPointer<ScreensaverService> service = owner.get();
    QTRY_VERIFY(service->ready());
    QSignalSpy completed(service, &ScreensaverService::completed);
    const QString request = service->importMedia(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QCOMPARE(source.lastImportId, request); // Qt 6.12 qremoteobjectreplica.cpp:874-891 dispatches directly.
    QString replacementRequest;
    bool reentered = false;
    QObject cancellationContext;
    connect(&source, &ScreensaverStateSimpleSource::operationCompleted, &cancellationContext,
            [&](const QString& id, const QString& operation, bool, const QVariantMap&, const QString&) {
        if (id != request || operation != QLatin1String("import-media") || reentered) return;
        reentered = true;
        ScreensaverService::setPublishedNode(&replacementHost);
        service->setNode(&replacementHost);
        replacementRequest = service->importMedia(QUuid::createUuid().toString(QUuid::WithoutBraces));
    });

    service->setNode(nullptr);
    QVERIFY(reentered);
    QCOMPARE(source.lastCancelTarget, request);
    QVERIFY(service->ready());
    QVERIFY(service->busy());
    QVERIFY(!replacementRequest.isEmpty());
    QCOMPARE(replacement.lastImportId, replacementRequest);
    QCOMPARE(replacement.lastImportClient, replacementHost.property("decenzaClientId").toString());
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(completed.first().at(0).toString(), request);
    QVERIFY(!completed.first().at(2).toBool());
    QVERIFY(service->busy()); // Retiring the old replica must not fail the new request.
    emit replacement.operationCompleted(replacementRequest, QStringLiteral("import-media"), true,
                                         {{QStringLiteral("imported"), 1}}, {});
    QTRY_COMPARE(completed.size(), 2);
    QCOMPARE(completed.last().at(0).toString(), replacementRequest);
    QVERIFY(completed.last().at(2).toBool());
    QVERIFY(!service->busy());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(source.calls, 2);
    QCOMPARE(replacement.calls, 1);
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
                                     + QLatin1Char('/')));

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
    QCOMPARE(row.value(QStringLiteral("filename")).toString(), QStringLiteral("private-background.png"));
    QVERIFY(!row.contains(QStringLiteral("path")));
    QVERIFY(!token.contains(QStringLiteral("private-background")));

    const QString resolved = manager.resolveBackgroundImageToken(token);
    QVERIFY(!resolved.isEmpty());
    QCOMPARE(manager.backgroundImageTokenForPath(resolved), token);
    QVERIFY(manager.resolveBackgroundImageToken(QString(64, QLatin1Char('0'))).isEmpty());

    const QVariantMap localRow = manager.backgroundPickerImages(true).first().toMap();
    QVERIFY(localRow.value(QStringLiteral("path")).toString().startsWith(
        QFileInfo(manager.personalMediaDirectory()).canonicalFilePath() + QLatin1Char('/')));
}

void tst_ScreensaverMediaTickets::personalOriginalNameSurvivesProcessingAndReload()
{
    QStandardPaths::setTestModeEnabled(true);
    seedScreensaverSettings();
    QTemporaryDir cache;
    QTemporaryDir input;
    QVERIFY(cache.isValid());
    QVERIFY(input.isValid());
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::blue);
    const QString processedPath = input.filePath(QStringLiteral("resized_123.png"));
    QVERIFY(image.save(processedPath));
    const QString originalName = QStringLiteral("Summer Holiday.PNG");

    Settings settings;
    QNetworkAccessManager network;
    ScreensaverVideoManager manager(&network, &settings, nullptr);
    isolateManagerCache(manager, cache.path());
    QVERIFY(manager.addPersonalMedia(processedPath, originalName));
    const QVariantMap stored = manager.getPersonalMediaList().first().toMap();
    const QString storedPath = stored.value(QStringLiteral("path")).toString();
    QCOMPARE(stored.value(QStringLiteral("filename")).toString(), originalName);
    QCOMPARE(QFileInfo(storedPath).fileName(), QStringLiteral("1_resized_123.png"));
    QVERIFY(QFile::exists(storedPath));
    QVERIFY(!QFile::exists(processedPath));
    const QString imageToken = manager.backgroundImageTokenForPath(storedPath);
    QCOMPARE(imageToken.size(), 64);
    QVERIFY(manager.hasPersonalMediaWithName(QStringLiteral("summer holiday.png")));
    QVERIFY(!manager.hasPersonalMediaWithName(QStringLiteral("resized_123.png")));

    manager.reloadPersonalMedia();
    const QVariantMap reloaded = manager.getPersonalMediaList().first().toMap();
    QCOMPARE(reloaded.value(QStringLiteral("filename")).toString(), originalName);
    QCOMPARE(reloaded.value(QStringLiteral("path")).toString(), storedPath);
    QCOMPARE(manager.backgroundImageTokenForPath(storedPath), imageToken);
    const QString duplicatePath = input.filePath(QStringLiteral("resized_456.png"));
    QVERIFY(image.save(duplicatePath));
    QVERIFY(!manager.addPersonalMedia(duplicatePath, QStringLiteral("SUMMER HOLIDAY.png")));
    QVERIFY(!QFile::exists(duplicatePath)); // Duplicate inputs retain existing cleanup semantics.
    QCOMPARE(manager.personalMediaCount(), 1);
    QVERIFY(QFile::exists(storedPath));

    // Existing catalogs lack original_name: keep their ID-prefix fallback and
    // physical path rather than requiring a migration or renaming cached files.
    QFile catalog(QDir(manager.personalMediaDirectory()).filePath(QStringLiteral("catalog.json")));
    QVERIFY(catalog.open(QIODevice::ReadOnly));
    QJsonArray legacy = QJsonDocument::fromJson(catalog.readAll()).array();
    catalog.close();
    QCOMPARE(legacy.size(), 1);
    QJsonObject legacyItem = legacy.first().toObject();
    QCOMPARE(legacyItem.value(QStringLiteral("original_name")).toString(), originalName);
    legacyItem.remove(QStringLiteral("original_name"));
    legacy.replace(0, legacyItem);
    QVERIFY(catalog.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray legacyJson = QJsonDocument(legacy).toJson();
    QCOMPARE(catalog.write(legacyJson), qint64(legacyJson.size()));
    catalog.close();
    manager.reloadPersonalMedia();
    const QVariantMap legacyRow = manager.getPersonalMediaList().first().toMap();
    QCOMPARE(legacyRow.value(QStringLiteral("filename")).toString(), QStringLiteral("resized_123.png"));
    QCOMPARE(legacyRow.value(QStringLiteral("path")).toString(), storedPath);
    QCOMPARE(manager.backgroundImageTokenForPath(storedPath), imageToken);
    QVERIFY(manager.hasPersonalMediaWithName(QStringLiteral("RESIZED_123.PNG")));
    QVERIFY(image.save(duplicatePath));
    QVERIFY(!manager.addPersonalMedia(duplicatePath, QStringLiteral("resized_123.png")));
    QCOMPARE(manager.personalMediaCount(), 1);
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
