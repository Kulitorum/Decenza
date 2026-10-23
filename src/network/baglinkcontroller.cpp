#include "baglinkcontroller.h"

#include "core/settings_dye.h"
#include "history/coffeebagstorage.h"
#include "network/beanbaseclient.h"
#include "network/beanbase_blob.h"
#include "network/visualizershotsync.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <utility>

BagLinkController::BagLinkController(CoffeeBagStorage* storage, BeanBaseClient* beanBase,
                                     SettingsDye* dye, VisualizerShotSync* visualizer,
                                     QObject* parent)
    : QObject(parent), m_storage(storage), m_beanBase(beanBase), m_dye(dye),
      m_visualizer(visualizer)
{
    if (!m_beanBase)
        return;
    connect(m_beanBase, &BeanBaseClient::bagLinkRecoveryFinished, this,
        [this](const QString& id, const QString& link) {
            if (link.isEmpty())
                clearWaiters(m_recoveryWaiters, id);
            else
                finishWaiters(m_recoveryWaiters, id, link, Outcome::Recover);
        });
    connect(m_beanBase, &BeanBaseClient::bagLinkResolved, this,
        [this](const QString& id, const QString& link) {
            finishWaiters(m_validationWaiters, id, link, Outcome::Resolve);
        });
    connect(m_beanBase, &BeanBaseClient::bagLinkArchived, this,
        [this](const QString& id, const QString& link) {
            finishWaiters(m_validationWaiters, id, link, Outcome::Archive);
            finishWaiters(m_archiveWaiters, id, link, Outcome::Archive);
        });
    connect(m_beanBase, &BeanBaseClient::bagLinkDead, this,
        [this](const QString& id) {
            finishWaiters(m_validationWaiters, id, QString(), Outcome::Dead);
        });
    connect(m_beanBase, &BeanBaseClient::bagLinkValidationFinished, this,
        [this](const QString& id) { clearWaiters(m_validationWaiters, id); });
    connect(m_beanBase, &BeanBaseClient::bagArchiveLookupFinished, this,
        [this](const QString& id, const QString& snapshot, bool) {
            // A manual archive lookup has no negative verdict. Its completion
            // only releases the bounded waiter when there is no snapshot.
            if (snapshot.isEmpty())
                finishWaiters(m_archiveWaiters, id, QString(), Outcome::Archive);
        });
}

void BagLinkController::inspectBag(qint64 bagId, const QString& requestId)
{
    if (!m_storage || bagId <= 0 || requestId.isEmpty()) {
        emit inspectionFinished(requestId, false, QStringLiteral("disconnected"));
        return;
    }
    m_storage->requestBagWithCompletion(bagId, this,
        [this, requestId](qint64 id, bool success, const QVariantMap& bag,
                          const QString& reason) {
            if (!success) {
                emit inspectionFinished(requestId, false,
                    reason.isEmpty() ? QStringLiteral("readFailed") : reason);
                return;
            }
            const QString failure = inspectRow(id, bag);
            emit inspectionFinished(requestId, failure.isEmpty(), failure);
        });
}

bool BagLinkController::refreshVisualizerBags() const
{
    if (!m_visualizer)
        return false;
    m_visualizer->refreshBags();
    return true;
}

