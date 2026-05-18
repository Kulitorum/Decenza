// Temporary canary for verifying the run_tests parser-race fix.
// Ensures Qt Creator discovers this class after a reconfigure+build cycle.
#include <QtTest>
class tst_RaceConditionFix : public QObject
{
    Q_OBJECT
private slots:
    void placeholder() { QVERIFY(true); }
    void placeholder2() { QVERIFY(true); }
};
QTEST_MAIN(tst_RaceConditionFix)
#include "tst_raceconditionfix.moc"
