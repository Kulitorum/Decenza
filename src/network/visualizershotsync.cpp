#include "visualizershotsync.h"

#include "core/appsettings.h"
#include "core/dbutils.h"
#include "core/diagnosticlogging.h"
#include "core/logfields.h"
#include "core/settings.h"
#include "core/settings_upload.h"
#include "core/settings_visualizer.h"
#include "history/coffeebagstorage.h"
#include "history/shothistorystorage.h"
#include "beanbaseclient.h"
#include "visualizershotlist.h"
#include "visualizersync.h"
#include "visualizeruploader.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHttpMultiPart>
#include <QMimeDatabase>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSqlQuery>

#include <limits>

namespace {
const QString kCursorKey = QStringLiteral("visualizer/pullCursor");
const QString kCursorAccountKey = QStringLiteral("visualizer/pullCursorAccount");
constexpr int kMaxListPages = 50;  // 5000 changed shots in one pass
}

VisualizerShotSync::VisualizerShotSync(VisualizerUploader* uploader, ShotHistoryStorage* shots,
                                       CoffeeBagStorage* bags, BeanBaseClient* beanbase,
                                       QNetworkAccessManager* networkManager, Settings* settings,
                                       QObject* parent)
    : QObject(parent)
    , m_uploader(uploader)
    , m_shots(shots)
    , m_bags(bags)
    , m_beanbase(beanbase)
    , m_networkManager(networkManager)
    , m_settings(settings)
{
    m_timer.setInterval(kPassIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &VisualizerShotSync::start);
    m_timer.start();
}

bool VisualizerShotSync::enabled() const
{
    return m_settings->visualizer()->visualizerActive() && m_settings->upload()->autoUpdate()
        && !m_settings->value("visualizer/username").toString().isEmpty()
        && !m_settings->value("visualizer/password").toString().isEmpty();
}

// The cursor is the newest updated_at a completed pass has seen, kept per
// account so signing in to another one starts that account afresh.
qint64 VisualizerShotSync::loadCursor() const
{
    AppSettings s;
    const QString account = m_settings->value("visualizer/username").toString();
    if (s.value(kCursorAccountKey).toString() == account && s.contains(kCursorKey))
        return s.value(kCursorKey).toLongLong();
    return QDateTime::currentSecsSinceEpoch() - kFirstPassWindowSecs;
}

void VisualizerShotSync::saveCursor(qint64 cursor) const
{
    AppSettings s;
    s.setValue(kCursorAccountKey, m_settings->value("visualizer/username").toString());
    s.setValue(kCursorKey, cursor);
}

void VisualizerShotSync::paced(std::function<void()> send)
{
    QTimer::singleShot(VisualizerUploader::kApiRequestIntervalMs, this, std::move(send));
}

void VisualizerShotSync::start()
{
    if (m_running || !enabled() || !m_shots || !m_shots->isReady())
        return;
    m_running = true;
    m_cursor = loadCursor();
    m_newestChange = m_cursor;
    m_changedIds.clear();
    m_shotQueue.clear();
    m_bagQueue.clear();
    m_shotsRead = 0;
    fetchListPage(1);
}

void VisualizerShotSync::fetchListPage(int page)
{
    // updated_after is strict (shots_controller.rb: `updated_at > ?`) and the
    // server compares sub-second times against whole seconds, so a shot changed
    // later in the cursor's own second is still returned next pass.
    const QString path = QStringLiteral("/api/shots?updated_after=%1&sort=updated_at&items=100&page=%2")
                             .arg(m_cursor).arg(page);
    QNetworkReply* reply = m_networkManager->get(m_uploader->makeApiJsonRequest(path));
    connect(reply, &QNetworkReply::finished, this, [this, reply, page]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            finishShots(false, QStringLiteral("changed-shot list: %1")
                                   .arg(m_uploader->apiErrorMessage(status, body, reply->errorString())));
            return;
        }
        using namespace VisualizerShotList;
        const PageResult result = processPage(body, page, kMaxListPages, 0, std::numeric_limits<qint64>::max());
        if (result.reason != FailReason::None) {
            finishShots(false, QStringLiteral("changed-shot list unreadable (page %1 of %2)")
                                   .arg(page).arg(result.totalPages));
            return;
        }
        for (const Entry& e : result.inWindow) {
            m_changedIds << e.visualizerId;
            m_newestChange = qMax(m_newestChange, e.updatedAtEpoch);
        }
        if (result.verdict == Verdict::Done)
            lookUpLinkedShots();
        else
            paced([this, page]() { fetchListPage(page + 1); });
    });
}

