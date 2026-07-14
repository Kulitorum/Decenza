#include <QtTest>

#include "core/autowakemanager.h"
#include "core/settings_autowake.h"

// Tests for AutoWakeManager::isWithinStayAwakeWindowAt() — the continuously
// evaluated "are we inside a scheduled stay-awake window?" predicate that
// replaced the one-shot countdown (Kulitorum/Decenza#1203). The schedule
// must take priority over the generic auto-off, and a window that began
// before midnight must keep applying into the next day.
//
// Settings isolation: SettingsAutoWake persists to the real shared
// QSettings("DecentEspresso", "DE1Qt") (same store the app uses, and the
// same pattern as tst_settings / tst_saw_settings). The three keys this
// suite touches are snapshotted once in initTestCase() and restored once
// in cleanupTestCase(), so intermediate per-test mutations never leak and
// the developer's real config ends unchanged. One documented caveat: if
// `autoWake/schedule` was originally unset, it is restored as an
// all-days-disabled 7-entry list rather than removed. That is behaviorally
// identical to "unset" for every consumer — no production code treats the
// presence of the schedule key as a sentinel (auto-wake on/off is the
// separate `autoWake/enabled` key; scheduleNextWake() and
// isWithinStayAwakeWindow() only ever read per-day `enabled`).
//
// DST note: the predicate builds both `now` and the window bounds as
// local-time QDateTime and uses QDateTime::addSecs(), which converts to
// UTC internally and so is correct across DST transitions. That is not
// unit-tested here on purpose: a DST assertion would depend on the test
// host's timezone and be non-deterministic across CI runners. Correctness
// is by construction (single time basis on both sides of the comparison).

class tst_AutoWakeWindow : public QObject {
    Q_OBJECT

private:
    SettingsAutoWake m_settings;

    QVariantList m_origSchedule;
    bool m_origStayEnabled = false;
    int m_origStayMinutes = 0;

    // Build a 7-entry schedule (0=Mon .. 6=Sun) with every day disabled
    // except `dayIndex`, which is enabled at hour:minute.
    static QVariantList scheduleWith(int dayIndex, int hour, int minute) {
        QVariantList schedule;
        for (int i = 0; i < 7; ++i) {
            QVariantMap day;
            day["enabled"] = (i == dayIndex);
            day["hour"] = (i == dayIndex) ? hour : 7;
            day["minute"] = (i == dayIndex) ? minute : 0;
            schedule.append(day);
        }
        return schedule;
    }

    // Build a 7-entry schedule with two days enabled at independent times.
    static QVariantList scheduleWith2(int dayA, int hourA, int minuteA,
                                      int dayB, int hourB, int minuteB) {
        QVariantList schedule;
        for (int i = 0; i < 7; ++i) {
            QVariantMap day;
            const bool enabled = (i == dayA || i == dayB);
            day["enabled"] = enabled;
            day["hour"] = (i == dayA) ? hourA : (i == dayB) ? hourB : 7;
            day["minute"] = (i == dayA) ? minuteA : (i == dayB) ? minuteB : 0;
            schedule.append(day);
        }
        return schedule;
    }

    bool windowAt(const QDateTime& now) {
        AutoWakeManager mgr(&m_settings);
        return mgr.isWithinStayAwakeWindowAt(now);
    }

private slots:

    void initTestCase() {
        m_origSchedule = m_settings.autoWakeSchedule();
        m_origStayEnabled = m_settings.autoWakeStayAwakeEnabled();
        m_origStayMinutes = m_settings.autoWakeStayAwakeMinutes();
    }

    void cleanupTestCase() {
        m_settings.setAutoWakeSchedule(m_origSchedule);
        m_settings.setAutoWakeStayAwakeEnabled(m_origStayEnabled);
        m_settings.setAutoWakeStayAwakeMinutes(m_origStayMinutes);
    }

    void init() { QTest::failOnWarning();
        // Every test fully establishes the schedule/duration it needs in
        // its own body; this just guarantees a deterministic enabled flag
        // regardless of what the previous test left behind.
        m_settings.setAutoWakeStayAwakeEnabled(true);
    }

