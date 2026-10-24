#pragma once

#include <QString>

// The native enum and session wire strings share one ordered state table.
#define DECENZA_PORTAL_STATES(X) \
    X(Disconnected, "disconnected") \
    X(Connecting, "connecting") \
    X(Discovering, "discovering") \
    X(Waiting, "waiting") \
    X(Streaming, "streaming") \
    X(Stale, "stale") \
    X(Error, "error") \
    X(Disconnecting, "disconnecting")

enum class PortalState {
#define DECENZA_DECLARE_PORTAL_STATE(name, wireName) name,
    DECENZA_PORTAL_STATES(DECENZA_DECLARE_PORTAL_STATE)
#undef DECENZA_DECLARE_PORTAL_STATE
};

inline QString portalStateName(PortalState state)
{
    switch (state) {
#define DECENZA_PORTAL_STATE_NAME(name, wireName) \
    case PortalState::name: return QStringLiteral(wireName);
        DECENZA_PORTAL_STATES(DECENZA_PORTAL_STATE_NAME)
#undef DECENZA_PORTAL_STATE_NAME
    }
    return {};
}

inline bool isPortalStateName(const QString& value)
{
#define DECENZA_PORTAL_STATE_MATCH(name, wireName) \
    if (value == QStringLiteral(wireName)) return true;
    DECENZA_PORTAL_STATES(DECENZA_PORTAL_STATE_MATCH)
#undef DECENZA_PORTAL_STATE_MATCH
    return false;
}
