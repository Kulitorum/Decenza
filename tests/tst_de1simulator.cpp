// DE1Simulator frame stepping.
//
// The simulator drives every shot pulled without a machine attached, so a fault
// here reads as a fault in the profile. That is how this was found: an A-Flow
// shot poured its entire yield through `Flow Start` at the pour flow rate and
// never reached `Flow Extraction`, which looked like a recipe-editor bug and was
// not — the profile was correct and matched the plugin exactly.
//
// The cause was `frameTime >= frame.seconds && frame.seconds > 0`: a zero-length
// frame never timed out, so it ran forever. **A zero-length frame is a DISABLED
// frame.** Both upstream plugins say "this step is off" that way, and Decenza
// reads them back the same way — `rampDownEnabled` and `secondFillEnabled` are
// literally `frame.seconds > 0`.
//
// It hid because a disabled frame usually also carries an exit condition, which
// advanced past it within a tick. Only `Flow Start` — disabled with `exit_if
// false` — had nothing else to end it.

#include <QtTest>
#include <QSignalSpy>

#include "../src/simulator/de1simulator.h"
#include "../src/ble/de1device.h"
#include "../src/profile/profile.h"
#include "../src/profile/profileframe.h"

namespace {

// Durations are deliberately short: the simulator runs on a 100 ms wall-clock
// tick, so frame lengths are real seconds and a leisurely fixture makes a slow
// test. Nothing here depends on the values being realistic.
ProfileFrame frame(const QString& name, double seconds, double flow = 2.0) {
    ProfileFrame f;
    f.name = name;
    f.seconds = seconds;
    f.flow = flow;
    f.pressure = 6.0;
    f.pump = QStringLiteral("flow");
    f.temperature = 93.0;
    f.transition = QStringLiteral("fast");
    f.volume = 0.0;       // no volume exit — the length is the only thing under test
    f.exitIf = false;     // and no exit condition, which is the case that hid the bug
    return f;
}

Profile profileOf(const QList<ProfileFrame>& frames) {
    Profile p;
    p.setTitle(QStringLiteral("Simulator Frame Test"));
    p.setSteps(frames);
    p.setTargetWeight(0.0);   // never stop on weight; let the frames run out
    p.setTargetVolume(0.0);
    return p;
}

// Run the simulator forward once, collecting the frame numbers it reports.
//
// ONE run, not one per scenario: the simulator holds the valve shut for a fixed
// 5 s preheat before any frame starts, so each extra run costs five real seconds
// for nothing. Every case below is folded into a single profile instead.
//
// Stops as soon as the last frame is seen — waiting for the shot to finish adds
// the whole ending phase and tells us nothing more. The wall-clock bound is not
// belt-and-braces: a frame that never expires is the exact failure under test,
// and without the bound that failure is a hung suite rather than a red test.
QList<int> framesVisited(const Profile& profile, int maxMs = 20000) {
    DE1Simulator sim;
    sim.setProfile(profile);

    const int lastFrame = int(profile.steps().size()) - 1;
    QList<int> visited;
    QObject::connect(&sim, &DE1Simulator::shotSampleReceived,
                     [&visited](const ShotSample& s) {
                         if (visited.isEmpty() || visited.last() != s.frameNumber)
                             visited << s.frameNumber;
                     });

    sim.startEspresso();
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < maxMs && sim.isRunning() && !visited.contains(lastFrame))
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    sim.stop();
    return visited;
}

QString describe(const QList<int>& visited) {
    QStringList s;
    for (int f : visited) s << QString::number(f);
    return s.join(QStringLiteral(" -> "));
}

} // namespace

