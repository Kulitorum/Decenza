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
    m_uploader->paceApiRequest(this, std::move(send));
}

void VisualizerShotSync::start()
{
    if (m_running || !enabled() || !m_shots || !m_shots->isReady())
        return;
    m_running = true;
    m_cursor = loadCursor();
    m_newestChange = m_cursor;
    m_listCount = -1;
    m_changedIds.clear();
    m_shotQueue.clear();
    m_bagQueue.clear();
    m_shotsRead = 0;
    paced([this]() { fetchListPage(1); });
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
        // Pages are offsets into a list that moves while it is read. A row
        // added (a shot changed meanwhile) only repeats one; a row deleted there
        // shifts one past unseen, behind a cursor that would then skip it — so a
        // shrinking list ends the pass without advancing the cursor.
        const qint64 count = QJsonDocument::fromJson(body).object()
                                 .value(QStringLiteral("paging")).toObject().value(QStringLiteral("count")).toInteger(-1);
        if (page == 1)
            m_listCount = count;
        else if (count >= 0 && count < m_listCount) {
            DIAG_DEBUG(VISUALIZER, "VisualizerShotSync") << "changed-shot list shrank mid-pass - reread next pass";
            finishShots(false, QString());
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
    m_changedIds.removeDuplicates();
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
    readShot(m_shotQueue.takeFirst(), [this](const QString& failure) {
        if (!failure.isEmpty()) {
            finishShots(false, failure);
            return;
        }
        ++m_shotsRead;
        readNextShot();
    });
}

void VisualizerShotSync::readShot(const ShotToRead& shot, std::function<void(const QString&)> done)
{
    paced([this, shot, done = std::move(done)]() {
        // essentials drops the chart data (shots_controller.rb, include_information).
        QNetworkReply* reply = m_networkManager->get(
            m_uploader->makeApiJsonRequest(QStringLiteral("/api/shots/%1?essentials=1").arg(shot.visualizerId)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, shot, done]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            if (status == 404) {
                done(QString());  // deleted there
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                done(QStringLiteral("shot %1: %2").arg(DecenzaLog::field(shot.visualizerId),
                     m_uploader->apiErrorMessage(status, body, reply->errorString())));
                return;
            }
            QJsonParseError parseError{};
            const QJsonObject remote = QJsonDocument::fromJson(body, &parseError).object();
            if (parseError.error != QJsonParseError::NoError) {
                done(QStringLiteral("shot %1 unreadable").arg(DecenzaLog::field(shot.visualizerId)));
                return;
            }
            m_shots->requestApplyVisualizerPull(shot.shotId, VisualizerSync::remoteShotValues(remote));
            done(QString());
        });
    });
}

void VisualizerShotSync::refreshShot(qint64 shotId)
{
    if (shotId <= 0 || !enabled() || !m_shots || !m_shots->isReady())
        return;
    const QString dbPath = m_shots->databasePath();
    QPointer<VisualizerShotSync> self(this);
    m_shots->runAfterQueuedWrites([self, dbPath, shotId]() {
        QString visualizerId;
        withTempDb(dbPath, "viz_refresh_shot", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT IFNULL(visualizer_id, '') FROM shots WHERE id = :id"));
            q.bindValue(QStringLiteral(":id"), shotId);
            if (q.exec() && q.next())
                visualizerId = q.value(0).toString();
        });
        if (visualizerId.isEmpty())
            return;
        QMetaObject::invokeMethod(qApp, [self, visualizerId, shotId]() {
            if (!self) return;
            self->readShot({visualizerId, shotId}, [self](const QString& failure) {
                if (self && !failure.isEmpty())
                    self->noteFailure(&self->m_lastShotFailure, failure);
            });
        }, Qt::QueuedConnection);
    });
}

void VisualizerShotSync::finishShots(bool complete, const QString& failure)
{
    if (complete) {
        // Advanced only over a complete pass: anything else is re-read whole
        // next time, which is safe because applying a pull twice writes nothing.
        if (m_newestChange > m_cursor)
            saveCursor(m_newestChange);
        DIAG_DEBUG(VISUALIZER, "VisualizerShotSync") << "pull:" << m_changedIds.size()
                 << "shot(s) changed on Visualizer," << m_shotsRead << "linked here and read";
        m_lastShotFailure.clear();
    } else if (!failure.isEmpty()) {
        // The bags would meet the same offline network or account refusal.
        noteFailure(&m_lastShotFailure, failure);
        endPass();
        return;
    }
    startBags();
}

void VisualizerShotSync::refreshBags()
{
    if (m_running || !enabled() || !m_shots || !m_shots->isReady())
        return;
    m_running = true;
    startBags();
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
        QList<BagToRead> bags;
        withTempDb(dbPath, "viz_pull_bags", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            if (!q.exec("SELECT id, visualizer_bag_id, IFNULL(beanbase_id, ''), in_inventory FROM coffee_bags "
                        "WHERE IFNULL(visualizer_bag_id, '') != ''"))
                return;
            while (q.next())
                bags.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                             q.value(3).toInt() != 0});
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
    QList<BagToRead> inUse;
    for (const BagToRead& bag : std::as_const(m_bagQueue)) {
        // Not listed: deleted there. The next shot upload re-creates and relinks it.
        const auto remote = m_remoteArchivedAt.constFind(bag.visualizerBagId);
        if (remote == m_remoteArchivedAt.constEnd())
            continue;
        const QString archivedAt = *remote;
        m_bags->requestApplyVisualizerPull(bag.bagId, [archivedAt](const QVariantMap& current) {
            return VisualizerSync::bagArchivePullChanges(archivedAt, current);
        });
        // The other fields matter only for a bag still in use on both sides.
        if (bag.inInventory && archivedAt.isEmpty())
            inUse << bag;
    }
    m_bagQueue = inUse;
    readNextBag();
}

