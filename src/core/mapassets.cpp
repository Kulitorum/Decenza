#include "mapassets.h"

#if defined(__EMSCRIPTEN__)
#include "core/browserorigin.h"
#include "decenza_image_sets.h"
#endif

QString MapAssets::imageBase() const
{
#if defined(__EMSCRIPTEN__)
    return hostImageSetBase(QStringLiteral("maps"), DECENZA_MAP_SET_HASH);
#else
    return QStringLiteral("qrc:/maps/");
#endif
}
