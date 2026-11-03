#pragma once

#include "rep_preparationservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QSet>

class PreparationState;

class NativePreparationServiceSource final : public PreparationServiceStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativePreparationServiceSource(PreparationState* preparation,
                                            QObject* parent = nullptr);
    void execute(QString requestId, QString action, QString owner) override;
    // Only an admitted client may offer a milk sample: closing it releases its offers.
    void openClient(const QString& clientId);
    void closeClient(const QString& clientId);
private:
    void publish();
    void complete(const QString& requestId, const QString& action,
                  bool success, const QString& message);
    QPointer<PreparationState> m_preparation;
    ServiceRequestIds m_requests;
    QSet<QString> m_clients;
};
