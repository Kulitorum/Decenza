#pragma once
#include "rep_beanuiservice_source.h"
#include "serviceoperation.h"
#include <QHash>
#include <QPointer>
#include <QNetworkAccessManager>
#include <functional>
#include <utility>
class BeanBaseClient;
class AIManager;
class Settings;
class VisualizerShotSync;
class CoffeeBagStorage;
class BagLinkController;
class CommunitySettingsClient;
class NativeBeanUiServiceSource final : public BeanUiStateSimpleSource {
    Q_OBJECT
public:
    NativeBeanUiServiceSource(BeanBaseClient*, AIManager*, Settings*, VisualizerShotSync*,
                              CoffeeBagStorage*, QObject* parent = nullptr);
    void invoke(QString requestId, QString operation, QVariantMap arguments) override;
    void setCommunitySettings(CommunitySettingsClient* client);
    void setCommunityApply(std::function<void(const QVariantMap&)> apply)
    { m_applyCommunity = std::move(apply); }
    void completeCommunitySuggestion(const QString& requestId, bool ok,
                                     const QString& recipeName, bool selected);
    void cancel(QString requestId) override;
private:
    void publishCapabilities();
    void finish(const QString& id, const QVariantMap& values = {}, const QString& reason = {});
    bool validateArguments(const QString& operation, const QVariantMap& arguments) const;
    void removeDiagnostic(const QString& requestId);
    void finishCommunityRead(bool profile, const QString& key, const QVariantMap& values,
                              const QString& reason = {});
    QPointer<BeanBaseClient> m_beans;
    QPointer<AIManager> m_ai;
    QPointer<Settings> m_settings;
    QPointer<VisualizerShotSync> m_visualizer;
    QPointer<BagLinkController> m_bagLinks;
    QPointer<CommunitySettingsClient> m_community;
    std::function<void(const QVariantMap&)> m_applyCommunity;
    QHash<QString, QString> m_communityReads;
    QHash<QString, QString> m_profileReads;
    QString m_startingCommunityRequest;
    QString m_startingProfileRequest;
    QNetworkAccessManager m_network;
    ServiceRequestIds m_requests;
    QHash<QString, QString> m_operations;
    QHash<QString, QString> m_diagnostics;
    QHash<QString, QString> m_extractionPages;
    QHash<QString, QPointer<BeanBaseClient>> m_searches;
};