void VisualizerShotSync::lookUpLinkedShots()
{
    if (m_changedIds.isEmpty()) {
        finishShots(true, QString());
        return;
    }
    const QString dbPath = m_shots->databasePath();
    const QStringList ids = m_changedIds;
    QPointer<VisualizerShotSync> self(this);
    m_shots->runAfterQueuedWrites([self, dbPath, ids]() {
        QHash<QString, qint64> linked;
        bool ok = false;
        withTempDb(dbPath, "viz_pull_ids", [&](QSqlDatabase& db) {
            ok = ShotHistoryStorage::shotIdsForVisualizerIdsStatic(db, ids, &linked);
        });
        QMetaObject::invokeMethod(qApp, [self, ids, linked, ok]() {
            if (!self) return;
            if (!ok) {
                self->finishShots(false, QStringLiteral("linked shots unreadable"));
                return;
            }
            // Changed shots that are not linked here belong to another app or
            // device; the recovery import is how those come in.
            for (const QString& id : ids)
                if (linked.contains(id))
                    self->m_shotQueue.append({id, linked.value(id)});
            self->readNextShot();
        }, Qt::QueuedConnection);
    });
}

void VisualizerShotSync::readNextShot()
{
    if (m_shotQueue.isEmpty()) {
        finishShots(true, QString());
        return;
    }
    const ShotToRead shot = m_shotQueue.takeFirst();
    paced([this, shot]() {
        // essentials drops the chart data (shots_controller.rb, include_information).
        QNetworkReply* reply = m_networkManager->get(
            m_uploader->makeApiJsonRequest(QStringLiteral("/api/shots/%1?essentials=1").arg(shot.visualizerId)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, shot]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            if (status == 404) {
                readNextShot();  // deleted there since the list was read
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                finishShots(false, QStringLiteral("shot %1: %2").arg(DecenzaLog::field(shot.visualizerId),
                                   m_uploader->apiErrorMessage(status, body, reply->errorString())));
                return;
            }
            QJsonParseError parseError{};
            const QJsonObject remote = QJsonDocument::fromJson(body, &parseError).object();
            if (parseError.error != QJsonParseError::NoError) {
                finishShots(false, QStringLiteral("shot %1 unreadable").arg(DecenzaLog::field(shot.visualizerId)));
                return;
            }
            ++m_shotsRead;
            m_shots->requestApplyVisualizerPull(shot.shotId, VisualizerSync::remoteShotValues(remote));
            readNextShot();
        });
    });
}

void VisualizerShotSync::finishShots(bool complete, const QString& failure)
{
    if (complete) {
        // Advanced only over a complete pass: a failed one is re-read whole
        // next time, which is safe because applying a pull twice writes nothing.
        if (m_newestChange > m_cursor)
            saveCursor(m_newestChange);
        DIAG_DEBUG(VISUALIZER, "VisualizerShotSync") << "pull:" << m_changedIds.size()
                 << "shot(s) changed on Visualizer," << m_shotsRead << "linked here and read";
        m_lastShotFailure.clear();
        startBags();
    } else {
        // The bags would meet the same offline network or account refusal.
        noteFailure(&m_lastShotFailure, failure);
        endPass();
    }
}

