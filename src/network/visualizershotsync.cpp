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
    if (s.value(kCursorAccountKey).toString() == m_passAccount && s.contains(kCursorKey))
        return s.value(kCursorKey).toLongLong();
    return QDateTime::currentSecsSinceEpoch() - kFirstPassWindowSecs;
}

void VisualizerShotSync::saveCursor(qint64 cursor) const
{
    // A pass that outlived its account (the user signed in to another one
    // meanwhile) must not hand its cursor to the new account.
    if (m_settings->value("visualizer/username").toString() != m_passAccount) {
        DIAG_DEBUG(VISUALIZER, "VisualizerShotSync") << "account changed during the pass - its cursor is dropped";
        return;
    }
    AppSettings s;
    s.setValue(kCursorAccountKey, m_passAccount);
    s.setValue(kCursorKey, cursor);
}

void VisualizerShotSync::paced(std::function<void()> send)
{
    m_uploader->paceApiRequest(this, std::move(send));
}

bool VisualizerShotSync::isAccountWideFailure(int status)
{
    // No other item would fare better: offline, signed out, refused, rate
    // limited, or the server failing.
    return status == 0 || status == 401 || status == 403 || status == 429 || status >= 500;
}

void VisualizerShotSync::start()
{
    if (m_running || !enabled() || !m_shots || !m_shots->isReady())
        return;
    m_running = true;
    m_passAccount = m_settings->value("visualizer/username").toString();
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
        switch (result.reason) {
        case FailReason::None:
            break;
        case FailReason::ParseError:
            finishShots(false, QStringLiteral("changed-shot list unreadable: %1").arg(result.parseError));
            return;
        case FailReason::MissingPaging:
            finishShots(false, QStringLiteral("changed-shot list came back without paging (an error page?)"));
            return;
        case FailReason::PageCeiling:
            // Newest first, so the cursor cannot advance part-way: past the
            // ceiling a pass would read 50 pages and fail, forever. Start the
            // account over at the first-pass window instead.
            DIAG_WARN(VISUALIZER, "VisualizerShotSync") << "over" << kMaxListPages * 100
                << "shots changed on Visualizer since the last pass - pulling only the last"
                << kFirstPassWindowSecs / 86400 << "days";
            saveCursor(QDateTime::currentSecsSinceEpoch() - kFirstPassWindowSecs);
            endPass();
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
    readShot(m_shotQueue.takeFirst(), [this](const QString& failure, bool accountWide) {
        if (!failure.isEmpty() && accountWide) {
            finishShots(false, failure);
            return;
        }
        if (!failure.isEmpty())
            DIAG_WARN(VISUALIZER, "VisualizerShotSync") << "pull skipped a shot -" << failure;
        else
            ++m_shotsRead;
        readNextShot();
    });
}

void VisualizerShotSync::readShot(const ShotToRead& shot, std::function<void(const QString&, bool)> done)
{
    paced([this, shot, done = std::move(done)]() {
        const quint64 generation = m_uploader->shotPushGeneration(shot.shotId);
        // essentials drops the chart data (shots_controller.rb, include_information).
        QNetworkReply* reply = m_networkManager->get(
            m_uploader->makeApiJsonRequest(QStringLiteral("/api/shots/%1?essentials=1").arg(shot.visualizerId)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, shot, done, generation]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            if (status == 404) {
                DIAG_DEBUG(VISUALIZER, "VisualizerShotSync") << "shot" << shot.shotId << "is gone from Visualizer";
                done(QString(), false);
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                done(QStringLiteral("shot %1: %2").arg(DecenzaLog::field(shot.visualizerId),
                     m_uploader->apiErrorMessage(status, body, reply->errorString())),
                     isAccountWideFailure(status));
                return;
            }
            QJsonParseError parseError{};
            const QJsonObject remote = QJsonDocument::fromJson(body, &parseError).object();
            if (parseError.error != QJsonParseError::NoError) {
                done(QStringLiteral("shot %1 unreadable").arg(DecenzaLog::field(shot.visualizerId)), false);
                return;
            }
            if (m_uploader->shotPushGeneration(shot.shotId) != generation) {
                // Sent from here since the read began: the read may predate it,
                // and our own change comes back on the next pass anyway.
                done(QString(), false);
                return;
            }
            m_shots->requestApplyVisualizerPull(shot.shotId, VisualizerSync::remoteShotValues(remote),
                                                [done](bool ok) {
                // Not saved: the pass fails, so the cursor stays and it is read again.
                done(ok ? QString() : QStringLiteral("changes not saved here"), !ok);
            });
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
        QString error;
        withTempDb(dbPath, "viz_refresh_shot", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT IFNULL(visualizer_id, '') FROM shots WHERE id = :id"));
            q.bindValue(QStringLiteral(":id"), shotId);
            if (!q.exec())
                error = q.lastError().text();
            else if (q.next())
                visualizerId = q.value(0).toString();
        });
        QMetaObject::invokeMethod(qApp, [self, visualizerId, shotId, error]() {
            if (!self) return;
            if (!error.isEmpty()) {
                DIAG_WARN(VISUALIZER, "VisualizerShotSync") << "refresh of shot" << shotId << "failed:" << error;
                return;
            }
            if (visualizerId.isEmpty())
                return;
            self->readShot({visualizerId, shotId}, [self](const QString& failure, bool) {
                if (self && !failure.isEmpty())
                    self->noteFailure(&self->m_shotFailure, failure);
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
        noteRecovered(&m_shotFailure);
    } else if (!failure.isEmpty()) {
        // The bags would meet the same offline network or account refusal.
        noteFailure(&m_shotFailure, failure);
        endPass();
        return;
    }
    startBags();
}

void VisualizerShotSync::refreshBags()
{
    if (m_running || !enabled() || !m_shots || !m_shots->isReady())
        return;
    // A screen opened again and again re-reads the bags at most this often:
    // each pass costs a request per bag in use, against the rate budget that
    // shot uploads share.
    if (m_lastBagPass.isValid() && m_lastBagPass.elapsed() < kBagRefreshMinIntervalMs)
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
        QString error;
        withTempDb(dbPath, "viz_pull_bags", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            if (!q.exec("SELECT id, visualizer_bag_id, IFNULL(beanbase_id, ''), in_inventory FROM coffee_bags "
                        "WHERE IFNULL(visualizer_bag_id, '') != ''")) {
                error = q.lastError().text();
                return;
            }
            while (q.next())
                bags.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                             q.value(3).toInt() != 0});
        });
        QMetaObject::invokeMethod(qApp, [self, bags, error]() {
            if (!self) return;
            if (!error.isEmpty()) {
                self->noteFailure(&self->m_bagFailure, QStringLiteral("synced bags unreadable: %1").arg(error));
                self->endPass();
                return;
            }
            self->m_bagQueue = bags;
            self->m_remoteArchivedAt.clear();
            self->m_bagGenerationAtList.clear();
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
        if (page == 1) {
            for (const BagToRead& bag : std::as_const(m_bagQueue))
                m_bagGenerationAtList.insert(bag.bagId, m_uploader->bagPushGeneration(bag.bagId));
        }
        QNetworkReply* reply = m_networkManager->get(
            m_uploader->makeApiJsonRequest(QStringLiteral("/api/coffee_bags?items=100&page=%1").arg(page)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, page]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                noteFailure(&m_bagFailure, QStringLiteral("bag list: %1")
                            .arg(m_uploader->apiErrorMessage(status, body, reply->errorString())));
                endPass();
                return;
            }
            const QJsonObject root = QJsonDocument::fromJson(body).object();
            if (!root.value("paging").isObject()) {
                noteFailure(&m_bagFailure, QStringLiteral("bag list unreadable (no paging - an error page?)"));
                endPass();
                return;
            }
            for (const QJsonValue& v : root.value("data").toArray()) {
                const QJsonObject bag = v.toObject();
                // A list entry without archived_at says nothing about the archive.
                if (bag.contains(QStringLiteral("archived_at")))
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
        const auto remote = m_remoteArchivedAt.constFind(bag.visualizerBagId);
        if (remote == m_remoteArchivedAt.constEnd()) {
            DIAG_DEBUG(VISUALIZER, "VisualizerShotSync") << "bag" << bag.bagId
                     << "not in Visualizer's list - the next shot upload re-creates it";
            continue;
        }
        const QString archivedAt = *remote;
        if (m_uploader->bagPushGeneration(bag.bagId) == m_bagGenerationAtList.value(bag.bagId)) {
            m_bags->requestApplyVisualizerPull(bag.bagId, [archivedAt](const QVariantMap& current) {
                return VisualizerSync::bagArchivePullChanges(archivedAt, current);
            });
        }
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
        noteRecovered(&m_bagFailure);
        m_lastBagPass.start();
        endPass();
        return;
    }
    readBag(m_bagQueue.takeFirst(), false, [this](const QString& failure, bool accountWide) {
        if (!failure.isEmpty() && accountWide) {
            noteFailure(&m_bagFailure, failure);
            endPass();
            return;
        }
        if (!failure.isEmpty())
            DIAG_WARN(VISUALIZER, "VisualizerShotSync") << "pull skipped a bag -" << failure;
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
        QString error;
        withTempDb(dbPath, "viz_refresh_bag", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT IFNULL(visualizer_bag_id, ''), IFNULL(beanbase_id, ''), in_inventory "
                                     "FROM coffee_bags WHERE id = :id"));
            q.bindValue(QStringLiteral(":id"), bagId);
            if (!q.exec())
                error = q.lastError().text();
            else if (q.next())
                bag = {bagId, q.value(0).toString(), q.value(1).toString(), q.value(2).toInt() != 0};
        });
        QMetaObject::invokeMethod(qApp, [self, bag, error]() {
            if (!self) return;
            if (!error.isEmpty()) {
                DIAG_WARN(VISUALIZER, "VisualizerShotSync") << "refresh of bag" << bag.bagId << "failed:" << error;
                return;
            }
            if (bag.visualizerBagId.isEmpty())
                return;
            self->readBag(bag, true, [self](const QString& failure, bool) {
                if (self && !failure.isEmpty())
                    self->noteFailure(&self->m_bagFailure, failure);
            });
        }, Qt::QueuedConnection);
    });
}

void VisualizerShotSync::readBag(const BagToRead& bag, bool withArchive,
                                 std::function<void(const QString&, bool)> done)
{
    paced([this, bag, withArchive, done = std::move(done)]() {
        const quint64 generation = m_uploader->bagPushGeneration(bag.bagId);
        QNetworkReply* reply = m_networkManager->get(
            m_uploader->makeApiJsonRequest(QStringLiteral("/api/coffee_bags/") + bag.visualizerBagId));
        connect(reply, &QNetworkReply::finished, this, [this, reply, bag, withArchive, done, generation]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            if (status == 404) {
                DIAG_DEBUG(VISUALIZER, "VisualizerShotSync") << "bag" << bag.bagId
                         << "is gone from Visualizer - the next shot upload re-creates it";
                done(QString(), false);
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                done(QStringLiteral("bag %1: %2").arg(DecenzaLog::field(bag.visualizerBagId),
                     m_uploader->apiErrorMessage(status, body, reply->errorString())),
                     isAccountWideFailure(status));
                return;
            }
            QJsonParseError parseError{};
            const QJsonObject remote = QJsonDocument::fromJson(body, &parseError).object();
            if (parseError.error != QJsonParseError::NoError || remote.isEmpty()) {
                done(QStringLiteral("bag %1 unreadable").arg(DecenzaLog::field(bag.visualizerBagId)), false);
                return;
            }
            if (m_uploader->bagPushGeneration(bag.bagId) != generation) {
                done(QString(), false);  // pushed from here since the read began
                return;
            }
            const bool archiveKnown = withArchive && remote.contains(QStringLiteral("archived_at"));
            const QString archivedAt = remote.value(QStringLiteral("archived_at")).toString();
            m_bags->requestApplyVisualizerPull(bag.bagId, [remote, archiveKnown, archivedAt](const QVariantMap& current) {
                VisualizerSync::BagPull pull = VisualizerSync::bagFieldPullChanges(remote, current);
                if (archiveKnown)
                    pull.add(VisualizerSync::bagArchivePullChanges(archivedAt, current));
                return pull;
            });
            syncBagPhoto(bag, remote.value(QStringLiteral("image_url")).toString(), [done]() { done(QString(), false); });
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
    if (m_photoUploadRefused) {
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
            DIAG_DEBUG(VISUALIZER, "VisualizerShotSync") << "bag" << bag.bagId << "photo unreadable:" << file->errorString();
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
                noteRecovered(&m_photoFailure);
            } else {
                // 403: bag writes need Premium. Not retried this session.
                if (status == 403)
                    m_photoUploadRefused = true;
                noteFailure(&m_photoFailure, QStringLiteral("photo upload for bag %1 refused: %2").arg(bag.bagId).arg(
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

// A failure is reported once; repeats are counted, and the count goes out with
// the recovery line, so the log tells the whole episode at INFO and above.
void VisualizerShotSync::noteFailure(Failure* failure, const QString& message)
{
    if (message == failure->message) {
        ++failure->repeats;
        return;
    }
    noteRecovered(failure);
    failure->message = message;
    failure->repeats = 0;
    DIAG_WARN(VISUALIZER, "VisualizerShotSync") << "Visualizer sync failed -" << message
               << "- retried at the next pass, every" << kPassIntervalMs / 60000 << "minutes";
}

void VisualizerShotSync::noteRecovered(Failure* failure)
{
    if (failure->message.isEmpty())
        return;
    DIAG_INFO(VISUALIZER, "VisualizerShotSync") << "Visualizer sync recovered from:" << failure->message
              << "(" << failure->repeats << "repeat(s))";
    failure->message.clear();
    failure->repeats = 0;
}