class tst_De1Simulator : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void aFrameWithBothAnExitConditionAndZeroLengthAdvancesOnlyOnce() {
        // The OTHER half of the fix, which the test below cannot reach.
        //
        // executeFrame had three independent `if`s — exit condition, frame
        // length, volume — each calling advanceToNextFrame(). Safe only while
        // they could not both hold. Removing the zero-length guard makes them
        // hold together routinely: a disabled frame carrying an exit condition
        // advances on the condition AND on its 0 s length, skipping the frame
        // after it. Both later tests also read `frame`/`frameTime`, captured
        // before the first advance, so they judge the frame already left.
        //
        // Every frame in the test below sets exitIf = false, and
        // checkExitCondition returns immediately when that is false — so the
        // exit-condition branch is dead there and reverting the `return`s would
        // not change its result. This fixture gives frame 1 BOTH a satisfied
        // exit condition and zero length, which is the case that double-advances.
        ProfileFrame trap = frame(QStringLiteral("Pause"), 0.0);
        trap.exitIf = true;
        trap.exitType = QStringLiteral("flow_under");
        trap.exitFlowUnder = 9.0;   // flow starts well below this, so it fires at once

        const Profile p = profileOf({
            frame(QStringLiteral("Fill"), 0.5),
            trap,
            frame(QStringLiteral("Pressure Up"), 0.5),
            frame(QStringLiteral("Pour"), 0.5, 4.0),
        });

        const QList<int> visited = framesVisited(p);
        QVERIFY2(visited.contains(2),
                 qPrintable(QStringLiteral("frame 2 was skipped — frame 1 advanced twice in "
                                           "one tick, once on its exit condition and once on "
                                           "its zero length. Frames seen: %1")
                            .arg(describe(visited))));
        QVERIFY2(visited.contains(3),
                 qPrintable(QStringLiteral("frame 3 never ran. Frames seen: %1")
                            .arg(describe(visited))));
    }

    void zeroLengthFramesExpireAndRealOnesDoNot() {
        // The shape of a real A-Flow profile with every optional step switched
        // off, which is how the bug reached a shot:
        //
        //   0 Fill            0.5 s   real
        //   1 2nd Fill        0 s     disabled  ) adjacent pair — must not
        //   2 Pause           0 s     disabled  ) deadlock or double-skip
        //   3 Pressure Up     0.5 s   real
        //   4 Flow Start      0 s     disabled  — the lone one, no exit condition,
        //                                        which is why only this one showed
        //   5 Flow Extraction 0.5 s   real
        //
        // No frame here carries an exit condition or a volume cap, so frame
        // length is the only thing that can end one. That is deliberate: the
        // disabled frames used to be carried past by their exit conditions, which
        // is exactly why the fault stayed hidden for so long.
        const Profile p = profileOf({
            frame(QStringLiteral("Fill"), 0.5),
            frame(QStringLiteral("2nd Fill"), 0.0),
            frame(QStringLiteral("Pause"), 0.0),
            frame(QStringLiteral("Pressure Up"), 0.5),
            frame(QStringLiteral("Flow Start"), 0.0),
            frame(QStringLiteral("Flow Extraction"), 0.5, 4.0),
        });

        const QList<int> visited = framesVisited(p);

        // The zero-length frames expired: the shot reached the far side of them.
        QVERIFY2(visited.contains(5),
                 qPrintable(QStringLiteral("frame 5 never ran — a zero-length frame did not "
                                           "expire and swallowed the shot. Frames seen: %1")
                            .arg(describe(visited))));

        // And the real frames were not stepped over. This is the other half of
        // the fix: advancing at most once per tick. With three independent
        // advance paths, a disabled frame could advance on its exit condition and
        // again on its zero length, jumping the frame after it.
        for (int real : {0, 3, 5})
            QVERIFY2(visited.contains(real),
                     qPrintable(QStringLiteral("frame %1 has a real duration and was skipped. "
                                               "Frames seen: %2")
                                .arg(real).arg(describe(visited))));

        // Order is monotonic — no frame revisited, none run out of sequence.
        for (qsizetype i = 1; i < visited.size(); ++i)
            QVERIFY2(visited[i] > visited[i - 1],
                     qPrintable(QStringLiteral("frames ran out of order: %1")
                                .arg(describe(visited))));
    }
};

QTEST_MAIN(tst_De1Simulator)
#include "tst_de1simulator.moc"
