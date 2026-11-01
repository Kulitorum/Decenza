#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QHash>

#include <functional>

#include "ble/protocol/de1characteristics.h"

class DE1Device;
class MachineState;

// Runs an americano or long black recipe as one drink (recipe-drink-sequence):
// after the first step's flow ends, request the second step once, at the later
// of the shot settling (americano only) and IDLE_DELAY_MS into the DE1's Idle.
// An americano arms on any espresso flow, so a GHC or MCP start sequences too; a
// long black arms only on the hot water its recipe tap started, so hot water for
// tea does not pull a shot. No firmware or GHC gate: firmware without the
// post-step window refuses the request, and that is the whole failure mode.
class DrinkSequenceController final : public QObject
{
    Q_OBJECT

public:
    enum class Step { None, HotWater, Espresso };
    Q_ENUM(Step)

    // A deliberate pause, not a guard: the maintainer's choice to let the cup
    // finish dripping. It must stay inside firmware 1359's 5 s window.
    static constexpr int IDLE_DELAY_MS = 4000;
    static constexpr int FIRMWARE_WINDOW_MS = 5000;
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
    Step pendingStep() const { return m_stage == Stage::Pending ? m_step : Step::None; }
    // From arming until the second step has finished, or is skipped, dropped or
    // refused. The drink stays on screen as one operation for this whole span.
    bool isActive() const { return m_stage != Stage::Idle; }
    // The recipe tap is starting a long black's hot water: arm on that flow only.
    void expectRecipeWater() { m_recipeWaterExpected = true; }
    // Drop the pending step for this drink only.
    void skip();
    // The espresso's settled end (ShotTimingController::shotProcessingReady).
    void onShotSettled();
    void setEspressoOperationProvider(std::function<qint64()> provider) { m_espressoOperation = std::move(provider); }
    void discardWaterForOperation(qint64 operation);

#ifdef DECENZA_TESTING
    void setIdleDelayForTesting(int ms) { m_idleDelay.setInterval(ms); }
    void setStartTimeoutForTesting(int ms) { m_startTimeout.setInterval(ms); }
#endif

signals:
    void pendingStepChanged();
    void activeChanged();
    void waterEspressoAssociated(qint64 operation);
    void waterResultReady(qint64 operation, const QVariantMap& result);

private:
    enum class Stage { Idle, Pending, AwaitingStart, Running };

    void onFlowStarted(int phase);
    void onDeviceStateChanged();
    void drop(const QString& why);
    void setStage(Stage stage, Step step);
    void fireIfReady();

    QPointer<DE1Device> m_device;
    QPointer<MachineState> m_machineState;
    ActiveRecipeProvider m_activeRecipe;
    Stage m_stage = Stage::Idle;
    Step m_step = Step::None;
    DE1::State m_firstStepState = DE1::State::Idle;
    // Set by a Skip made during the first step, cleared when the machine leaves
    // it, so a flow that restarts inside that operation cannot re-arm the step.
    bool m_skipped = false;
    bool m_recipeWaterExpected = false;
    bool m_shotSettled = false;
    bool m_idleDelayDone = false;
    QTimer m_idleDelay;
    QElapsedTimer m_sinceIdle;
    QTimer m_startTimeout;
    QString m_recipeName;
    struct WaterCapture {
        qint64 espressoOperation = 0;
        quint64 waterCycle = 0;
        QVariantMap result;
    };
    quint64 m_nextCaptureId = 0;
    quint64 m_activeCaptureId = 0;
    QHash<quint64, WaterCapture> m_waterCaptures;
    std::function<qint64()> m_espressoOperation;
    void publishWaterResult(quint64 captureId);
    void associateEspresso(quint64 captureId);

};
