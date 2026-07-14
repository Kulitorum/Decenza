#include <QTest>
#include <QLocale>

#include "network/roastdate.h"

// Guards RoastDate::toIso() — the roast-date normalizer applied at the
// Visualizer payload boundary. New bean-bag shots are already ISO; legacy
// pre-bag shots carry the user's display format (e.g. US "12.15.2025"), which
// the migration-16 re-sync would otherwise ship verbatim. The tricky parts are
// year-position detection and the locale tie-break on genuinely ambiguous
// numeric dates, so those get explicit coverage.
class TstRoastDate : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void iso_passesThroughCanonical();
    void yearFirst_separators_normalize();
    void usDisplay_unambiguous_normalize();
    void dayFirst_unambiguous_normalize();
    void ambiguous_resolvesByLocale();
    void unparseable_passThrough_data();
    void unparseable_passThrough();
    void whitespace_trimmedThenParsed();

private:
    QLocale m_saved;
};

void TstRoastDate::init()
{
    QTest::failOnWarning();
    m_saved = QLocale();
    // Pin a month-first locale so non-locale tests are deterministic.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void TstRoastDate::cleanup()
{
    QLocale::setDefault(m_saved);
}

void TstRoastDate::iso_passesThroughCanonical()
{
    QCOMPARE(RoastDate::toIso(QStringLiteral("2025-12-15")), QStringLiteral("2025-12-15"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("2025-01-02")), QStringLiteral("2025-01-02"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("2024-02-29")), QStringLiteral("2024-02-29"));  // leap day
}

void TstRoastDate::yearFirst_separators_normalize()
{
    QCOMPARE(RoastDate::toIso(QStringLiteral("2025/12/15")), QStringLiteral("2025-12-15"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("2025.12.15")), QStringLiteral("2025-12-15"));
}

void TstRoastDate::usDisplay_unambiguous_normalize()
{
    // The actual legacy shape: US MM.DD.YYYY with dots. Unambiguous because the
    // day component exceeds 12, so month-first is the only valid calendar parse.
    QCOMPARE(RoastDate::toIso(QStringLiteral("12.15.2025")), QStringLiteral("2025-12-15"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("12/15/2025")), QStringLiteral("2025-12-15"));
}

void TstRoastDate::dayFirst_unambiguous_normalize()
{
    // Month 15 is impossible, so only day-first parses — locale is irrelevant.
    QCOMPARE(RoastDate::toIso(QStringLiteral("15.12.2025")), QStringLiteral("2025-12-15"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("31/01/2025")), QStringLiteral("2025-01-31"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("29.02.2024")), QStringLiteral("2024-02-29"));  // leap day, day-first
}

void TstRoastDate::ambiguous_resolvesByLocale()
{
    // Both groups <= 12, so both orders are valid calendar dates and the locale
    // order (the order the legacy display string was written in) decides.
    // Single-digit "1/2/2025" is equally ambiguous despite looking US-shaped.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));  // M/d
    QCOMPARE(RoastDate::toIso(QStringLiteral("01.02.2025")), QStringLiteral("2025-01-02"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("1/2/2025")), QStringLiteral("2025-01-02"));

    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));  // d/M
    QCOMPARE(RoastDate::toIso(QStringLiteral("01.02.2025")), QStringLiteral("2025-02-01"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("1/2/2025")), QStringLiteral("2025-02-01"));
}

void TstRoastDate::unparseable_passThrough_data()
{
    QTest::addColumn<QString>("input");
    QTest::newRow("empty") << QString();
    QTest::newRow("text month") << QStringLiteral("December 2025");
    QTest::newRow("year only") << QStringLiteral("2025");
    QTest::newRow("garbage") << QStringLiteral("not a date");
    QTest::newRow("two digit year") << QStringLiteral("12.15.25");
    QTest::newRow("invalid both orders") << QStringLiteral("13.13.2025");
    QTest::newRow("iso datetime") << QStringLiteral("2025-12-15T10:00:00");
    // Year-first shape but not a real calendar date: must pass through, never
    // get silently mangled or cleared (exercises the isValid() guard).
    QTest::newRow("year-first bad month") << QStringLiteral("2025-13-01");
    QTest::newRow("year-first bad day") << QStringLiteral("2025.02.30");
}

void TstRoastDate::unparseable_passThrough()
{
    QFETCH(QString, input);
    QCOMPARE(RoastDate::toIso(input), input);
}

void TstRoastDate::whitespace_trimmedThenParsed()
{
    QCOMPARE(RoastDate::toIso(QStringLiteral("  2025-12-15  ")), QStringLiteral("2025-12-15"));
}

QTEST_MAIN(TstRoastDate)
#include "tst_roastdate.moc"
