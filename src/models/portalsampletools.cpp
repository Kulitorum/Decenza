#include "portalsampletools.h"

#include "portalsample.h"

double PortalSampleTools::ecAxisPadding() const
{
    return PortalSamples::EcAxisPadding;
}

QVariantList PortalSampleTools::ecBounds(const QVariantList& samples) const
{
    PortalSamples::EcBounds bounds;
    for (const PortalSample& sample : PortalSamples::fromVariant(samples))
        bounds.add(sample.ecRaw);
    return {bounds.minimum, bounds.maximum};
}
