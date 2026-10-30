#pragma once

// Keep the machine phase vocabulary in one place. Expand this list to make both
// the shared value type and MachineState's QML-visible nested enum.
#define DECENZA_MACHINE_PHASES(X) \
    X(Disconnected)                 \
    X(Sleep)                        \
    X(Idle)                         \
    X(Heating)                      \
    X(Ready)                        \
    X(EspressoPreheating)           \
    X(Preinfusion)                  \
    X(Pouring)                      \
    X(Ending)                       \
    X(Steaming)                     \
    X(HotWater)                     \
    X(Flushing)                     \
    X(Refill)                       \
    X(Descaling)                    \
    X(Cleaning)                     \
    X(Transport)                    \
    X(SteamPurge)

enum class MachinePhase {
#define DECENZA_DECLARE_MACHINE_PHASE(name) name,
    DECENZA_MACHINE_PHASES(DECENZA_DECLARE_MACHINE_PHASE)
#undef DECENZA_DECLARE_MACHINE_PHASE
};
