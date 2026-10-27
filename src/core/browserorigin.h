#pragma once

#include <QString>

#if defined(__EMSCRIPTEN__)
// The page's origin (scheme://host[:port]) as the browser reports it; empty if unreadable.
QString browserPageOrigin();

// Where the host serves a bundled image set to this client: <origin>/wasm/<set>/<content hash>/
// (WebClientAssets readWebClientImage). The hash comes from decenza_image_sets.h.
QString hostImageSetBase(const QString& set, const char* contentHash);
#endif
