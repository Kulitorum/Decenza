#pragma once

#include "mocks/MockTransport.h"
#include "ble/de1device.h"
#include "machine/machinestate.h"
#include "core/settings.h"
#include "controllers/profilemanager.h"
#include "mcp/mcptoolregistry.h"

#include <memory>

// Shared test fixture for MCP tool tests.
// Wires ProfileManager with a MockTransport so BLE writes can be verified.
//
// Usage:
//   McpTestFixture f;
//   registerProfileTools(f.registry, f.profileManager);
//   auto result = f.callTool("profiles_list", {});

// RAII guard to suppress qWarning messages matching a pattern.
// Nestable: patterns are pushed onto a stack. A warning is suppressed if it
// matches ANY active filter's pattern. Non-matching messages are forwarded
// to Qt Test's handler so QTest::ignoreMessage still works.
struct ScopedWarningFilter {
    static inline QVector<QRegularExpression*> s_filters;
    static inline QtMessageHandler s_originalHandler = nullptr;
    static inline int s_depth = 0;

    static void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
        if (type == QtWarningMsg) {
            for (auto* f : s_filters) {
                if (f && f->match(msg).hasMatch())
                    return;  // Suppress
            }
        }
        // Forward to Qt Test's handler
        if (s_originalHandler)
            s_originalHandler(type, ctx, msg);
    }

    QRegularExpression m_pattern;

    ScopedWarningFilter(const QString& pattern) : m_pattern(pattern) {
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

struct McpTestFixture {
    QTemporaryDir tempDir;   // isolated profile storage
    Settings settings;
    MockTransport transport;
    // Profile upload verification (DE1Device::finishProfileUpload) emits qWarning
    // when an upload doesn't complete cleanly. In tests, MockTransport never ACKs
    // writes, so every uploadProfile() call that's followed by fixture teardown
    // (BLE disconnect — fires from ~DE1Device), a rapid second uploadProfile()
    // (supersede), or an explicit clearCommandQueue() ends up as a "FAILED"
    // verdict. These are expected test-harness artifacts, not real failures —
    // silence them. Unexpected reasons (frame sequence mismatch, ACK timeout)
    // are intentionally NOT suppressed so they still surface if a test exposes
    // one. tst_profileupload.cpp exercises the verification path directly and
    // doesn't use this fixture, so its assertions are unaffected.
    //
    // Declared before `device` so the filter outlives ~DE1Device (members are
    // destroyed in reverse declaration order).
    ScopedWarningFilter uploadFilter{"profile upload FAILED — (BLE disconnect during upload|superseded by a new upload|command queue cleared during upload)"};
    DE1Device device;
    MachineState machineState;
    // Suppress expected warnings during ProfileManager construction — test env has
    // no saved profile (falls back to default), no ai.qrc (knowledge base missing),
    // and may have stale favorites/currentProfile in real QSettings from the dev machine.
    // Filter must be declared before profileManager so it is constructed first and destroyed last.
    ScopedWarningFilter constructionFilter{"Profile not found|Failed to load profile knowledge|refreshProfiles: .*stale|Settings: addFavoriteProfile|Settings: removeFavoriteProfile"};
    ProfileManager profileManager;
    McpToolRegistry registry;

    McpTestFixture()
        : machineState(&device)
        , profileManager(&settings, &device, &machineState)
    {
        device.setTransport(&transport);
        // Point profile storage at temp dir so tests don't touch real profiles
        settings.setValue("profile/path", tempDir.path());
    }

    // Helper: call a sync tool and return the result
    QJsonObject callTool(const QString& name, const QJsonObject& args, int accessLevel = 2)
    {
        QString error;
        QJsonObject result = registry.callTool(name, args, accessLevel, error);
        if (!error.isEmpty())
            qWarning() << "callTool error:" << error;
        return result;
    }

    // Helper: call an async tool, block until respond() fires, return result
    QJsonObject callAsyncTool(const QString& name, const QJsonObject& args, int accessLevel = 2)
    {
        QString error;
        // Shared state, NOT captures of this frame's locals.
        //
        // The callback outlives this function on the timeout path below: it
        // returns after 5 s with queued work still in flight, and that work can
        // fire during a LATER test's processEvents. Capturing &result/&responded
        // by reference would then write into a destroyed stack frame — a
        // stack-use-after-return, which is exactly what the ASan job this
        // fixture feeds exists to catch. Pre-existing, but the window widened
        // when bag_update gained a main-thread hop (one more event-loop cycle
        // before it can respond), so it is fixed here rather than noted.
        struct AsyncState { QJsonObject result; bool responded = false; };
        auto state = std::make_shared<AsyncState>();

        registry.callAsyncTool(name, args, accessLevel, error,
            [state](const QJsonObject& r) {
                state->result = r;
                state->responded = true;
            });

        if (!error.isEmpty()) {
            qWarning() << "callAsyncTool error:" << error;
            return state->result;
        }

        // Process events until the async handler responds
        QElapsedTimer timer;
        timer.start();
        while (!state->responded && timer.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        if (!state->responded)
            qWarning() << "callAsyncTool timed out waiting for response";

        // A leak that lived here, and the two wrong explanations for it.
        //
        // tst_mcptools_write leaked 6,540 bytes / 70 allocations under
        // LeakSanitizer, at the QThread::create sites in mcptools_write.cpp's
        // headless static-fallback paths. RESOLVED — see the long note at
        // src/mcp/mcptools_write.cpp:1774. Kept here because both failed
        // diagnoses pointed at THIS function, and the record is what stops the
        // next person re-deriving them:
        //
        //   1. "There is no event loop, so deleteLater never runs." Wrong about
        //      this function — the loop above is one.
        //   2. "The loop returns the instant the response lands, before
        //      QThread::finished fires, so the queued deleteLater is never
        //      delivered." Tested: draining events for 250 ms here plus an
        //      explicit sendPostedEvents(DeferredDelete) changed the leak by
        //      exactly ZERO bytes. Reverted rather than kept.
        //
        // The real cause was neither: respondWithBag() ran on a QThread::create
        // WORKER, so the QThread it built inherited that worker's affinity and
        // its deleteLater was posted to a queue nothing would ever pump. #2
        // failed precisely because the pending delete was never on the main
        // thread's queue — the thing this loop drains. Fixed by hopping to the
        // main thread before creating that thread; nightly ASan now reports
        // zero (run 29707910438).
        //
        return state->result;
    }

    // Helper: find BLE writes to a specific characteristic UUID
    QList<QByteArray> writesTo(const QBluetoothUuid& uuid) const
    {
        QList<QByteArray> result;
        for (const auto& [writeUuid, data] : transport.writes) {
            if (writeUuid == uuid)
                result.append(data);
        }
        return result;
    }
};