void VisualizerShotSync::readNextBag()
{
    if (m_bagQueue.isEmpty()) {
        m_lastBagFailure.clear();
        endPass();
        return;
    }
    readBag(m_bagQueue.takeFirst(), false, [this](const QString& failure) {
        if (!failure.isEmpty()) {
            noteFailure(&m_lastBagFailure, failure);
            endPass();
            return;
        }
        readNextBag();
    });
}

void VisualizerShotSync::refreshBag(qint64 bagId)
{
    if (bagId <= 0 || !enabled() || !m_shots || !m_shots->isReady() || !m_bags)
        return;
    const QString dbPath = m_shots->databasePath();
    QPointer<VisualizerShotSync> self(this);
    m_shots->runAfterQueuedWrites([self, dbPath, bagId]() {
        BagToRead bag{bagId, QString(), QString(), true};
        withTempDb(dbPath, "viz_refresh_bag", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT IFNULL(visualizer_bag_id, ''), IFNULL(beanbase_id, ''), in_inventory "
                                     "FROM coffee_bags WHERE id = :id"));
            q.bindValue(QStringLiteral(":id"), bagId);
            if (q.exec() && q.next())
                bag = {bagId, q.value(0).toString(), q.value(1).toString(), q.value(2).toInt() != 0};
        });
        if (bag.visualizerBagId.isEmpty())
            return;
        QMetaObject::invokeMethod(qApp, [self, bag]() {
            if (!self) return;
            self->readBag(bag, true, [self](const QString& failure) {
                if (self && !failure.isEmpty())
                    self->noteFailure(&self->m_lastBagFailure, failure);
            });
        }, Qt::QueuedConnection);
    });
}

void VisualizerShotSync::readBag(const BagToRead& bag, bool withArchive, std::function<void(const QString&)> done)
{
    paced([this, bag, withArchive, done = std::move(done)]() {
        QNetworkReply* reply = m_networkManager->get(
            m_uploader->makeApiJsonRequest(QStringLiteral("/api/coffee_bags/") + bag.visualizerBagId));
        connect(reply, &QNetworkReply::finished, this, [this, reply, bag, withArchive, done]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            if (status == 404) {
                done(QString());  // deleted there: the next shot upload re-creates and relinks it
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                // 403 = not Premium, 429/401 = account-wide: no other bag would fare better.
                done(QStringLiteral("bag %1: %2").arg(DecenzaLog::field(bag.visualizerBagId),
                     m_uploader->apiErrorMessage(status, body, reply->errorString())));
                return;
            }
            const QJsonObject remote = QJsonDocument::fromJson(body).object();
            if (remote.isEmpty()) {
                done(QString());
                return;
            }
            const QString archivedAt = remote.value(QStringLiteral("archived_at")).toString();
            m_bags->requestApplyVisualizerPull(bag.bagId, [remote, withArchive, archivedAt](const QVariantMap& current) {
                QVariantMap changes = withArchive ? VisualizerSync::bagArchivePullChanges(archivedAt, current)
                                                  : QVariantMap();
                const QVariantMap fields = VisualizerSync::bagFieldPullChanges(remote, current);
                for (auto it = fields.cbegin(); it != fields.cend(); ++it)
                    changes.insert(it.key(), it.value());
                return changes;
            });
            syncBagPhoto(bag, remote.value(QStringLiteral("image_url")).toString(), [done]() { done(QString()); });
        });
    });
}

// A photo fills whichever side lacks one; neither side's photo is replaced.
void VisualizerShotSync::syncBagPhoto(const BagToRead& bag, const QString& remoteImageUrl, std::function<void()> done)
{
    const QString key = BeanBaseClient::imageKeyFor(bag.bagId, bag.beanBaseId);
    const QString localPath = (m_beanbase && !key.isEmpty()) ? m_beanbase->bagImagePath(key) : QString();
    if (!m_beanbase || key.isEmpty() || (remoteImageUrl.isEmpty() == localPath.isEmpty())) {
        done();
        return;
    }
    if (!remoteImageUrl.isEmpty()) {
        // A signed link that expires in 5 minutes (openapi.yaml), so fetched now.
        m_beanbase->cacheBagImageFromUrl(key, remoteImageUrl);
        done();
        return;
    }
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(localPath, QMimeDatabase::MatchContent);
    // Visualizer takes raster images only.
    if (!mime.name().startsWith(QLatin1String("image/")) || mime.name() == QLatin1String("image/svg+xml")) {
        done();
        return;
    }
    paced([this, bag, localPath, mime, done = std::move(done)]() {
        auto* file = new QFile(localPath);
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            done();
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
        QNetworkRequest request = m_uploader->makeApiJsonRequest(QStringLiteral("/api/coffee_bags/") + bag.visualizerBagId);
        // Unset, so Qt writes the multipart type and boundary
        // (QNetworkAccessManagerPrivate::prepareMultipart, qnetworkaccessmanager.cpp:1786).
        request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());
        QNetworkReply* reply = m_networkManager->sendCustomRequest(request, "PATCH", multiPart);
        multiPart->setParent(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, bag, done]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 200) {
                DIAG_INFO(VISUALIZER, "VisualizerShotSync") << "bag" << bag.bagId << "photo uploaded to Visualizer";
            } else {
                noteFailure(&m_lastBagFailure, QStringLiteral("bag %1 photo: %2").arg(bag.bagId).arg(
                            m_uploader->apiErrorMessage(status, reply->readAll(), reply->errorString())));
            }
            done();
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
