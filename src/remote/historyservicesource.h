#pragma once

#include "rep_historyservice_source.h"

#include <QHash>
#include <QPointer>
#include <functional>

class SettingsNetwork;
class ShotHistoryStorage;

class NativeHistoryServiceSource final : public HistoryStateSimpleSource
{
    Q_OBJECT
public:
    using LoadShot = std::function<void(const QString& requestId, qint64 shotId)>;

    NativeHistoryServiceSource(ShotHistoryStorage* history, SettingsNetwork* settings,
                               LoadShot loadShot = {}, QObject* parent = nullptr);

    void refresh() override;
    void requestAutoFavorites(QString requestId, QString groupBy, int maxItems) override;
    void requestAutoFavoriteGroupDetails(QString requestId, QVariantMap group) override;
    void requestMostRecentShotId(QString requestId) override;
    void requestPage(QString requestId, QVariantMap filter, int offset, int limit) override;
    void requestShot(QString requestId, qint64 shotId) override;
    void requestOutcome(QString requestId, qint64 shotId) override;
    void requestUploadState(QString requestId, qint64 shotId) override;
    void requestBaristas(QString requestId) override;
    void updateMetadata(QString requestId, qint64 shotId, QVariantMap metadata) override;
    void deleteShot(QString requestId, qint64 shotId) override;
    void deleteShots(QString requestId, QVariantList shotIds) override;
    void loadShotWithMetadata(QString requestId, qint64 shotId) override;
    void completeShotLoad(const QString& requestId, qint64 shotId, bool success);
    void setPreference(QString requestId, QString name, QVariant value,
                       quint64 expectedRevision) override;

private:
    bool validRequest(const QString& requestId, const QString& operation);
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void updateHistoryState();
    QVariantMap capturePreferences() const;
    void finishShotMap(const QString& requestId, const QString& operation,
                       qint64 shotId, const QVariantMap& result, const QString& reason);

    QPointer<ShotHistoryStorage> m_history;
    QPointer<SettingsNetwork> m_settings;
    LoadShot m_loadShot;
    quint64 m_historyRevision = 0;
    quint64 m_preferencesRevision = 0;
    QHash<QString, QString> m_pending;
    QHash<QString, qint64> m_pendingShotLoads;
};
