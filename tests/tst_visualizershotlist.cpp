// Unit tests for VisualizerShotList::processPage — the shared, pure
// page-processing logic behind the paginated GET /api/shots list used by BOTH
// the uploader back-sync and the recovery importer. This is the boundary code
// (window filter, newest-first early-stop, page-ceiling guard, auth-envelope
// detection) that used to be duplicated inline in two network callbacks and was
// therefore untestable; extracting it made it a pure function we can exercise
// without a live network.

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <limits>

#include "network/visualizershotlist.h"

using namespace VisualizerShotList;

class TstVisualizerShotList : public QObject
{
    Q_OBJECT

private:
    void init() { QTest::failOnWarning(); }

    // Build a page body: { data:[{id,clock}...], paging:{pages} }.
    static QByteArray page(const QVector<QPair<QString, qint64>>& shots, int totalPages)
    {
        QJsonArray data;
        for (const auto& s : shots) {
            QJsonObject o;
            o["id"] = s.first;
            o["clock"] = static_cast<double>(s.second);
            data.append(o);
        }
        QJsonObject paging;
        paging["pages"] = totalPages;
        QJsonObject root;
        root["data"] = data;
        root["paging"] = paging;
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    static constexpr qint64 kNoUpper = std::numeric_limits<qint64>::max();

private slots:
    // Non-JSON / non-object bodies fail with ParseError, not a silent empty page.
    void parse_error_is_flagged()
    {
        const PageResult r = processPage(QByteArrayLiteral("not json{"), 1, 50, 0, kNoUpper);
        QCOMPARE(r.verdict, Verdict::Fail);
        QCOMPARE(r.reason, FailReason::ParseError);
        QVERIFY(!r.parseError.isEmpty());
    }

    // A 200 with no paging.pages is an auth/error envelope, not an empty library.
    void missing_paging_is_flagged()
    {
        const PageResult r = processPage(QByteArrayLiteral("{}"), 1, 50, 0, kNoUpper);
        QCOMPARE(r.verdict, Verdict::Fail);
        QCOMPARE(r.reason, FailReason::MissingPaging);
    }

    // Entries inside [from,to] are collected; those outside are dropped. Junk
    // entries (empty id, non-positive clock) are ignored.
    void window_filters_entries()
    {
        const QByteArray body = page({
            {"a", 1000},           // below window
            {"b", 2000},           // in window
            {"c", 3000},           // in window
            {"d", 9000},           // above window
            {"",  2500},           // empty id -> skipped
            {"e", 0},              // clock 0 -> skipped
        }, /*totalPages*/ 1);

        const PageResult r = processPage(body, 1, 50, /*from*/ 1500, /*to*/ 3500);
        QCOMPARE(r.verdict, Verdict::Done);       // single page, paged out
        QCOMPARE(r.reason, FailReason::None);
        QCOMPARE(r.inWindow.size(), 2);
        QCOMPARE(r.inWindow[0].visualizerId, QStringLiteral("b"));
        QCOMPARE(r.inWindow[0].clockEpoch, qint64(2000));
        QCOMPARE(r.inWindow[1].visualizerId, QStringLiteral("c"));
    }

    // Lower-bound-only window (uploader back-sync): to == max.
    void lower_bound_only_window()
    {
        const QByteArray body = page({{"a", 500}, {"b", 5000}}, 1);
        const PageResult r = processPage(body, 1, 50, /*from*/ 1000, kNoUpper);
        QCOMPARE(r.inWindow.size(), 1);
        QCOMPARE(r.inWindow[0].visualizerId, QStringLiteral("b"));
    }

    // More pages remain and this page isn't yet past the window -> keep paging.
    void keeps_paging_when_more_pages()
    {
        const QByteArray body = page({{"a", 9000}, {"b", 8000}}, /*totalPages*/ 3);
        const PageResult r = processPage(body, /*page*/ 1, 50, /*from*/ 1000, kNoUpper);
        QCOMPARE(r.verdict, Verdict::NextPage);
    }

    // Newest-first early stop: once a page's oldest entry predates the window
    // start, everything further back is older too -> Done, even mid-pagination.
    void whole_page_older_stops_early()
    {
        const QByteArray body = page({{"a", 2000}, {"b", 900}}, /*totalPages*/ 10);
        const PageResult r = processPage(body, /*page*/ 2, 50, /*from*/ 1000, kNoUpper);
        QCOMPARE(r.verdict, Verdict::Done);
        QCOMPARE(r.inWindow.size(), 1);           // only the in-window one (a)
        QCOMPARE(r.inWindow[0].visualizerId, QStringLiteral("a"));
    }

    // Hitting the ceiling before the real end (and before paging past the
    // window) is a loud failure, never a silent partial "Done".
    void page_ceiling_fails_loudly()
    {
        // page == maxPages, more pages exist, and this page is NOT wholly older
        // than the window -> PageCeiling.
        const QByteArray body = page({{"a", 9000}, {"b", 8000}}, /*totalPages*/ 20);
        const PageResult r = processPage(body, /*page*/ 5, /*maxPages*/ 5, /*from*/ 1000, kNoUpper);
        QCOMPARE(r.verdict, Verdict::Fail);
        QCOMPARE(r.reason, FailReason::PageCeiling);
        QCOMPARE(r.totalPages, 20);
    }

    // At the ceiling but already paged past the window: NOT a failure — all
    // in-window shots were collected (this is the importer's !wholePageOlder
    // refinement, now shared by both callers).
    void ceiling_with_whole_page_older_is_done()
    {
        const QByteArray body = page({{"a", 900}, {"b", 800}}, /*totalPages*/ 20);
        const PageResult r = processPage(body, /*page*/ 5, /*maxPages*/ 5, /*from*/ 1000, kNoUpper);
        QCOMPARE(r.verdict, Verdict::Done);
        QCOMPARE(r.reason, FailReason::None);
    }
};

QTEST_GUILESS_MAIN(TstVisualizerShotList)
#include "tst_visualizershotlist.moc"
