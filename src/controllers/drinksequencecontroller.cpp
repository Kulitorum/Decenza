#include "drinksequencecontroller.h"

#include "ble/de1device.h"
#include "core/diagnosticlogging.h"
#include "core/recipeeditfields.h"
#include "machine/machinestate.h"
#include "history/shotwaterresult.h"

namespace {
QString stepName(DrinkSequenceController::Step step)
{
    return step == DrinkSequenceController::Step::HotWater ? QStringLiteral("hot water")
                                                           : QStringLiteral("espresso");
}

// A state the machine stays in for a reason of its own, so a drink waiting on
// it is over. Anything else (Busy, Refill, SchedIdle, ...) is passed through.
bool endsDrinkWait(DE1::State state)
{
    switch (state) {
    case DE1::State::Sleep: case DE1::State::GoingToSleep:
    case DE1::State::Espresso: case DE1::State::Steam: case DE1::State::HotWater:
    case DE1::State::HotWaterRinse: case DE1::State::SteamRinse: case DE1::State::Descale:
    case DE1::State::Clean: case DE1::State::AirPurge:
        return true;
    default:
        return false;
    }
}

DE1::State stateFor(DrinkSequenceController::Step step)
{
    return step == DrinkSequenceController::Step::HotWater ? DE1::State::HotWater
                                                           : DE1::State::Espresso;
}
}  // namespace

DrinkSequenceController::DrinkSequenceController(DE1Device* device, MachineState* machineState,
                                                 ActiveRecipeProvider activeRecipe,
                                                 QObject* parent)
    : QObject(parent), m_device(device), m_machineState(machineState),
      m_activeRecipe(std::move(activeRecipe))
{
    m_idleDelay.setSingleShot(true);
    m_idleDelay.setInterval(IDLE_DELAY_MS);
    connect(&m_idleDelay, &QTimer::timeout, this, [this] {
        m_idleDelayDone = true;
        fireIfReady();
    });
    m_startTimeout.setSingleShot(true);
    m_startTimeout.setInterval(START_TIMEOUT_MS);
    connect(&m_startTimeout, &QTimer::timeout, this, [this] {
        if (m_stage != Stage::AwaitingStart)
            return;
        DIAG_WARN(RECIPES, "DrinkSequenceController").noquote()
            << QStringLiteral("'%1': %2 did not start within %3 s of the request, so the DE1 "
                              "likely refused it (firmware %4, GHC %5); firmware before 1359 "
                              "needs a tap on the group head")
                   .arg(m_recipeName, stepName(m_step))
                   .arg(m_startTimeout.interval() / 1000.0)
                   .arg(m_device ? m_device->firmwareBuildNumber() : 0)
                   .arg(m_device && m_device->isHeadless() ? QStringLiteral("inactive")
                                                           : QStringLiteral("active"));
        setStage(Stage::Idle, Step::None);
    });
    if (m_machineState) {
        connect(m_machineState, &MachineState::shotFlowStarted,
                this, &DrinkSequenceController::onFlowStarted);
        connect(m_machineState, &MachineState::hotWaterResultReady, this,
                [this](quint64 cycle, const QVariantMap& result) {
            for (auto it = m_waterCaptures.begin(); it != m_waterCaptures.end(); ++it) {
                if (it->waterCycle != cycle) continue;
                it->result = result;
                publishWaterResult(it.key());
                break;
            }
        });
    }
    if (m_device) {
        connect(m_device, &DE1Device::stateChanged,
                this, &DrinkSequenceController::onDeviceStateChanged);
        connect(m_device, &DE1Device::connectedChanged, this, [this] {
            if (!m_device || m_device->isConnected())
                return;
            m_recipeWaterExpected = false;
            drop(QStringLiteral("the DE1 disconnected"));
        });
    }
}

