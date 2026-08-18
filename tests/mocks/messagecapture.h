#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QVector>
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
// And why not LogCollapse::suppressedFor(): that counts COLLAPSED repeats, so a
// line that prints for the first time leaves it at zero. Zero there is equally
// consistent with silence and with one printed line — blind in the direction
// that matters for a "was not logged" assertion.
//
// count() deliberately spans EVERY tier. A tier promotion is a normal edit to
// make to a noisy line, and it must turn a "not logged" assertion red rather
// than satisfying it. Use countAtTier() where the tier is itself the subject.
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
        // Depth-counted rather than one install per instance: a nested instance
        // that installed again would get &handler back as its "previous", and
        // the first message would recurse into handler() forever. Installing
        // once and walking the m_outer chain removes that failure mode instead
        // of documenting it.
        if (s_depth++ == 0)
            s_original = qInstallMessageHandler(&MessageCapture::handler);
    }

    ~MessageCapture() {
        s_active = m_outer;
        if (--s_depth == 0) {
            qInstallMessageHandler(s_original);
            s_original = nullptr;
        }
    }

    Q_DISABLE_COPY_MOVE(MessageCapture)

    void clear() { m_entries.clear(); }

    const QList<Entry>& entries() const { return m_entries; }

    // Messages containing `needle`, at ANY tier. See the class comment on why
    // this is not tier-filtered.
    int count(const QString& needle) const {
        int n = 0;
        for (const Entry& e : m_entries) {
            if (e.text.contains(needle))
                ++n;
        }
        return n;
    }

    int countAtTier(const QString& needle, QtMsgType type) const {
        int n = 0;
        for (const Entry& e : m_entries) {
            if (e.type == type && e.text.contains(needle))
                ++n;
        }
        return n;
    }

    // Last message containing `needle`, or an empty Entry.
    Entry last(const QString& needle) const {
        Entry found;
        for (const Entry& e : m_entries) {
            if (e.text.contains(needle))
                found = e;
        }
        return found;
    }

    // The one message containing `needle`, or false. Insists on EXACTLY one:
    // zero means the line vanished, more than one means the event was announced
    // twice, and a test that accepted either would not notice.
    bool single(const QString& needle, Entry* out) const {
        Entry found;
        int matches = 0;
        for (const Entry& e : m_entries) {
            if (e.text.contains(needle)) {
                found = e;
                ++matches;
            }
        }
        if (matches == 1 && out)
            *out = found;
        return matches == 1;
    }

private:
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
    static inline int s_depth = 0;
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
    static inline QVector<QRegularExpression*> s_filters;
    static inline QtMessageHandler s_originalHandler = nullptr;
    static inline int s_depth = 0;

    static void handler(QtMsgType type, const QMessageLogContext& ctx,
                        const QString& msg) {
        if (type == QtWarningMsg) {
            for (auto* f : s_filters) {
                if (f && f->match(msg).hasMatch())
                    return;  // Suppress
            }
        }
        if (s_originalHandler)
            s_originalHandler(type, ctx, msg);
    }

    QRegularExpression m_pattern;

    // A copy would register nothing but still decrement s_depth on destruction,
    // uninstalling the handler early and leaving a dangling &m_pattern in
    // s_filters. Nothing copies one today; this makes sure nothing starts.
    Q_DISABLE_COPY_MOVE(ScopedWarningFilter)

    explicit ScopedWarningFilter(const QString& pattern) : m_pattern(pattern) {
        s_filters.append(&m_pattern);
        if (s_depth++ == 0)
            s_originalHandler = qInstallMessageHandler(handler);
    }

    ~ScopedWarningFilter() {
        s_filters.removeOne(&m_pattern);
        if (--s_depth == 0) {
            qInstallMessageHandler(s_originalHandler);
            s_originalHandler = nullptr;
        }
    }
};
