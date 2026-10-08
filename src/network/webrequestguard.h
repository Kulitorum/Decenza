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
// single label, exactly this machine's hostname, or a LAN, mDNS or Tailscale
// suffix. A missing Host passes: rebinding needs a browser, which always sends it.
bool hostIsOurs(const QString& host);

// Why the request must be refused, or empty when it may proceed.
//  - A Host that is not ours (see hostIsOurs) is refused on every path.
//  - `Origin` is on every cross-origin fetch, XHR and form post, and on every
//    non-GET request, over plain HTTP too: it has to name the host the request
//    was sent to. This is the defence for writes, which is why every route
//    with a side effect takes POST and not GET.
//  - `Sec-Fetch-Site`: anything but `same-origin` or `none` is refused on /api
//    and /mcp paths, and a frame from another site is refused everywhere; a
//    top-level navigation to a page from another site (a link) still opens.
//    Browsers send the Sec-Fetch headers only to a potentially trustworthy URL
//    (Fetch Standard, "append the Fetch metadata headers for a request", step
//    1): https, the tsnet funnel or localhost. On the default http://<ip> page
//    they are absent and this branch passes, so it adds nothing there.
// Anything that is not a browser (curl, QNetworkAccessManager, an MCP client,
// Home Assistant) sends neither header and is let through.
QString crossSiteReason(const QString& method, const QString& path, const QByteArray& headerBlock);

}