QString BagLinkController::inspectRow(qint64 bagId, const QVariantMap& bag)
{
    if (!m_beanBase)
        return QStringLiteral("disconnected");
    if (m_pendingWrites.contains(bagId))
        return {};
    const QString rawBlob = bag.value(QStringLiteral("beanBaseData")).toString();
    const QJsonDocument document = QJsonDocument::fromJson(rawBlob.toUtf8());
    const QJsonObject blob = document.isObject() ? document.object() : QJsonObject();
    const QString canonicalId = bag.value(QStringLiteral("beanBaseId")).toString();
    const QString link = blob.value(QStringLiteral("link")).toString();
    const bool linkDead = blob.value(QStringLiteral("linkDead")).toBool();
    const bool linkChecked = blob.value(QStringLiteral("linkChecked")).toBool();
    const QString linkKey = BeanBaseClient::imageKeyFor(bagId, canonicalId);
    const QString coffeeName = bag.value(QStringLiteral("coffeeName")).toString();

    if (!canonicalId.isEmpty() && link.isEmpty() && !linkDead) {
        Waiter waiter{bagId, rawBlob, {}, canonicalId, coffeeName, linkKey,
                      canonicalId, Outcome::Recover};
        const QString recovered = m_recoveredLinks.value(canonicalId);
        if (!recovered.isEmpty())
            apply(waiter, recovered, Outcome::Recover);
        else {
            if (!m_recoveryWaiters.contains(canonicalId)
                && m_recoveredLinks.size() >= kMaximumRecoveredLinks)
                return QStringLiteral("tooManyCachedRecoveries");
            const auto admission = addWaiter(m_recoveryWaiters, canonicalId, waiter);
            if (admission == WaiterAdmission::Rejected)
                return QStringLiteral("tooManyPendingLinks");
            if (admission == WaiterAdmission::Added)
                m_beanBase->recoverBagLink(canonicalId, coffeeName);
        }
    }

    if (!linkKey.isEmpty() && !link.isEmpty() && !linkChecked && !linkDead
        && !BeanBaseClient::isArchiveUrl(link)) {
        const QString token = requestKey(bagId, linkKey, link);
        Waiter waiter{bagId, rawBlob, link, canonicalId, coffeeName, linkKey,
                      token, Outcome::Resolve};
        const auto admission = addWaiter(m_validationWaiters, token, waiter);
        if (admission == WaiterAdmission::Rejected)
            return QStringLiteral("tooManyPendingLinks");
        if (admission == WaiterAdmission::Added)
            m_beanBase->validateBagLink(token, link);
    }

    const bool active = m_dye && m_dye->activeBagId() == bagId;
    if (active && linkDead) {
        QString archivedTarget = link;
        const QJsonObject canonical = blob.value(QStringLiteral("canonical")).toObject();
        if (archivedTarget.isEmpty())
            archivedTarget = canonical.value(QStringLiteral("link")).toString();
        if (!archivedTarget.isEmpty()) {
            const QString token = requestKey(bagId, linkKey, archivedTarget);
            Waiter waiter{bagId, rawBlob, archivedTarget, canonicalId, coffeeName,
                          linkKey, token, Outcome::Archive};
            waiter.activeRequired = true;
            const auto admission = addWaiter(m_archiveWaiters, token, waiter);
            if (admission == WaiterAdmission::Rejected)
                return QStringLiteral("tooManyPendingLinks");
            if (admission == WaiterAdmission::Added)
                m_beanBase->lookupArchivedLink(token, archivedTarget);
        }
    }
    return {};
}

BagLinkController::WaiterAdmission BagLinkController::addWaiter(
    QHash<QString, QVector<Waiter>>& table, const QString& key, const Waiter& waiter)
{
    if (key.isEmpty())
        return WaiterAdmission::Rejected;
    auto it = table.find(key);
    if (it != table.end()) {
        for (const Waiter& existing : std::as_const(it.value())) {
            if (existing.bagId == waiter.bagId && existing.expectedBlob == waiter.expectedBlob
                && existing.expectedLink == waiter.expectedLink && existing.outcome == waiter.outcome)
                return WaiterAdmission::Existing;
        }
        if (it->size() >= kMaximumWaitersPerKey)
            return WaiterAdmission::Rejected;
    }
    if (m_waiterCount >= kMaximumWaiters)
        return WaiterAdmission::Rejected;
    if (it == table.end())
        it = table.insert(key, {});
    it->append(waiter);
    ++m_waiterCount;
    return WaiterAdmission::Added;
}

void BagLinkController::finishWaiters(QHash<QString, QVector<Waiter>>& table,
                                      const QString& key, const QString& value, Outcome outcome)
{
    const QVector<Waiter> waiters = table.take(key);
    m_waiterCount = qMax<qsizetype>(0, m_waiterCount - waiters.size());
    for (const Waiter& waiter : waiters) {
        QString link = value;
        Outcome effectiveOutcome = outcome;
        if (effectiveOutcome == Outcome::Resolve && value.isEmpty())
            effectiveOutcome = Outcome::Dead;
        if (effectiveOutcome == Outcome::Archive && value.isEmpty())
            continue;
        if (outcome == Outcome::Recover && value.isEmpty())
            continue;
        if (effectiveOutcome == Outcome::Dead)
            link = waiter.expectedLink;
        if (effectiveOutcome == Outcome::Recover) {
            if (m_recoveredLinks.contains(key)
                || m_recoveredLinks.size() < kMaximumRecoveredLinks)
                m_recoveredLinks.insert(key, link);
        }
        apply(waiter, link, effectiveOutcome);
    }
}

