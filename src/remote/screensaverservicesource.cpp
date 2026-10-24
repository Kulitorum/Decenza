#include "screensaverservicesource.h"

#include "screensaver/screensavervideomanager.h"

#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <utility>

NativeScreensaverServiceSource::NativeScreensaverServiceSource(
    ScreensaverVideoManager* manager, QObject* parent)
    : ScreensaverStateSimpleSource(parent)
    , m_manager(manager)
{
    setReady(manager != nullptr);
    if (manager) {
        const auto refresh = [this] { publishState(); };
        connect(manager, &ScreensaverVideoManager::enabledChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::screensaverTypeChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::imageDisplayDurationChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::dimPercentChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::dimDelayMinutesChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::videosShowClockChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::pipesShowClockChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::attractorShowClockChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::pipesSpeedChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::pipesCameraSpeedChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::flipClockUse3DChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::shotMapShapeChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::shotMapTextureChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::shotMapShowClockChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::shotMapShowProfilesChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::shotMapShowTerminatorChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::overlayShowWaterLevelChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::overlayShowShotPlanChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::overlayShowBatteryChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::overlayLinkButtonEnabledChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::overlayLinkButtonLabelChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::overlayLinkButtonUrlChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::showDateOnPersonalChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::selectedCategoryIdChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::catalogUpdated, this, refresh);
        connect(manager, &ScreensaverVideoManager::isDownloadingChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::downloadProgressChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::personalMediaChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::categoriesChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::isFetchingCategoriesChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::isRefreshingChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::cacheUsedBytesChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::rateLimitedChanged, this, refresh);
        connect(manager, &ScreensaverVideoManager::categoriesError, this,
                [this](const QString& error) {
                    setOperationError(error);
                    m_asyncErrors.insert(QStringLiteral("refresh-categories"), error);
                    finishAsyncOperation(QStringLiteral("refresh-categories"));
                });
        connect(manager, &ScreensaverVideoManager::catalogError, this,
                [this](const QString& error) {
                    setOperationError(error);
                    m_asyncErrors.insert(QStringLiteral("refresh-catalog"), error);
                    finishAsyncOperation(QStringLiteral("refresh-catalog"));
                });
        connect(manager, &ScreensaverVideoManager::isFetchingCategoriesChanged, this,
                [this] { if (m_manager && !m_manager->isFetchingCategories()) finishAsyncOperation(QStringLiteral("refresh-categories")); }, Qt::QueuedConnection);
        connect(manager, &ScreensaverVideoManager::isRefreshingChanged, this,
                [this] { if (m_manager && !m_manager->isRefreshing()) finishAsyncOperation(QStringLiteral("refresh-catalog")); }, Qt::QueuedConnection);
        connect(manager, &ScreensaverVideoManager::videoReady, this,
                [this](const QString&) { emit mediaAvailable(); });
        connect(manager, &QObject::destroyed, this, [this] {
            setReady(false);
            failAsyncOperations(QStringLiteral("Screensaver service is unavailable."));
        });
        publishState();
    }
}

void NativeScreensaverServiceSource::publishState()
{
    if (!m_manager)
        return;
    setEnabled(m_manager->enabled());
    setScreensaverType(m_manager->screensaverType());
    setImageDisplayDuration(m_manager->imageDisplayDuration());
    setDimPercent(m_manager->dimPercent());
    setDimDelayMinutes(m_manager->dimDelayMinutes());
    setVideosShowClock(m_manager->videosShowClock());
    setPipesShowClock(m_manager->pipesShowClock());
    setAttractorShowClock(m_manager->attractorShowClock());
    setPipesSpeed(m_manager->pipesSpeed());
    setPipesCameraSpeed(m_manager->pipesCameraSpeed());
    setFlipClockUse3D(m_manager->flipClockUse3D());
    setShotMapShape(m_manager->shotMapShape());
    setShotMapTexture(m_manager->shotMapTexture());
    setShotMapShowClock(m_manager->shotMapShowClock());
    setShotMapShowProfiles(m_manager->shotMapShowProfiles());
    setShotMapShowTerminator(m_manager->shotMapShowTerminator());
    setOverlayShowWaterLevel(m_manager->overlayShowWaterLevel());
    setOverlayShowShotPlan(m_manager->overlayShowShotPlan());
    setOverlayShowBattery(m_manager->overlayShowBattery());
    setOverlayLinkButtonEnabled(m_manager->overlayLinkButtonEnabled());
    setOverlayLinkButtonLabel(m_manager->overlayLinkButtonLabel());
    setOverlayLinkButtonUrl(m_manager->overlayLinkButtonUrl());
    setShowDateOnPersonal(m_manager->showDateOnPersonal());
    setIsPersonalCategory(m_manager->isPersonalCategory());
    setItemCount(m_manager->itemCount());
    setIsDownloading(m_manager->isDownloading());
    setDownloadProgress(m_manager->downloadProgress());
    setPersonalMediaCount(m_manager->personalMediaCount());
    setCategories(m_manager->categories());
    setSelectedCategoryId(m_manager->selectedCategoryId());
    setSelectedCategoryName(m_manager->selectedCategoryName());
    setIsFetchingCategories(m_manager->isFetchingCategories());
    setIsRefreshing(m_manager->isRefreshing());
    setCacheUsedBytes(m_manager->cacheUsedBytes());
    setIsRateLimited(m_manager->isRateLimited());
    setRateLimitMinutesRemaining(m_manager->rateLimitMinutesRemaining());
}

