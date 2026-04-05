#pragma once

#include "mocks/MockTransport.h"
#include "ble/de1device.h"
#include "machine/machinestate.h"
#include "core/settings.h"
#include "controllers/profilemanager.h"
#include "mcp/mcptoolregistry.h"

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
    DE1Device device;
    MachineState machineState;
    // Suppress expected warnings during ProfileManager construction — test env has
    // no saved profile (falls back to default), no ai.qrc (knowledge base missing),
    // and may have stale favorites/currentProfile in real QSettings from the dev machine.
    // Filter must be declared before profileManager so it is constructed first and destroyed last.
    ScopedWarningFilter constructionFilter{"Profile not found|Failed to load profile knowledge|refreshProfiles: .*stale"};
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
        QJsonObject result;
        bool responded = false;

        registry.callAsyncTool(name, args, accessLevel, error,
            [&result, &responded](const QJsonObject& r) {
                result = r;
                responded = true;
            });

        if (!error.isEmpty()) {
            qWarning() << "callAsyncTool error:" << error;
            return result;
        }

        // Process events until the async handler responds
        QElapsedTimer timer;
        timer.start();
        while (!responded && timer.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        if (!responded)
            qWarning() << "callAsyncTool timed out waiting for response";
        return result;
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