void DrinkSequenceController::onFlowStarted(int phase)
{
    const bool recipeWater = std::exchange(m_recipeWaterExpected, false);
    const auto phaseValue = static_cast<MachineState::Phase>(phase);
    const bool espresso = phaseValue == MachineState::Phase::Preinfusion
                       || phaseValue == MachineState::Phase::Pouring;
    if (m_activeCaptureId && (m_stage == Stage::Pending || m_stage == Stage::AwaitingStart
                              || m_stage == Stage::Running)) {
        if (m_step == Step::Espresso && espresso)
            associateEspresso(m_activeCaptureId);
        else if (m_step == Step::HotWater && phaseValue == MachineState::Phase::HotWater) {
            auto it = m_waterCaptures.find(m_activeCaptureId);
            if (it != m_waterCaptures.end()) it->waterCycle = m_machineState->hotWaterCycleId();
        }
    }
    if (!m_device || !m_activeRecipe || m_skipped || m_stage != Stage::Idle)
        return;
    const QVariantMap recipe = m_activeRecipe();
    if (recipe.isEmpty())
        return;
    const RecipeEditFields::DrinkSequence sequence = RecipeEditFields::drinkSequence(recipe);

    const auto p = static_cast<MachineState::Phase>(phase);
    const bool espressoFlow = p == MachineState::Phase::Preinfusion
                           || p == MachineState::Phase::Pouring;
    Step next = Step::None;
    if (espressoFlow && sequence == RecipeEditFields::DrinkSequence::WaterAfter)
        next = Step::HotWater;
    else if (p == MachineState::Phase::HotWater && recipeWater
             && sequence == RecipeEditFields::DrinkSequence::WaterBefore)
        next = Step::Espresso;
    if (next == Step::None)
        return;

    m_activeCaptureId = ++m_nextCaptureId;
    m_waterCaptures.insert(m_activeCaptureId, {});
    if (next == Step::HotWater)
        associateEspresso(m_activeCaptureId);
    else
        m_waterCaptures[m_activeCaptureId].waterCycle = m_machineState->hotWaterCycleId();

    m_firstStepState = m_device->state();
    m_recipeName = recipe.value(QStringLiteral("name")).toString();
    // A long black's first step is hot water: no shot to wait for.
    m_shotSettled = next == Step::Espresso;
    m_idleDelayDone = false;
    DIAG_INFO(RECIPES, "DrinkSequenceController").noquote()
        << QStringLiteral("'%1': %2 will follow this %3")
               .arg(m_recipeName, stepName(next),
                    next == Step::HotWater ? QStringLiteral("espresso") : QStringLiteral("hot water"));
    setStage(Stage::Pending, next);
}

void DrinkSequenceController::onDeviceStateChanged()
{
    if (!m_device)
        return;
    const DE1::State state = m_device->state();
    if (state != m_firstStepState)
        m_skipped = false;
    // Not on Idle: a recipe start from Sleep passes through it on the way.
    if (endsDrinkWait(state) && state != DE1::State::HotWater)
        m_recipeWaterExpected = false;

    switch (m_stage) {
    case Stage::Idle:
        return;
    case Stage::Running:
        if (state != stateFor(m_step))
            setStage(Stage::Idle, Step::None);
        return;
    case Stage::AwaitingStart:
        if (state == stateFor(m_step)) {
            setStage(Stage::Running, m_step);
        } else if (endsDrinkWait(state)) {
            DIAG_INFO(RECIPES, "DrinkSequenceController").noquote()
                << QStringLiteral("'%1': %2 did not start: the machine went to %3")
                       .arg(m_recipeName, stepName(m_step), DE1::stateToString(state));
            setStage(Stage::Idle, Step::None);
        }
        return;
    case Stage::Pending:
        if (state == stateFor(m_step)) {
            // A GHC/manual start can arrive before the automatic request.
            setStage(Stage::Running, m_step);
            return;
        }
        break;
    }

    if (state == m_firstStepState)
        return;
    if (state != DE1::State::Idle) {
        if (endsDrinkWait(state))
            drop(QStringLiteral("the machine went to %1").arg(DE1::stateToString(state)));
        return;
    }
    // Firmware 1359's window opens at this Idle report and lasts FIRMWARE_WINDOW_MS.
    m_sinceIdle.start();
    m_idleDelay.start();
}

void DrinkSequenceController::onShotSettled()
{
    if (m_stage != Stage::Pending || m_shotSettled)
        return;
    m_shotSettled = true;
    fireIfReady();
}

