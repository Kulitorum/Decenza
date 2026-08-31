#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

#include <limits>

// Collapses a repeating log line, carrying the count of what it stood in for.
//
// Two modes, and kChangesOnly is the one a periodic source wants: an unchanged line never repeats,
// however long it has been. A window (the other mode) is for a source already gated on a problem,
// where the repeat is itself evidence. See kChangesOnly for which is which and why.
//
// WHY THIS EXISTS AS ONE CLASS
// ----------------------------
// Three unrelated periodic sources — the MMR USB-charger keepalive (60 s), the memory sampler
// (60 s) and the ShotServer request log (a 5 s browser poll) — each wanted the same behaviour, and
// each was about to grow its own copy of it. In a 48-hour tablet capture they were 3,147 + 3,107 +
// 954 lines, 35% of the entire log, and every line was near-identical to the one before it. A
// fourth copy of "remember the last text, count repeats, print a summary" is exactly the drift the
// centralize rule in CLAUDE.md is about, so it lives here and they call it.
//
// It is deliberately NOT a general log filter and NOT tied to a category: callers keep their own
// qDebug()/qWarning() and their own wording. All this decides is whether to speak.
//
// NO TIMERS
// ---------
// The window is checked against the message's own arrival, not driven by a QTimer — a suppressed
// run is flushed by the next message in that run, not by a clock. That keeps this a pure decision
// function with no ownership, no thread affinity and nothing to stop, and it is why a source that
// falls silent mid-window simply stops: its last suppressed count is folded into the next line it
// does emit. For a periodic source (which is all three callers) the difference is one line at the
// end of the run, and in exchange there is no timer to leak or fire after teardown.
class LogCollapse
{
public:
    // ON RUN ENDS, which is the one thing a caller has to get right and the thing four of six got
    // wrong.
    //
    // A source that runs for the process lifetime cannot leak — the memory sampler, the battery
    // poll. Every OTHER source eventually goes quiet, and when it does its pending tally sits in
    // the table until the next run reuses the key and prints it annotated with a span measured to
    // THAT moment: last week's repeat count on today's first line. Such a source must flush() or
    // flushAll() at whatever event ends its run.
    //
    // This was a rule in a comment on flush() and it stayed one until someone checked the callers:
    // of six, three are episodic and two of those never flushed (the MMR keepalive across a
    // disconnect, the ShotServer request log across a server stop). Both are fixed, and every
    // declaration site now states which kind it is and where it flushes.
    //
    // It is NOT a constructor mode with a destructor assert. That was built and removed: process
    // exit is not a run end anyone can observe, so an episodic source holds a legitimate tally when
    // it is destroyed, and the assert aborted on a CORRECT caller — MqttClient, which flushes on
    // every reconnect, died at teardown. With the assert gone the mode had no reader at all and the
    // compiler said so (-Wunused-private-field). An enum nobody reads is a comment with a type, so
    // this is the comment.

    // A window meaning "an unchanged line is never worth repeating, at any spacing".
    //
    // This is the DEFAULT for a periodic source, and every periodic caller in the tree uses it. A
    // window was the original design and it was the wrong unit: it asks "how often may this line
    // repeat" when the question is "does a repeat carry anything". For a source whose text is
    // unchanged the answer does not improve with elapsed time — a byte-identical charger keepalive
    // an hour after the last one tells a reader exactly what the last one did, and costs a line of
    // a ring buffer that has to hold everything else. Six lines an hour is not noticeably better
    // than six hundred when none of them say anything.
    //
    // What replaces the periodic reassurance is the pair that always did the work: a CHANGED line,
    // which emits at once, and the run-end flush, which prints the tally. Between them the reader
    // gets the transition and the count it stood for, and nothing in between. The cost is that
    // silence no longer distinguishes "healthy" from "stopped" until the flush — accepted
    // deliberately, because for all five callers a death is visible in lines this one does not own
    // (a disconnect, a broker error, a server stop).
    //
    // A source that is already gated on a PROBLEM does not want this and must keep a real window:
    // its repeats are evidence, not reassurance. BleGattQueue's dispatch line is the one such
    // caller — it speaks only above a foreign-wait threshold, within episodes lasting under a
    // second, so a window is what separates two operations inside one stall.
    static constexpr qint64 kChangesOnly = std::numeric_limits<qint64>::max();

    // `windowMs` is the minimum spacing between emitted lines for a given key while the text is
    // unchanged, or kChangesOnly to never repeat one. A CHANGED text always emits immediately
    // regardless of the window — a value that moved is the interesting event, and holding it back
    // is how a collapser turns into a bug.
    explicit LogCollapse(qint64 windowMs) : m_windowMs(windowMs) {}


    // What a single emitted line stood in for. Both fields are needed to describe it honestly, and
    // they travel together for exactly that reason — see suffix().
    struct Collapsed
    {
        int suppressed = 0;   // identical lines swallowed since the last emit
        qint64 spanMs = 0;    // wall time those lines actually covered
    };