void BagLinkController::clearWaiters(QHash<QString, QVector<Waiter>>& table, const QString& key)
{
    const QVector<Waiter> waiters = table.take(key);
    m_waiterCount = qMax<qsizetype>(0, m_waiterCount - waiters.size());
}

QString BagLinkController::requestKey(qint64 bagId, const QString& imageKey, const QString& url)
{
    const QByteArray digest = QCryptographicHash::hash(
        (QString::number(bagId) + QLatin1Char('\n') + imageKey + QLatin1Char('\n') + url)
            .toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("baglink-") + QString::fromLatin1(digest);
}

void BagLinkController::apply(const Waiter& waiter, const QString& link, Outcome outcome)
{
    if (!m_storage || !m_beanBase || link.isEmpty())
        return;
    if (waiter.activeRequired && (!m_dye || m_dye->activeBagId() != waiter.bagId))
        return;
    if (m_pendingWrites.contains(waiter.bagId))
        return;
    QString replacement;
    if (outcome == Outcome::Recover)
        replacement = BeanBaseClient::blobWithLink(waiter.expectedBlob, link);
    else
        replacement = BeanBaseClient::blobWithLinkVerdict(waiter.expectedBlob, link,
                                                         outcome == Outcome::Dead);
    if (replacement.isEmpty() || replacement == waiter.expectedBlob)
        return;

    m_pendingWrites.insert(waiter.bagId);
    QPointer<BagLinkController> self(this);
    m_storage->requestUpdateBeanBaseDataIfCurrent(waiter.bagId, waiter.beanBaseId,
        waiter.expectedBlob,
        replacement, this,
        [self, waiter, link, outcome](qint64 bagId, bool success, const QString&,
                                     const QString& reason) {
            if (!self)
                return;
            self->m_pendingWrites.remove(bagId);
            if (!success) {
                if (reason == QLatin1String("staleData"))
                    self->reapplyIfStillCurrent(waiter, link, outcome);
                return;
            }
            if (outcome == Outcome::Archive && self->m_beanBase) {
                self->m_beanBase->refreshBagImage(waiter.imageKey, waiter.coffeeName, link);
            } else if (outcome == Outcome::Recover) {
                self->inspectAgain(bagId);
            }
        });
}

void BagLinkController::inspectAgain(qint64 bagId)
{
    if (!m_storage)
        return;
    m_storage->requestBagWithCompletion(bagId, this,
        [this](qint64 id, bool success, const QVariantMap& bag, const QString&) {
            if (success)
                inspectRow(id, bag);
        });
}

void BagLinkController::reapplyIfStillCurrent(const Waiter& waiter, const QString& link,
                                              Outcome outcome)
{
    if (!m_storage || (waiter.activeRequired
        && (!m_dye || m_dye->activeBagId() != waiter.bagId)))
        return;
    m_storage->requestBagWithCompletion(waiter.bagId, this,
        [this, waiter, link, outcome](qint64 id, bool success, const QVariantMap& current,
                                      const QString&) {
            if (!success)
                return;
            const QString currentBlob = current.value(QStringLiteral("beanBaseData")).toString();
            const QJsonDocument doc = QJsonDocument::fromJson(currentBlob.toUtf8());
            const QJsonObject blob = doc.isObject() ? doc.object() : QJsonObject();
            const QString currentId = current.value(QStringLiteral("beanBaseId")).toString();
            const QString currentLink = blob.value(QStringLiteral("link")).toString();
            const bool stillSameQuestion = currentId == waiter.beanBaseId
                && (outcome == Outcome::Recover ? currentLink.isEmpty()
                                                : currentLink == waiter.expectedLink);
            if (!stillSameQuestion || id != waiter.bagId)
                return;
            Waiter fresh = waiter;
            fresh.expectedBlob = currentBlob;
            apply(fresh, link, outcome);
        });
}
