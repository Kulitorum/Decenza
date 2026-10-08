#pragma once

#include <QByteArray>
#include <QString>

// Keeps a web page on another site from reaching the shot server through the
// user's own browser. With web security off the server has no credential, so
// this check is all that stands between a page the user has open and
// POST /api/shot/<id>/metadata on the LAN.
namespace WebRequestGuard {

// The value of header `name` in a request's header block (request line and
// headers, no body), trimmed; empty when absent. The name is case-insensitive.
QString headerValue(const QByteArray& headerBlock, const char* name);

// Whether a `Host` header names this machine rather than a public domain an
// attacker pointed at its address (DNS rebinding): an IP literal, localhost, a
// single label, this machine's hostname, or a LAN, mDNS or Tailscale suffix.
// A missing Host passes: rebinding needs a browser, and a browser always sends it.
bool hostIsOurs(const QString& host);

// Why the request must be refused, or empty when it may proceed.
//  - A Host that is not ours (see hostIsOurs) is refused on every path.
//  - `Origin` is on every cross-origin fetch, XHR and form post, and on every
//    non-GET request: it has to name the host the request was sent to.
//  - `Sec-Fetch-Site` is on every browser request: anything but `same-origin`
//    or `none` is refused on /api and /mcp paths, which covers an <img> or
//    <iframe> aimed at a GET endpoint with a side effect from another site or
//    from another port on this host. A top-level navigation to a page from
//    another site (a link) still opens; a frame does not.
// Anything that is not a browser (curl, QNetworkAccessManager, an MCP client,
// Home Assistant) sends neither header and is let through, as is a browser
// older than Sec-Fetch (Chrome 76, Safari 16.4).
QString crossSiteReason(const QString& method, const QString& path, const QByteArray& headerBlock);

}
