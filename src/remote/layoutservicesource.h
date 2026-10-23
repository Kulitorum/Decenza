#pragma once

#include "rep_layoutservice_source.h"
#include "serviceoperation.h"

#include <QMetaObject>
#include <QPointer>

class SettingsNetwork;

class NativeLayoutServiceSource final : public LayoutStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeLayoutServiceSource(SettingsNetwork* settings,
                                       QObject* parent = nullptr);

    void execute(QString requestId, qint64 expectedRevision,
                 QString operation, QVariantMap fields) override;

private:
    void publishSnapshot(bool advanceRevision);
    void complete(const QString& requestId, const QString& operation,
                  bool success, const QString& reason = {});

    QPointer<SettingsNetwork> m_settings;
    QMetaObject::Connection m_layoutChanged;
    ServiceRequestIds m_requests;
    qint64 m_revision = 0;
};
