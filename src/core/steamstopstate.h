#pragma once

// Host-owned two-stage steam stop state. The UI supplies the state it rendered
// so an old first-tap request cannot become a purge after another client or a
// new machine cycle has changed the host state.
class SteamStopState final
{
public:
    enum class Decision {
        Rejected,
        SoftStop,
        Purge
    };

    bool softStopped() const { return m_softStopped; }
    bool purgeRequested() const { return m_purgeRequested; }

    // The DE1 keeps its raw Steam state through heating, Puffing and Ending.
    // Reset only when that raw operation has ended or the transport is gone.
    void observeMachine(bool connected, bool rawSteam)
    {
        if (!connected || !rawSteam) {
            m_softStopped = false;
            m_purgeRequested = false;
        }
    }

    Decision request(bool connected, bool rawSteam, bool twoTapEnabled,
                     bool expectedSoftStopped)
    {
        if (!connected || !rawSteam || m_purgeRequested
            || expectedSoftStopped != m_softStopped) {
            return Decision::Rejected;
        }

        if (twoTapEnabled && !m_softStopped) {
            m_softStopped = true;
            return Decision::SoftStop;
        }

        // Keep the rendered state until the raw Steam cycle ends. This both
        // preserves the Purge label and prevents a duplicate request from
        // being reinterpreted as a new first tap while requestIdle is pending.
        m_purgeRequested = true;
        return Decision::Purge;
    }

private:
    bool m_softStopped = false;
    bool m_purgeRequested = false;
};