    // Returns true when the caller should log, and fills `out` with what the emitted line stands in
    // for (0/0 on the first line, or when the text changed).
    //
    // `nowMs` is passed in rather than read from a clock so the caller can reuse a timestamp it
    // already has, and so this is testable without waiting.
    bool shouldLog(const QString& key, const QString& text, qint64 nowMs, Collapsed* out)
    {
        Entry& e = m_entries[key];
        const bool changed = (e.text != text);
        // Spelled as an explicit branch rather than leaning on kChangesOnly being unreachably
        // large. The comparison would in fact never fire, but a sentinel that works by arithmetic
        // accident is one refactor away from a subtraction that wraps, and this reads as what it
        // means at no cost.
        const bool windowElapsed =
            (m_windowMs != kChangesOnly) && (nowMs - e.lastEmitMs) >= m_windowMs;

        if (!e.everEmitted || changed || windowElapsed) {
            if (out)
                *out = pending(e, nowMs);
            e.text = text;
            e.lastEmitMs = nowMs;
            e.suppressed = 0;
            e.everEmitted = true;
            return true;
        }

        e.suppressed++;
        if (out)
            *out = {};
        return false;
    }

    // How many keys are live, and whether one of them is `key`. For a caller that must BOUND its
    // key space: the collapse limits repeats within a key and says nothing about how many keys
    // exist, so a caller keying on attacker- or noise-supplied data (a frame's command byte, say)
    // has to fold past a cap of its own. See scaleFrameShapeLine().
    qsizetype keyCount() const { return m_entries.size(); }
    bool hasKey(const QString& key) const { return m_entries.contains(key); }

    // Ends a run: returns what is still pending for `key` and forgets the key entirely, so the next
    // run starts as a first sighting.
    //
    // For a periodic source there is no run end and this is never needed. For an EPISODIC one there
    // is, and without this the tally is not merely late — it is misattributed. A broker outage that
    // recovers stops calling shouldLog(), so its suppressed count sits in the table until the NEXT
    // outage prints it, stapling last week's repeat count onto today's first failure. The caller
    // that observes the recovery is the only code that knows the run ended, so it is the only place
    // that can say so.
    Collapsed flush(const QString& key, qint64 nowMs)
    {
        const auto it = m_entries.constFind(key);
        if (it == m_entries.cend())
            return {};
        const Collapsed c = pending(*it, nowMs);
        m_entries.erase(it);
        return c;
    }

    // Ends a run across EVERY key at once, returning what was still pending for each.
    //
    // flush() above takes a key because its callers observe the end of one named thing — a broker
    // reconnecting, a poll stopping. An episodic source that is keyed by something it does not
    // enumerate cannot use it: the GATT queue keys its dispatch line by operation LABEL, and a
    // contention episode touches whatever labels happened to be queued. Making each caller keep a
    // set of the keys it has used, purely so it can flush them, is the copy-per-caller this class
    // exists to prevent.
    //
    // The caller is expected to LOG what comes back. Dropping the return value silently discards
    // the tallies, which is the same misattribution as never flushing, only quieter.
    QList<QPair<QString, Collapsed>> flushAll(qint64 nowMs)
    {
        QList<QPair<QString, Collapsed>> out;
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            if (it->suppressed > 0)
                out.append({it.key(), pending(*it, nowMs)});
        }
        m_entries.clear();
        return out;
    }

    // Convenience for the common shape: " (+N identical in the preceding M s)" or an empty string.
    // Centralized so the five callers cannot word the same annotation five ways — which is what
    // happened to the [USB Scale] prefix (73 hand-written sites, 21 of them drifted).
    //
    // M IS THE MEASURED SPAN, NOT THE WINDOW. This took the window at first, and that was a lie
    // whenever the source was bursty rather than periodic. The window is only a MINIMUM: a run that
    // repeated for two minutes and then went quiet for three hours gets flushed by the next line
    // three hours later, and "in the last 600 s" would date a stale burst to the moment a reader is
    // looking at. For the periodic callers the two numbers are near-identical, which is precisely
    // why the error would have survived review — every log they produce looks right.
    static QString suffix(const Collapsed& c)
    {
        if (c.suppressed <= 0)
            return QString();
        return QStringLiteral(" (+%1 identical in the preceding %2 s)")
            .arg(c.suppressed)
            .arg(c.spanMs / 1000);
    }

private:
    struct Entry
    {
        QString text;
        qint64 lastEmitMs = 0;
        int suppressed = 0;
        bool everEmitted = false;
    };

    // One definition of what a pending tally is. The expression was written at three sites after
    // flushAll() arrived, in a file whose own suffix() comment records what happened last time this
    // class had one definition worded slightly differently at one of them.
    static Collapsed pending(const Entry& e, qint64 nowMs)
    {
        return {e.suppressed, e.everEmitted ? nowMs - e.lastEmitMs : 0};
    }

    qint64 m_windowMs;
    QHash<QString, Entry> m_entries;
};