void NativeScreensaverServiceSource::execute(QString requestId, QString operation,
                                             QString ticket)
{
    const auto started = m_requests.begin(requestId);
    if (started != ServiceRequestIds::Start::Accepted) {
        // A duplicate ID must not complete the original caller's operation.
        // Invalid IDs are ignored; the shared facade always supplies UUIDs.
        if (started == ServiceRequestIds::Start::LimitReached)
            emit operationCompleted(requestId, operation, false, {},
                                    QStringLiteral("Screensaver service is busy."));
        return;
    }

    if (!m_manager) {
        finish(requestId, operation, false, {}, QStringLiteral("Screensaver service is unavailable."));
        return;
    }

    if (operation == QStringLiteral("next-media")) {
        if (!ticket.isEmpty()) {
            finish(requestId, operation, false, {}, QStringLiteral("Unexpected media ticket."));
            return;
        }
        const QVariantMap media = m_manager->getNextMediaTicket();
        if (media.isEmpty()) {
            finish(requestId, operation, false, {}, QStringLiteral("No cached screensaver media is available."));
            return;
        }
        finish(requestId, operation, true, media);
        return;
    }

    if (operation == QStringLiteral("set-type")) {
        if (!m_manager->availableScreensaverTypes().contains(ticket)) {
            finish(requestId, operation, false, {}, QStringLiteral("Invalid screensaver type."));
            return;
        }
        m_manager->setScreensaverType(ticket);
        publishState();
        finish(requestId, operation, true);
        return;
    }

    if (ticket.isEmpty() && operation == QStringLiteral("refresh-shot-map")) {
        if (m_shotMapReply) {
            m_asyncRequests.insert(operation, requestId);
            return;
        }
        auto* network = m_manager->networkAccessManager();
        if (!network) {
            finish(requestId, operation, false, {}, QStringLiteral("Screensaver network service is unavailable."));
            return;
        }
        QNetworkRequest request(QUrl(QStringLiteral("https://decenza.coffee/api/shots-latest.json")));
        request.setTransferTimeout(15000);
        m_asyncRequests.insert(operation, requestId);
        m_asyncErrors.remove(operation);
        m_shotMapPayload.clear();
        auto* reply = network->get(request);
        m_shotMapReply = reply;
        const auto contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (contentLength > 2 * 1024 * 1024)
            reply->abort();
        connect(reply, &QIODevice::readyRead, this, [this, reply] {
            m_shotMapPayload.append(reply->readAll());
            if (m_shotMapPayload.size() > 2 * 1024 * 1024)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, operation] {
            if (m_shotMapReply != reply)
                return;
            m_shotMapReply = nullptr;
            const QByteArray payload = std::exchange(m_shotMapPayload, {});
            const auto error = reply->error();
            const QString errorText = reply->errorString();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reply->deleteLater();
            if (error != QNetworkReply::NoError || status < 200 || status >= 300 || payload.size() > 2 * 1024 * 1024) {
                m_asyncErrors.insert(operation, errorText.isEmpty()
                    ? QStringLiteral("Shot map request failed (HTTP %1). ").arg(status)
                    : errorText);
                setOperationError(m_asyncErrors.value(operation));
                finishAsyncOperation(operation);
                return;
            }
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                m_asyncErrors.insert(operation, QStringLiteral("Shot map response is invalid."));
                setOperationError(m_asyncErrors.value(operation));
                finishAsyncOperation(operation);
                return;
            }
            const QJsonObject object = document.object();
            const QJsonValue shots = object.value(QStringLiteral("shots"));
            const QJsonValue profiles = object.value(QStringLiteral("top_profiles"));
            if (!shots.isArray() || !profiles.isArray() || shots.toArray().size() > 10000
                || profiles.toArray().size() > 1000) {
                m_asyncErrors.insert(operation, QStringLiteral("Shot map response has an invalid shape."));
                setOperationError(m_asyncErrors.value(operation));
                finishAsyncOperation(operation);
                return;
            }
            setShotMapData(object.toVariantMap());
            m_asyncErrors.remove(operation);
            setOperationError({});
            finishAsyncOperation(operation);
        });
        return;
    }

    if (ticket.isEmpty() && operation == QStringLiteral("refresh-categories")) {
        setOperationError({});
        m_asyncErrors.remove(operation);
        m_asyncRequests.insert(operation, requestId);
        m_manager->refreshCategories();
        if (!m_manager->isFetchingCategories()) {
            m_asyncRequests.remove(operation, requestId);
            finish(requestId, operation, false, {}, QStringLiteral("Categories request did not start."));
        }
        return;
    }
    if (ticket.isEmpty() && operation == QStringLiteral("refresh-catalog")) {
        setOperationError({});
        m_asyncErrors.remove(operation);
        m_asyncRequests.insert(operation, requestId);
        m_manager->refreshCatalog();
        if (!m_manager->isRefreshing()) {
            m_asyncRequests.remove(operation, requestId);
            finish(requestId, operation, false, {}, QStringLiteral("Catalog request did not start."));
        }
        return;
    }
    if (ticket.isEmpty() && operation == QStringLiteral("clear-cache")) {
        if (m_manager->isRateLimited()) {
            finish(requestId, operation, false, {}, QStringLiteral("Screensaver video cache is rate limited."));
            return;
        }
        m_manager->clearCacheWithRateLimit();
        publishState();
        finish(requestId, operation, true);
        return;
    }
    if (ticket.isEmpty() && operation == QStringLiteral("clear-personal")) {
        m_manager->clearPersonalMedia();
        publishState();
        finish(requestId, operation, true);
        return;
    }

    if (ticket.size() != 36) {
        finish(requestId, operation, false, {}, QStringLiteral("Invalid media ticket."));
        return;
    }
    if (operation == QStringLiteral("mark-played")) {
        const bool ok = m_manager->markMediaTicketPlayed(ticket);
        finish(requestId, operation, ok, {}, ok ? QString() : QStringLiteral("Media ticket is no longer valid."));
        return;
    }
    if (operation == QStringLiteral("mark-corrupt")) {
        const bool ok = m_manager->markMediaTicketCorrupt(ticket);
        finish(requestId, operation, ok, {}, ok ? QString() : QStringLiteral("Media ticket cannot be marked corrupt."));
        return;
    }

    finish(requestId, operation, false, {}, QStringLiteral("Unsupported screensaver operation."));
}

