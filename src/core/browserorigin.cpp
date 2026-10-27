#include "browserorigin.h"

#include <cstdlib>
#include <emscripten.h>

QString browserPageOrigin()
{
    auto* value = static_cast<char*>(EM_ASM_PTR({
        return stringToNewUTF8(window.location.origin);
    }));
    if (!value)
        return {};
    const QString origin = QString::fromUtf8(value);
    std::free(value);
    return origin;
}

QString hostImageSetBase(const QString& set, const char* contentHash)
{
    return browserPageOrigin() + QStringLiteral("/wasm/") + set + QLatin1Char('/')
        + QLatin1String(contentHash) + QLatin1Char('/');
}
