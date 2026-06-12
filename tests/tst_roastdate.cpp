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
}

void TstRoastDate::yearFirst_separators_normalize()
{
    QCOMPARE(RoastDate::toIso(QStringLiteral("2025/12/15")), QStringLiteral("2025-12-15"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("2025.12.15")), QStringLiteral("2025-12-15"));
}

void TstRoastDate::usDisplay_unambiguous_normalize()
{
    // The actual legacy shape: US MM.DD.YYYY with dots, day > 12.
    QCOMPARE(RoastDate::toIso(QStringLiteral("12.15.2025")), QStringLiteral("2025-12-15"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("12/15/2025")), QStringLiteral("2025-12-15"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("1/2/2025")), QStringLiteral("2025-01-02"));
}

void TstRoastDate::dayFirst_unambiguous_normalize()
{
    // Month 15 is impossible, so only day-first parses — locale is irrelevant.
    QCOMPARE(RoastDate::toIso(QStringLiteral("15.12.2025")), QStringLiteral("2025-12-15"));
    QCOMPARE(RoastDate::toIso(QStringLiteral("31/01/2025")), QStringLiteral("2025-01-31"));
}

void TstRoastDate::ambiguous_resolvesByLocale()
{
    // 01.02.2025 parses validly both ways — the locale order decides.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));  // M/d
    QCOMPARE(RoastDate::toIso(QStringLiteral("01.02.2025")), QStringLiteral("2025-01-02"));

    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));  // d/M
    QCOMPARE(RoastDate::toIso(QStringLiteral("01.02.2025")), QStringLiteral("2025-02-01"));
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