    // 06:10 wake + 8 h → window [06:10, 14:10) on the scheduled weekday.
    void sameDayWindow() {
        const QDate base(2026, 5, 18); // a Monday
        const int dow = base.dayOfWeek() - 1;
        m_settings.setAutoWakeSchedule(scheduleWith(dow, 6, 10));
        m_settings.setAutoWakeStayAwakeMinutes(480);

        QVERIFY2(!windowAt(QDateTime(base, QTime(5, 0))),  "before window");
        QVERIFY2(windowAt(QDateTime(base, QTime(6, 10))),  "exactly at start (inclusive)");
        QVERIFY2(windowAt(QDateTime(base, QTime(8, 0))),   "well inside — the #1203 repro");
        QVERIFY2(windowAt(QDateTime(base, QTime(14, 9))),  "one minute before end");
        QVERIFY2(!windowAt(QDateTime(base, QTime(14, 10))),"exactly at end (exclusive)");
        QVERIFY2(!windowAt(QDateTime(base, QTime(15, 0))), "after window");
    }

    // A window that starts at 22:00 and runs 8 h must still be active at
    // 02:00 the *next* calendar day — the day it began is "yesterday".
    void windowSpillsPastMidnight() {
        const QDate base(2026, 5, 18);
        const QDate nextDay = base.addDays(1);
        const int dow = base.dayOfWeek() - 1;
        m_settings.setAutoWakeSchedule(scheduleWith(dow, 22, 0));
        m_settings.setAutoWakeStayAwakeMinutes(480); // → ends 06:00 next day

        QVERIFY2(windowAt(QDateTime(base, QTime(23, 30))),     "late night, same day");
        QVERIFY2(windowAt(QDateTime(base, QTime(22, 0))),      "exactly at start (inclusive)");
        QVERIFY2(windowAt(QDateTime(nextDay, QTime(2, 0))),    "after midnight, still inside");
        QVERIFY2(windowAt(QDateTime(nextDay, QTime(5, 59))),   "one minute before spill end");
        QVERIFY2(!windowAt(QDateTime(nextDay, QTime(6, 0))),   "spill end (exclusive)");
        QVERIFY2(!windowAt(QDateTime(nextDay, QTime(9, 0))),   "next day, not itself scheduled");
    }

    // A short window on "yesterday" that does NOT cross midnight must not
    // leak into today via the dayOffset==1 branch of the loop.
    void yesterdayWindowDoesNotSpanMidnight() {
        const QDate base(2026, 5, 18);
        const QDate nextDay = base.addDays(1);
        const int dow = base.dayOfWeek() - 1;
        // Enable only `base`'s weekday at 06:10 + 2 h → [06:10, 08:10) on base.
        m_settings.setAutoWakeSchedule(scheduleWith(dow, 6, 10));
        m_settings.setAutoWakeStayAwakeMinutes(120);

        QVERIFY2(windowAt(QDateTime(base, QTime(7, 0))),       "inside on its own day (sanity)");
        // nextDay's weekday is disabled; the dayOffset==1 lookback finds
        // base's window but it ended at 08:10 on base — must not match.
        QVERIFY2(!windowAt(QDateTime(nextDay, QTime(7, 0))),   "non-spanning window must not leak into next day");
        QVERIFY2(!windowAt(QDateTime(nextDay, QTime(0, 30))),  "just after midnight, prior window already closed");
    }

    // Two adjacent enabled days: yesterday's window spills across midnight
    // while today's own window opens later, leaving a gap between them.
    // Pins the today/yesterday interaction against loop refactors.
    void adjacentEnabledDaysOverlap() {
        const QDate base(2026, 5, 18);
        const int dow = base.dayOfWeek() - 1;
        const int prevDow = (dow + 6) % 7;          // base's weekday minus one
        const QDate prevDay = base.addDays(-1);
        // prevDay: 22:00 + 240 min → spill ends base 02:00.
        // base   : 06:10 + 120 min → [06:10, 08:10).
        // Gap on base: [02:00, 06:10).
        m_settings.setAutoWakeSchedule(scheduleWith2(prevDow, 22, 0, dow, 6, 10));

        // Duration is a single global value, so exercise each window with
        // the duration that defines it.
        m_settings.setAutoWakeStayAwakeMinutes(240);
        QVERIFY2(windowAt(QDateTime(prevDay, QTime(23, 0))),  "inside yesterday's window, pre-midnight");
        QVERIFY2(windowAt(QDateTime(base, QTime(1, 0))),      "inside yesterday's spill, post-midnight");
        QVERIFY2(!windowAt(QDateTime(base, QTime(3, 0))),     "in the gap between the two windows");

        m_settings.setAutoWakeStayAwakeMinutes(120);
        QVERIFY2(windowAt(QDateTime(base, QTime(7, 0))),      "inside today's own window");
        QVERIFY2(!windowAt(QDateTime(base, QTime(9, 0))),     "after today's window");
    }

