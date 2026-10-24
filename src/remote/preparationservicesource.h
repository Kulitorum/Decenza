#pragma once

#include "rep_preparationservice_source.h"
#include "serviceoperation.h"

#include <QPointer>

class PreparationState;

class NativePreparationServiceSource final : public PreparationServiceStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativePreparationServiceSource(PreparationState* preparation,
                                            QObject* parent = nullptr);
    void execute(QString requestId, QString action) override;
private:
    void publish();
    void complete(const QString& requestId, const QString& action,
                  bool success, const QString& message);
    QPointer<PreparationState> m_preparation;
    ServiceRequestIds m_requests;
};
