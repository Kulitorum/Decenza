#pragma once

#include <QString>

#if defined(__EMSCRIPTEN__)
// The page's origin (scheme://host[:port]) as the browser reports it; empty if unreadable.
QString browserPageOrigin();
#endif
