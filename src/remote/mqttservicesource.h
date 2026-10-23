#pragma once

#include "rep_mqttservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QByteArray>

class MqttClient;
class Settings;
class ShotServer;

class NativeMqttServiceSource final : public MqttStateSimpleSource
{
    Q_OBJECT
public:
    NativeMqttServiceSource(Settings* settings, MqttClient* client,
                            ShotServer* shotServer, QObject* parent = nullptr);
    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments,
                 qint64 expectedRevision) override;

private:
    void publish();
    QVariantMap capture() const;
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    bool setPreference(const QVariantMap& arguments, QString* reason);

    QPointer<Settings> m_settings;
    QPointer<MqttClient> m_client;
    QPointer<ShotServer> m_shotServer;
    ServiceRequestIds m_requests;
    QVariantMap m_snapshot;
    QVariantMap m_preferenceSnapshot;
    QByteArray m_passwordRevisionToken;
    qint64 m_revision = 0;
};
