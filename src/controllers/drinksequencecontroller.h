#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include <functional>

#include "ble/protocol/de1characteristics.h"

class DE1Device;
class MachineState;

// Runs an americano or long black recipe as one drink (recipe-drink-sequence):
// after the first step's flow ends, request the second step once, at the later
// of the shot settling (americano only) and IDLE_DELAY_MS into the DE1's Idle.
// Keyed on machine events, so a GHC or MCP start sequences too. No firmware or
// GHC gate: firmware without the post-step window refuses the request, and that
// is the whole failure mode.
class DrinkSequenceController final : public QObject
{
    Q_OBJECT

public:
    enum class Step { None, HotWater, Espresso };
    Q_ENUM(Step)

    // A deliberate pause, not a guard: the maintainer's choice to let the cup
    // finish dripping. It must stay inside firmware 1359's 5 s window.
    static constexpr int IDLE_DELAY_MS = 4000;
    // The DE1 sends nothing when it refuses a request, so a second step that has
    // not started this long after the request is taken as refused. A deliberate,
    // documented exception to the "no timers as guards" rule, like startEspresso's
    // profile-upload settle: there is no event to key on (design D9).
    static constexpr int START_TIMEOUT_MS = 3000;

    // Returns the active recipe's raw fields (MainController::activeRecipe).
    using ActiveRecipeProvider = std::function<QVariantMap()>;

    DrinkSequenceController(DE1Device* device, MachineState* machineState,
                            ActiveRecipeProvider activeRecipe, QObject* parent = nullptr);

    // The step that will be requested when the current one ends.
    Step pendingStep() const { return m_pending; }
    // From arming until the second step has finished, or is skipped, dropped or
    // refused. The drink stays on screen as one operation for this whole span.
    bool isActive() const
    {
        return m_pending != Step::None || m_awaitingStart != Step::None || m_running != Step::None;
    }
    // Drop the pending step for this drink only.
    void skip();
    // The espresso's settled end (ShotTimingController::shotProcessingReady).
    void onShotSettled();

#ifdef DECENZA_TESTING
    void setIdleDelayForTesting(int ms) { m_idleDelay.setInterval(ms); }
    void setStartTimeoutForTesting(int ms) { m_startTimeout.setInterval(ms); }
#endif

signals:
    void pendingStepChanged();
    void activeChanged();

private:
    void onFlowStarted(int phase);
    void onDeviceStateChanged();
    void drop(const QString& why);
    void setPending(Step step);
    void fireIfReady();
    void endAwaitingStart(const QString& refusedWhy);
    void finishDrink();

    QPointer<DE1Device> m_device;
    QPointer<MachineState> m_machineState;
    ActiveRecipeProvider m_activeRecipe;
    Step m_pending = Step::None;
    DE1::State m_firstStepState = DE1::State::Idle;
    // Set by Skip, cleared when the machine leaves the first step, so a flow
    // that restarts inside the same operation cannot re-arm what was skipped.
    bool m_skipped = false;
    bool m_shotSettled = false;
    bool m_idleDelayDone = false;
    QTimer m_idleDelay;
    Step m_awaitingStart = Step::None;
    Step m_running = Step::None;
    QTimer m_startTimeout;
    QString m_recipeName;
};