    // A 720-minute (12 h) duration — the current Settings slider maximum
    // (issue #1204, raised from 8 h by PR #1214) — must produce a full
    // 12 h window. The predicate itself enforces no cap: it honors
    // whatever autoWakeStayAwakeMinutes returns; the 12 h ceiling lives on
    // the UI slider, not here. This just verifies the math holds at that
    // duration.
    void twelveHourDuration() {
        const QDate base(2026, 5, 18);
        const int dow = base.dayOfWeek() - 1;
        m_settings.setAutoWakeSchedule(scheduleWith(dow, 6, 10));
        m_settings.setAutoWakeStayAwakeMinutes(720); // → ends 18:10

        QVERIFY2(windowAt(QDateTime(base, QTime(17, 0))),   "11 h in, still awake");
        QVERIFY2(!windowAt(QDateTime(base, QTime(18, 30))), "past 12 h, auto-off resumes");
    }

    // Guard conditions: the predicate must yield false (auto-off governs)
    // when stay-awake is off, the duration is non-positive, or the current
    // weekday is not scheduled.
    void disabledAndUnscheduledYieldFalse() {
        const QDate base(2026, 5, 18);
        const int dow = base.dayOfWeek() - 1;
        m_settings.setAutoWakeSchedule(scheduleWith(dow, 6, 10));
        m_settings.setAutoWakeStayAwakeMinutes(480);
        const QDateTime inside(base, QTime(8, 0));

        QVERIFY2(windowAt(inside), "sanity: inside window with stay-awake on");

        m_settings.setAutoWakeStayAwakeEnabled(false);
        QVERIFY2(!windowAt(inside), "stay-awake disabled");
        m_settings.setAutoWakeStayAwakeEnabled(true);

        m_settings.setAutoWakeStayAwakeMinutes(0);
        QVERIFY2(!windowAt(inside), "zero duration");

        m_settings.setAutoWakeStayAwakeMinutes(-30);
        QVERIFY2(!windowAt(inside), "negative duration");
        m_settings.setAutoWakeStayAwakeMinutes(480);

        // Enable only the *other* day; today's weekday is no longer scheduled.
        m_settings.setAutoWakeSchedule(scheduleWith((dow + 1) % 7, 6, 10));
        QVERIFY2(!windowAt(inside), "current weekday not scheduled");
    }

    // Structurally short / empty schedule lists must not crash or match —
    // the dayOfWeek >= schedule.size() guard returns false (auto-off wins).
    void shortAndEmptySchedule() {
        const QDate base(2026, 5, 18);
        const QDateTime probe(base, QTime(7, 0)); // inside a 00:00+480m window
        m_settings.setAutoWakeStayAwakeMinutes(480);

        m_settings.setAutoWakeSchedule(QVariantList{});
        QVERIFY2(!windowAt(probe), "empty schedule list");

        // A 3-entry list: any weekday index >= 3 must be safely skipped by
        // the dayOfWeek >= schedule.size() guard.
        QVariantList shortList;
        for (int i = 0; i < 3; ++i) {
            QVariantMap day;
            day["enabled"] = true;
            day["hour"] = 0;     // 00:00 + 480 min → [00:00, 08:00)
            day["minute"] = 0;
            shortList.append(day);
        }
        m_settings.setAutoWakeSchedule(shortList);
        const int dow = base.dayOfWeek() - 1; // base is Monday → dow == 0
        const bool expected = (dow < shortList.size()); // only present entries can match
        QCOMPARE(windowAt(probe), expected);
    }

    // An enabled day entry that omits hour/minute must default to 07:00,
    // mirroring scheduleNextWake()'s defaulting.
    void dayEntryMissingTimeKeysDefaultsToSevenAM() {
        const QDate base(2026, 5, 18);
        const int dow = base.dayOfWeek() - 1;
        QVariantList schedule;
        for (int i = 0; i < 7; ++i) {
            QVariantMap day;
            day["enabled"] = (i == dow);
            if (i != dow) { day["hour"] = 7; day["minute"] = 0; }
            // For `dow`: deliberately omit hour/minute → must default 07:00.
            schedule.append(day);
        }
        m_settings.setAutoWakeSchedule(schedule);
        m_settings.setAutoWakeStayAwakeMinutes(120); // 07:00 + 2 h → [07:00, 09:00)

        QVERIFY2(!windowAt(QDateTime(base, QTime(6, 30))), "before defaulted 07:00 start");
        QVERIFY2(windowAt(QDateTime(base, QTime(7, 30))),  "inside the defaulted 07:00 window");
        QVERIFY2(!windowAt(QDateTime(base, QTime(9, 1))),  "after the defaulted window");
    }
};

QTEST_GUILESS_MAIN(tst_AutoWakeWindow)
#include "tst_autowakewindow.moc"