void VisualizerShotSync::startBags()
{
    if (!m_bags) {
        endPass();
        return;
    }
    const QString dbPath = m_shots->databasePath();
    QPointer<VisualizerShotSync> self(this);
    m_shots->runAfterQueuedWrites([self, dbPath]() {
        QList<QVariantMap> bags;
        withTempDb(dbPath, "viz_pull_bags", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            if (!q.exec("SELECT id FROM coffee_bags WHERE IFNULL(visualizer_bag_id, '') != ''"))
                return;
            QList<qint64> ids;
            while (q.next())
                ids << q.value(0).toLongLong();
            for (qint64 id : ids) {
                const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, id);
                if (bag.isValid())
                    bags << bag.toVariantMap();
            }
        });
        QMetaObject::invokeMethod(qApp, [self, bags]() {
            if (!self) return;
            self->m_bagQueue = bags;
            self->m_remoteArchivedAt.clear();
            if (bags.isEmpty())
                self->endPass();
            else
                self->fetchBagListPage(1);
        }, Qt::QueuedConnection);
    });
}

void VisualizerShotSync::fetchBagListPage(int page)
{
    // The list carries every bag's archived_at, so archive state costs a page
    // per hundred bags rather than a read per bag.
    paced([this, page]() {
        QNetworkReply* reply = m_networkManager->get(
            m_uploader->makeApiJsonRequest(QStringLiteral("/api/coffee_bags?items=100&page=%1").arg(page)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, page]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            const QJsonObject root = QJsonDocument::fromJson(body).object();
            if (reply->error() != QNetworkReply::NoError || !root.value("paging").isObject()) {
                // 403 = not Premium, 429/401 = account-wide: no bag would fare better.
                noteFailure(&m_lastBagFailure, QStringLiteral("bag list: %1")
                            .arg(m_uploader->apiErrorMessage(status, body, reply->errorString())));
                endPass();
                return;
            }
            for (const QJsonValue& v : root.value("data").toArray()) {
                const QJsonObject bag = v.toObject();
                m_remoteArchivedAt.insert(bag.value("id").toString(), bag.value("archived_at").toString());
            }
            const int pages = root.value("paging").toObject().value("pages").toInt(page);
            if (page < pages && page < kMaxListPages)
                fetchBagListPage(page + 1);
            else
                applyBagArchiveState();
        });
    });
}

void VisualizerShotSync::applyBagArchiveState()
{
    QList<QVariantMap> stillInInventory;
    for (QVariantMap bag : std::as_const(m_bagQueue)) {
        const QString remoteId = bag.value(QStringLiteral("visualizerBagId")).toString();
        // Not listed: deleted there. The next shot upload re-creates and relinks it.
        if (!m_remoteArchivedAt.contains(remoteId))
            continue;
        const QVariantMap changes = VisualizerSync::bagArchivePullChanges(m_remoteArchivedAt.value(remoteId), bag);
        if (!changes.isEmpty()) {
            const qint64 bagId = bag.value(QStringLiteral("id")).toLongLong();
            if (changes.contains(QStringLiteral("inInventory"))) {
                DIAG_INFO(VISUALIZER, "VisualizerShotSync") << "bag" << bagId
                    << (changes.value(QStringLiteral("inInventory")).toBool()
                            ? "restored on Visualizer - back in inventory"
                            : "archived on Visualizer - marked finished");
            }
            m_bags->requestApplyVisualizerPull(bagId, changes);
            for (auto it = changes.cbegin(); it != changes.cend(); ++it)
                bag.insert(it.key(), it.value());
        }
        // Freezer state and blank fields matter only for a bag still in use.
        if (bag.value(QStringLiteral("inInventory")).toBool())
            stillInInventory << bag;
    }
    m_bagQueue = stillInInventory;
    readNextBag();
}