void NativeScreensaverServiceSource::finish(const QString& requestId, const QString& operation,
                                             bool success, const QVariantMap& result,
                                             const QString& reason)
{
    m_requests.finish(requestId);
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeScreensaverServiceSource::finishAsyncOperation(const QString& operation)
{
    if (m_asyncRequests.values(operation).isEmpty())
        return;
    const bool stillRunning = operation == QLatin1String("refresh-categories")
            ? m_manager && m_manager->isFetchingCategories()
            : operation == QLatin1String("refresh-catalog")
                ? m_manager && m_manager->isRefreshing()
                : operation == QLatin1String("refresh-shot-map") && !m_shotMapReply.isNull();
    if (stillRunning)
        return;

    const QString error = m_asyncErrors.take(operation);
    const auto requestIds = m_asyncRequests.values(operation);
    m_asyncRequests.remove(operation);
    for (const QString& requestId : requestIds)
        finish(requestId, operation, error.isEmpty(), {}, error);
}

void NativeScreensaverServiceSource::failAsyncOperations(const QString& reason)
{
    const auto operations = m_asyncRequests.keys();
    for (const QString& operation : operations) {
        const auto requestIds = m_asyncRequests.values(operation);
        m_asyncRequests.remove(operation);
        m_asyncErrors.remove(operation);
        for (const QString& requestId : requestIds)
            finish(requestId, operation, false, {}, reason);
    }
    if (m_shotMapReply)
        m_shotMapReply->abort();
}
