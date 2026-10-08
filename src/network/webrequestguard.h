#pragma once

#include <QByteArray>
#include <QString>

// Keeps a web page on another site from driving the shot server through the
// user's own browser. With web security off the server has no credential, so
// this check is all that stands between a page the user has open and
// POST /api/shot/<id>/metadata on the LAN.
namespace WebRequestGuard {

// The value of header `name` in a request's header block (request line and
// headers, no body), trimmed; empty when absent. The name is case-insensitive.
QString headerValue(const QByteArray& headerBlock, const char* name);

// Why the request must be refused as cross-site, or empty when it may proceed.
// A browser says where a request came from in two headers; anything else
// (curl, QNetworkAccessManager, an MCP client, Home Assistant) sends neither
// and is let through.
//  - `Origin` is on every cross-origin fetch, XHR and form post, and on every
//    non-GET request: it has to name the host the request was sent to.
//  - `Sec-Fetch-Site` is on every browser request: `cross-site` is refused on
//    /api and /mcp paths, which also covers an <img> or <iframe> aimed at a GET
//    endpoint with a side effect, while a cross-site navigation to a page (a
//    link from another site, an iframe in a dashboard) still opens.
QString crossSiteReason(const QString& method, const QString& path, const QByteArray& headerBlock);

}
