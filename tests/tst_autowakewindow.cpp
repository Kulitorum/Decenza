#include <QtTest>

#include "core/autowakemanager.h"
#include "core/settings_autowake.h"

// Tests for AutoWakeManager::isWithinStayAwakeWindowAt() — the continuously
// evaluated "are we inside a scheduled stay-awake window?" predicate that
// replaced the one-shot countdown (Kulitorum/Decenza#1203). The schedule
// must take priority over the generic auto-off, and a window that began
// before midnight must keep applying into the next day.
//
// SettingsAutoWake persists to QSettings("DecentEspresso", "DE1Qt"); the
// stay-awake keys are saved in init() and restored in cleanup() (runs even
// on assertion failure) so the developer's real config is untouched.

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

    bool windowAt(const QDateTime& now) {
        AutoWakeManager mgr(&m_settings);
        return mgr.isWithinStayAwakeWindowAt(now);
    }

private slots:

    void init() {
        m_origSchedule = m_settings.autoWakeSchedule();
        m_origStayEnabled = m_settings.autoWakeStayAwakeEnabled();
        m_origStayMinutes = m_settings.autoWakeStayAwakeMinutes();
        m_settings.setAutoWakeStayAwakeEnabled(true);
    }

    void cleanup() {
        m_settings.setAutoWakeSchedule(m_origSchedule);
        m_settings.setAutoWakeStayAwakeEnabled(m_origStayEnabled);
        m_settings.setAutoWakeStayAwakeMinutes(m_origStayMinutes);
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
        QVERIFY2(windowAt(QDateTime(nextDay, QTime(2, 0))),    "after midnight, still inside");
        QVERIFY2(windowAt(QDateTime(nextDay, QTime(5, 59))),   "one minute before spill end");
        QVERIFY2(!windowAt(QDateTime(nextDay, QTime(6, 0))),   "spill end (exclusive)");
        QVERIFY2(!windowAt(QDateTime(nextDay, QTime(9, 0))),   "next day, not itself scheduled");
    }

    // The 12 h cap added in #1204 must be honored end-to-end.
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
        m_settings.setAutoWakeStayAwakeMinutes(480);

        // Enable only the *other* day; today's weekday is no longer scheduled.
        m_settings.setAutoWakeSchedule(scheduleWith((dow + 1) % 7, 6, 10));
        QVERIFY2(!windowAt(inside), "current weekday not scheduled");
    }
};

QTEST_GUILESS_MAIN(tst_AutoWakeWindow)
#include "tst_autowakewindow.moc"
