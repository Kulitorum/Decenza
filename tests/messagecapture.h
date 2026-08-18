#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QtGlobal>

// Scoped qInstallMessageHandler helpers, in one place.
//
// There were four hand-rolled copies of this idea in tests/ — two of them
// verbatim duplicates of each other — and they had already drifted into three
// different answers for the same question. Worse, the newest copy was the
// weakest: it filtered on QtDebugMsg alone, so promoting the line it watched to
// qInfo would have made every "this was not logged" assertion pass while the
// log got LOUDER, which is the exact regression its test file exists to prevent.
// One definition, so a fix to the shape reaches every caller.
//
// Two types, because there are two genuinely different jobs and merging them
// would produce a framework nobody wants:
//
//   MessageCapture      — record messages so a test can ASSERT on them
//   ScopedWarningFilter — suppress expected noise so it does not fail the run
//
// Header-only and dependency-free on purpose. tests/mocks/McpTestFixture.h used
// to host ScopedWarningFilter, and tst_decentscalewifi.cpp inlined its own copy
// specifically to avoid pulling in that header's Settings/MockTransport. This
// header pulls in nothing, so that reason to copy is gone.

// Records messages so a test can assert on their COUNT, TEXT and TIER.
//
// Why this rather than QTest::ignoreMessage: ignoreMessage is a PERMISSION, not
// an assertion. An unmatched pattern is reported by printUnhandledIgnoreMessages()
// via addMessage() with QAbstractTestLogger::Info
// (qtbase/src/testlib/qtestlog.cpp:397-419) — a printed line, never a failure.
// So a test built on ignoreMessage alone still passes if the line under test is
// demoted a tier or deleted outright.
//
// The predecessor of this class asserted silence by reading a collapsed-repeat
// count, which is zero both when nothing was logged and when one line printed —
// blind in the one direction a "was not logged" assertion needs. Counting the
// emitted lines is the assertion; anything derived from the collapser is not.
class MessageCapture {
public:
    // Whether messages also reach the handler that was installed before this
    // one. Chain when the surrounding QTest machinery must keep working —
    // QTest::ignoreMessage and QTest::failOnWarning both live in that handler.
    // Swallow when the test is ASSERTING on a warning that failOnWarning would
    // otherwise treat as a failure.
    enum Chaining { ChainToPrevious, SwallowAll };

    struct Entry {
        QtMsgType type = QtDebugMsg;
        QString text;
    };

    explicit MessageCapture(Chaining chaining = ChainToPrevious)
        : m_chaining(chaining) {
        m_outer = s_active;
        s_active = this;
        // Installed ONCE, for the outermost instance only. A nested instance
        // that installed again would get &handler back as its "previous" and the
        // first message would recurse into handler() forever; walking the
        // m_outer chain removes that failure mode instead of documenting it.
        // No depth counter — an empty chain is depth zero.
        if (!m_outer)
            s_original = qInstallMessageHandler(&MessageCapture::handler);
    }

    ~MessageCapture() {
        s_active = m_outer;
        if (!s_active) {
            qInstallMessageHandler(s_original);
            s_original = nullptr;
        }
    }

    Q_DISABLE_COPY_MOVE(MessageCapture)

    void clear() { m_entries.clear(); }

    // Messages containing `needle`, at ANY tier. Not tier-filtered, deliberately:
    // a tier promotion is a normal edit to make to a noisy line, and it must
    // turn a "was not logged" assertion red rather than satisfying it. Filtering
    // to QtDebugMsg is what made the first version of this helper blind in the
    // one direction that mattered.
    qsizetype count(const QString& needle) const { return matching(needle).size(); }

    // The one message containing `needle`, or false. Insists on EXACTLY one:
    // zero means the line vanished, more than one means the event was announced
    // twice, and a test that accepted either would not notice. Carries the tier
    // out with it, so a slot asserting ON the tier reads `.type`.
    bool single(const QString& needle, Entry* out) const {
        const QList<Entry> m = matching(needle);
        if (m.size() == 1 && out)
            *out = m.first();
        return m.size() == 1;
    }

private:
    QList<Entry> matching(const QString& needle) const {
        QList<Entry> out;
        for (const Entry& e : m_entries) {
            if (e.text.contains(needle))
                out.append(e);
        }
        return out;
    }

    static void handler(QtMsgType type, const QMessageLogContext& ctx,
                        const QString& msg) {
        // Every live capture records, innermost first. A nested capture does
        // not hide the message from the one that encloses it.
        bool swallow = false;
        for (MessageCapture* c = s_active; c; c = c->m_outer) {
            c->m_entries.append({type, msg});
            if (c->m_chaining == SwallowAll)
                swallow = true;
        }
        // Any live capture asking to swallow wins. A test that installed one to
        // keep a warning away from failOnWarning must get that, even if an
        // enclosing capture is content to chain.
        if (!swallow && s_original)
            s_original(type, ctx, msg);
    }

    QList<Entry> m_entries;
    MessageCapture* m_outer = nullptr;
    Chaining m_chaining = ChainToPrevious;

    static inline MessageCapture* s_active = nullptr;
    static inline QtMessageHandler s_original = nullptr;
};

// Suppresses qWarning messages matching a pattern, so expected noise does not
// fail a run under QTest::failOnWarning().
//
// Nestable: patterns are pushed onto a stack and a warning is suppressed if it
// matches ANY active filter. Non-matching messages forward to whatever handler
// was installed before, so QTest::ignoreMessage still works alongside it.
//
// The distinction that matters: ignoreMessage REQUIRES its message to fire,
// this only ALLOWS it.
struct ScopedWarningFilter {
    // Same chain as MessageCapture above, for the same reason: one idiom for
    // "nest a message handler" in this header rather than two. It replaced a
    // QVector<QRegularExpression*> plus a depth counter, whose only real cost
    // was a dangling-pointer hazard that existed solely BECAUSE the pattern was
    // parked in a container — the chain has nowhere to dangle. Requires LIFO
    // destruction, which stack scoping gives for free.
    explicit ScopedWarningFilter(const QString& pattern) : m_pattern(pattern) {
        m_outer = s_active;
        s_active = this;
        if (!m_outer)
            s_original = qInstallMessageHandler(&ScopedWarningFilter::handler);
    }

    ~ScopedWarningFilter() {
        s_active = m_outer;
        if (!s_active) {
            qInstallMessageHandler(s_original);
            s_original = nullptr;
        }
    }

    Q_DISABLE_COPY_MOVE(ScopedWarningFilter)

private:
    static void handler(QtMsgType type, const QMessageLogContext& ctx,
                        const QString& msg) {
        if (type == QtWarningMsg) {
            for (const ScopedWarningFilter* f = s_active; f; f = f->m_outer) {
                if (f->m_pattern.match(msg).hasMatch())
                    return;  // Suppressed by this filter or one enclosing it.
            }
        }
        if (s_original)
            s_original(type, ctx, msg);
    }

    QRegularExpression m_pattern;
    ScopedWarningFilter* m_outer = nullptr;

    static inline ScopedWarningFilter* s_active = nullptr;
    static inline QtMessageHandler s_original = nullptr;
};
