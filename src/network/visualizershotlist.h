#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QJsonValue>
#include <limits>

// Shared page processing for the paginated GET /api/shots list on
// visualizer.coffee. Both the uploader back-sync (VisualizerUploader) and the
// recovery importer (VisualizerImporter) page the same endpoint with the same
// { data:[{id, clock, updated_at}], paging:{pages,...} } schema and the same
// window / early-stop / ceiling policy — this is the one implementation of that
// decision logic so the two callers can't drift.
//
// Pure and dependency-free (no networking, no state): each caller keeps its own
// transport (request building, recursion, signal/state wiring) and hands each
// page's raw body to processPage(). That also makes the boundary logic — the
// bit most prone to off-by-one / silent-drop bugs — unit-testable without a
// live network (tests/tst_visualizershotlist.cpp).
//
// Early termination (see wholePageOlder below) relies on the endpoint's default
// sort being newest-first by start time. Confirmed against the visualizer.coffee
// source (app/controllers/api/shots_controller.rb + app/models/shot.rb):
//   - default order is `scope :by_start_time, -> { order(start_time: :desc) }`;
//   - the `clock` field this filters on is `start_time.to_i` — i.e. the exact
//     value the window is expressed in;
//   - the only other ordering is the opt-in `?sort=updated_at`, which neither
//     caller sends.
// So a whole page older than the window start guarantees every later page is
// older too. As a belt-and-braces backstop regardless, the kMaxPages ceiling
// bounds the loop and turns an over-long run into a loud PageCeiling failure
// rather than a silent partial result.
namespace VisualizerShotList {

// One shot from the list: its Visualizer id and start time (Unix seconds).
struct Entry {
    QString visualizerId;
    qint64 clockEpoch = 0;
};

// What the caller should do after this page.
enum class Verdict {
    NextPage,  // keep paging
    Done,      // all in-window shots collected
    Fail       // stop and surface `reason`
};

// Why a page failed (only meaningful when verdict == Fail). Callers map this to
// their own user-facing / internal message so each keeps its existing wording.
enum class FailReason {
    None,
    ParseError,    // body was not valid JSON / not an object
    MissingPaging, // 200 without a numeric paging.pages — likely an auth/error envelope
    PageCeiling    // hit the defensive page cap before the real end
};

struct PageResult {
    Verdict verdict = Verdict::Fail;
    FailReason reason = FailReason::None;
    QString parseError;         // JSON error text when reason == ParseError
    int totalPages = 0;         // paging.pages (for the caller's ceiling message)
    QVector<Entry> inWindow;    // this page's entries with clock ∈ [fromEpoch, toEpoch]
};

// Process one page of GET /api/shots?page=N.
//   body               raw response bytes
//   page               1-based page number just fetched
//   maxPages           defensive page ceiling (hitting it before totalPages => PageCeiling)
//   fromEpoch/toEpoch  inclusive selection window in Unix seconds; pass
//                      toEpoch == std::numeric_limits<qint64>::max() for a
//                      lower-bound-only window (the uploader's back-sync case).
inline PageResult processPage(const QByteArray& body, int page, int maxPages,
                              qint64 fromEpoch, qint64 toEpoch)
{
    PageResult result;

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        result.reason = FailReason::ParseError;
        result.parseError = perr.errorString();
        return result;
    }

    const QJsonObject root = doc.object();
    // A valid list response MUST carry a `paging` object with a numeric `pages`.
    // A 200 lacking it is almost certainly an auth/error envelope (e.g. an
    // expired session returning {}), NOT a legitimately empty library — treating
    // it as an empty success would silently report "nothing to import".
    const QJsonValue pagingVal = root.value("paging");
    if (!pagingVal.isObject() || !pagingVal.toObject().value("pages").isDouble()) {
        result.reason = FailReason::MissingPaging;
        return result;
    }
    result.totalPages = pagingVal.toObject().value("pages").toInt(page);

    const QJsonArray data = root.value("data").toArray();
    qint64 minClockThisPage = std::numeric_limits<qint64>::max();
    for (const QJsonValue& v : data) {
        const QJsonObject s = v.toObject();
        const QString id = s.value("id").toString();
        const qint64 clock = s.value("clock").toVariant().toLongLong();
        if (id.isEmpty() || clock <= 0) continue;
        minClockThisPage = qMin(minClockThisPage, clock);
        if (clock < fromEpoch || clock > toEpoch)
            continue;  // outside the requested window
        result.inWindow.append({id, clock});
    }

    // Newest-first sort: once a whole page predates the window start, every later
    // page is older too, so all in-window shots have been collected.
    const bool wholePageOlder =
        !data.isEmpty() && minClockThisPage < fromEpoch;

    // Hitting the page ceiling BEFORE reaching the real end (and before we've
    // paged past the window) means older in-window shots may exist beyond the
    // cap. Reporting the partial result as "complete" would be silent data loss,
    // so fail loudly instead.
    if (page >= maxPages && page < result.totalPages && !wholePageOlder) {
        result.reason = FailReason::PageCeiling;
        return result;
    }

    const bool pagedOut = page >= result.totalPages;
    result.verdict = (pagedOut || wholePageOlder) ? Verdict::Done : Verdict::NextPage;
    return result;
}

}  // namespace VisualizerShotList
