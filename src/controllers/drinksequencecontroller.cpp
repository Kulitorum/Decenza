#include "drinksequencecontroller.h"

#include "ble/de1device.h"
#include "core/diagnosticlogging.h"
#include "core/recipeeditfields.h"
#include "machine/machinestate.h"

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
        endAwaitingStart(QStringLiteral("the machine did not start it within %1 s, so the "
                                        "firmware refused it (a GHC machine without the "
                                        "post-step window needs a tap)")
                             .arg(START_TIMEOUT_MS / 1000));
    });
    if (m_machineState) {
        connect(m_machineState, &MachineState::shotFlowStarted,
                this, &DrinkSequenceController::onFlowStarted);
    }
    if (m_device) {
        connect(m_device, &DE1Device::stateChanged,
                this, &DrinkSequenceController::onDeviceStateChanged);
        connect(m_device, &DE1Device::connectedChanged, this, [this] {
            if (m_device && !m_device->isConnected()) {
                drop(QStringLiteral("the DE1 disconnected"));
                endAwaitingStart(QStringLiteral("the DE1 disconnected"));
                finishDrink();
            }
        });
    }
}

void DrinkSequenceController::onFlowStarted(int phase)
{
    if (!m_device || !m_activeRecipe || m_skipped)
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
    else if (p == MachineState::Phase::HotWater && sequence == RecipeEditFields::DrinkSequence::WaterBefore)
        next = Step::Espresso;
    if (next == Step::None || next == m_pending)
        return;

    m_firstStepState = m_device->state();
    m_recipeName = recipe.value(QStringLiteral("name")).toString();
    // A long black's first step is hot water: no shot to wait for.
    m_shotSettled = next == Step::Espresso;
    m_idleDelayDone = false;
    DIAG_INFO(RECIPES, "DrinkSequenceController").noquote()
        << QStringLiteral("'%1': %2 will follow this %3")
               .arg(m_recipeName, stepName(next),
                    next == Step::HotWater ? QStringLiteral("espresso") : QStringLiteral("hot water"));
    setPending(next);
}

void DrinkSequenceController::onDeviceStateChanged()
{
    if (!m_device)
        return;
    const DE1::State state = m_device->state();
    if (m_running != Step::None && state != stateFor(m_running))
        finishDrink();
    if (m_awaitingStart != Step::None) {
        if (state == stateFor(m_awaitingStart)) {
            m_running = m_awaitingStart;
            m_awaitingStart = Step::None;
            m_startTimeout.stop();
        } else if (endsDrinkWait(state)) {
            endAwaitingStart(QStringLiteral("the machine went to %1")
                                 .arg(DE1::stateToString(state)));
        }
    }
    if (state != m_firstStepState)
        m_skipped = false;
    if (m_pending == Step::None || state == m_firstStepState)
        return;
    if (state != DE1::State::Idle) {
        if (endsDrinkWait(state))
            drop(QStringLiteral("the machine went to %1").arg(DE1::stateToString(state)));
        return;
    }
    // Firmware 1359's window opens at this Idle report and lasts 5 s.
    m_idleDelay.start();
}

void DrinkSequenceController::onShotSettled()
{
    if (m_pending == Step::None || m_shotSettled)
        return;
    m_shotSettled = true;
    fireIfReady();
}

void DrinkSequenceController::fireIfReady()
{
    if (m_pending == Step::None || !m_shotSettled || !m_idleDelayDone || !m_device)
        return;
    // A refusal is not retried (no firmware or GHC gate by design).
    const Step step = m_pending;
    m_awaitingStart = step;
    m_startTimeout.start();
    setPending(Step::None);
    DIAG_INFO(RECIPES, "DrinkSequenceController").noquote()
        << QStringLiteral("'%1': requesting %2").arg(m_recipeName, stepName(step));
    m_device->startUnattended(stateFor(step));
}

void DrinkSequenceController::endAwaitingStart(const QString& refusedWhy)
{
    if (m_awaitingStart == Step::None)
        return;
    DIAG_INFO(RECIPES, "DrinkSequenceController").noquote()
        << QStringLiteral("'%1': %2 did not start: %3")
               .arg(m_recipeName, stepName(m_awaitingStart), refusedWhy);
    m_awaitingStart = Step::None;
    m_startTimeout.stop();
    emit activeChanged();
}

void DrinkSequenceController::finishDrink()
{
    if (m_running == Step::None)
        return;
    m_running = Step::None;
    emit activeChanged();
}

void DrinkSequenceController::skip()
{
    if (m_pending == Step::None)
        return;
    DIAG_INFO(RECIPES, "DrinkSequenceController").noquote()
        << QStringLiteral("'%1': %2 skipped").arg(m_recipeName, stepName(m_pending));
    m_skipped = true;
    setPending(Step::None);
}

void DrinkSequenceController::drop(const QString& why)
{
    if (m_pending == Step::None)
        return;
    DIAG_INFO(RECIPES, "DrinkSequenceController").noquote()
        << QStringLiteral("'%1': %2 not requested, %3").arg(m_recipeName, stepName(m_pending), why);
    setPending(Step::None);
}

void DrinkSequenceController::setPending(Step step)
{
    if (step == Step::None)
        m_idleDelay.stop();
    if (m_pending == step)
        return;
    const bool wasActive = isActive();
    m_pending = step;
    emit pendingStepChanged();
    if (isActive() != wasActive)
        emit activeChanged();
}
