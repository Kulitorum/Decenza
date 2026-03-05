#include "solobaristascale.h"

SoloBaristaScale::SoloBaristaScale(ScaleBleTransport* transport, QObject* parent)
    : EurekaPrecisaScale(transport, parent)
{
    m_scaleName = "Solo Barista";
}