void DrinkSequenceController::fireIfReady()
{
    if (m_stage != Stage::Pending || !m_shotSettled || !m_idleDelayDone)
        return;
    if (!m_device) {
        drop(QStringLiteral("the DE1 is gone"));
        return;
    }
    // Requested once; a refusal is not retried.
    const Step step = m_step;
    setStage(Stage::AwaitingStart, step);
    const qint64 sinceIdleMs = m_sinceIdle.isValid() ? m_sinceIdle.elapsed() : 0;
    if (sinceIdleMs > FIRMWARE_WINDOW_MS) {
        DIAG_WARN(RECIPES, "DrinkSequenceController").noquote()
            << QStringLiteral("'%1': requesting %2 %3 ms after Idle, past the firmware's "
                              "%4 ms window, because the shot settled late")
                   .arg(m_recipeName, stepName(step)).arg(sinceIdleMs).arg(FIRMWARE_WINDOW_MS);
    } else {
        DIAG_INFO(RECIPES, "DrinkSequenceController").noquote()
            << QStringLiteral("'%1': requesting %2").arg(m_recipeName, stepName(step));
    }
    m_device->startUnattended(stateFor(step));
}

void DrinkSequenceController::skip()
{
    if (m_stage != Stage::Pending)
        return;
    DIAG_INFO(RECIPES, "DrinkSequenceController").noquote()
        << QStringLiteral("'%1': %2 skipped").arg(m_recipeName, stepName(m_step));
    // Once the first step is over there is no flow left to re-arm, and a latch
    // set now would never clear before the next drink's first step.
    m_skipped = m_device && m_device->state() == m_firstStepState;
    setStage(Stage::Idle, Step::None);
}

void DrinkSequenceController::drop(const QString& why)
{
    if (m_stage == Stage::Idle)
        return;
    DIAG_INFO(RECIPES, "DrinkSequenceController").noquote()
        << QStringLiteral("'%1': %2 %3, %4")
               .arg(m_recipeName, stepName(m_step),
                    m_stage == Stage::Pending ? QStringLiteral("not requested")
                                              : QStringLiteral("ended"),
                    why);
    setStage(Stage::Idle, Step::None);
}

void DrinkSequenceController::setStage(Stage stage, Step step)
{
    if (stage != Stage::Pending)
        m_idleDelay.stop();
    if (stage != Stage::AwaitingStart)
        m_startTimeout.stop();
    else if (m_stage != Stage::AwaitingStart)
        m_startTimeout.start();
    if (stage == m_stage && step == m_step)
        return;
    const Step wasPending = pendingStep();
    const bool wasActive = isActive();
    if (stage == Stage::Idle && m_activeCaptureId) {
        const auto id = std::exchange(m_activeCaptureId, 0);
        auto it = m_waterCaptures.find(id);
        if (it != m_waterCaptures.end()) {
            if (it->espressoOperation <= 0)
                m_waterCaptures.erase(it);
            else if (it->waterCycle == 0) {
                it->result = ShotWaterResult::notDispensed().toMap();
                publishWaterResult(id);
            }
        }
    }
    m_stage = stage;
    m_step = step;
    if (pendingStep() != wasPending)
        emit pendingStepChanged();
    if (isActive() != wasActive)
        emit activeChanged();
}

void DrinkSequenceController::associateEspresso(quint64 captureId)
{
    auto it = m_waterCaptures.find(captureId);
    if (it == m_waterCaptures.end() || it->espressoOperation > 0 || !m_espressoOperation) return;
    it->espressoOperation = m_espressoOperation();
    if (it->espressoOperation <= 0) return;
    emit waterEspressoAssociated(it->espressoOperation);
    publishWaterResult(captureId);
}

void DrinkSequenceController::publishWaterResult(quint64 captureId)
{
    const auto it = m_waterCaptures.find(captureId);
    if (it == m_waterCaptures.end() || it->espressoOperation <= 0 || it->result.isEmpty()) return;
    const auto capture = *it;
    m_waterCaptures.erase(it);
    emit waterResultReady(capture.espressoOperation, capture.result);
}

void DrinkSequenceController::discardWaterForOperation(qint64 operation)
{
    // Non-espresso completion uses operation 0. A long black's water is still
    // awaiting its espresso identity then, so it must survive that cleanup.
    if (operation <= 0) return;
    for (auto it = m_waterCaptures.begin(); it != m_waterCaptures.end();) {
        if (it->espressoOperation == operation) it = m_waterCaptures.erase(it);
        else ++it;
    }
}
