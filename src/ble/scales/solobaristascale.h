#pragma once

#include "eurekaprecisascale.h"

// Solo Barista uses the same protocol as Eureka Precisa
class SoloBaristaScale : public EurekaPrecisaScale {
    Q_OBJECT

public:
    explicit SoloBaristaScale(ScaleBleTransport* transport, QObject* parent = nullptr);

    QString name() const override { return m_scaleName; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::SoloBarista); }

private:
    QString m_scaleName = "Solo Barista";
};