void VisualizerShotSync::readNextBag()
{
    if (m_bagQueue.isEmpty()) {
        m_lastBagFailure.clear();
        endPass();
        return;
    }
    const QVariantMap bag = m_bagQueue.takeFirst();
    paced([this, bag]() {
        const QString remoteId = bag.value(QStringLiteral("visualizerBagId")).toString();
        QNetworkReply* reply = m_networkManager->get(
            m_uploader->makeApiJsonRequest(QStringLiteral("/api/coffee_bags/") + remoteId));
        connect(reply, &QNetworkReply::finished, this, [this, reply, bag, remoteId]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            if (status == 404) {
                readNextBag();
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                noteFailure(&m_lastBagFailure, QStringLiteral("bag %1: %2").arg(DecenzaLog::field(remoteId),
                            m_uploader->apiErrorMessage(status, body, reply->errorString())));
                endPass();
                return;
            }
            const QJsonObject remote = QJsonDocument::fromJson(body).object();
            const qint64 bagId = bag.value(QStringLiteral("id")).toLongLong();
            const QVariantMap changes = VisualizerSync::bagFieldPullChanges(remote, bag);
            if (!changes.isEmpty()) {
                DIAG_INFO(VISUALIZER, "VisualizerShotSync") << "bag" << bagId << "updated from Visualizer:"
                                                            << changes.keys().join(QStringLiteral(", "));
                m_bags->requestApplyVisualizerPull(bagId, changes);
            }
            syncBagPhoto(bagId, bag.value(QStringLiteral("beanBaseId")).toString(), remoteId,
                         remote.value(QStringLiteral("image_url")).toString());
        });
    });
}

// A photo fills whichever side lacks one; neither side's photo is replaced.
void VisualizerShotSync::syncBagPhoto(qint64 bagId, const QString& beanBaseId, const QString& remoteId,
                                      const QString& remoteImageUrl)
{
    const QString key = BeanBaseClient::imageKeyFor(bagId, beanBaseId);
    const QString localPath = (m_beanbase && !key.isEmpty()) ? m_beanbase->bagImagePath(key) : QString();
    if (!m_beanbase || key.isEmpty() || (remoteImageUrl.isEmpty() == localPath.isEmpty())) {
        readNextBag();
        return;
    }
    if (!remoteImageUrl.isEmpty()) {
        // A signed link that expires in 5 minutes (openapi.yaml), so fetched now.
        m_beanbase->cacheBagImageFromUrl(key, remoteImageUrl);
        readNextBag();
        return;
    }
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(localPath, QMimeDatabase::MatchContent);
    // Visualizer takes raster images only.
    if (!mime.name().startsWith(QLatin1String("image/")) || mime.name() == QLatin1String("image/svg+xml")) {
        readNextBag();
        return;
    }
    paced([this, bagId, remoteId, localPath, mime]() {
        auto* file = new QFile(localPath);
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            readNextBag();
            return;
        }
        auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentTypeHeader, mime.name());
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"coffee_bag[image]\"; filename=\"bag.%1\"")
                           .arg(mime.preferredSuffix()));
        part.setBodyDevice(file);
        file->setParent(multiPart);
        multiPart->append(part);
        QNetworkRequest request = m_uploader->makeApiJsonRequest(QStringLiteral("/api/coffee_bags/") + remoteId);
        // Unset, so Qt writes the multipart type and boundary
        // (QNetworkAccessManagerPrivate::prepareMultipart, qnetworkaccessmanager.cpp:1786).
        request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());
        QNetworkReply* reply = m_networkManager->sendCustomRequest(request, "PATCH", multiPart);
        multiPart->setParent(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, bagId]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 200) {
                DIAG_INFO(VISUALIZER, "VisualizerShotSync") << "bag" << bagId << "photo uploaded to Visualizer";
            } else {
                noteFailure(&m_lastBagFailure, QStringLiteral("bag %1 photo: %2").arg(bagId).arg(
                            m_uploader->apiErrorMessage(status, reply->readAll(), reply->errorString())));
            }
            readNextBag();
        });
    });
}

void VisualizerShotSync::endPass()
{
    m_running = false;
    // A bag edit parked by an earlier failure goes out now, rather than waiting
    // for a shot upload that a desktop install may never make.
    m_uploader->retrySyncPendingBags();
}

void VisualizerShotSync::noteFailure(QString* last, const QString& failure)
{
    if (failure == *last) {
        DIAG_DEBUG(VISUALIZER, "VisualizerShotSync") << "pull failed again -" << failure;
        return;
    }
    *last = failure;
    DIAG_WARN(VISUALIZER, "VisualizerShotSync") << "pull from Visualizer failed -" << failure
               << "- retried in" << kPassIntervalMs / 60000 << "minutes";
}
