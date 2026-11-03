#pragma once

#include "core/wateramount.h"
#include <optional>

// Pure ownership model: editing the next setup never replaces a drink's spec.
class RecipeWaterState {
public:
    bool setup(qint64 recipeId, const QJsonObject& water, double targetG, bool targetEdited = false) {
        const auto snapshot = WaterAmount::writableSnapshot(water);
        const bool targetChanged = targetG != m_setup.espressoTargetG;
        const bool amountChanged = snapshot != m_setup.spec.snapshot
            || (WaterAmount::isRatio(snapshot) && targetChanged);
        if (amountChanged) m_setup = WaterAmount::plan(snapshot, targetG);
        else m_setup.espressoTargetG = targetG;
        m_recipeId = recipeId;
        if (targetEdited && targetChanged && m_drink && m_drink->recipeId == recipeId && !m_drink->capture)
            updateDrinkTarget(targetG);
        return amountChanged;
    }

    void beginDrink() {
        if (!m_drink && m_setup.enabled()) m_drink = Drink{m_recipeId, 0, m_setup, std::nullopt, 0};
    }
    void bindEspresso(qint64 operation) {
        beginDrink();
        if (m_drink) m_drink->espressoOperation = operation;
    }
    void espressoTargetEdited(qint64 operation, double targetG) {
        if (m_drink && m_drink->espressoOperation == operation && !m_drink->capture)
            updateDrinkTarget(targetG);
    }
    void waterStarted(quint64 cycle, const WaterAmount::Plan& standalone) {
        beginDrink();
        if (!m_drink) m_drink = Drink{0, 0, standalone, std::nullopt, 0};
        if (!m_drink->capture) {
            m_drink->capture = m_drink->pending;
            m_drink->cycle = cycle;
        }
    }
    void endDrink() { m_drink.reset(); }
    void applicationResult(bool applied) {
        m_applicationError = applied ? QString()
            : QStringLiteral("Could not apply the water vessel. Check its settings and try again.");
    }
    bool drinking() const { return m_drink.has_value(); }
    bool waterCaptured() const { return m_drink && m_drink->capture.has_value(); }
    qint64 drinkOperation() const { return m_drink ? m_drink->espressoOperation : 0; }
    qint64 setupRecipeId() const { return m_recipeId; }
    const WaterAmount::Plan& setupPlan() const { return m_setup; }
    const WaterAmount::Plan& effectivePlan() const {
        return m_drink ? (m_drink->capture ? *m_drink->capture : m_drink->pending) : m_setup;
    }
    QJsonObject historySnapshot() const {
        if ((m_drink ? m_drink->recipeId : m_recipeId) <= 0) return {};
        return effectivePlan().historySnapshot();
    }
    QString startError(bool activationPending) const {
        if (activationPending && !m_drink) return QStringLiteral("Recipe settings are still being applied.");
        const auto& plan = effectivePlan();
        if (!plan.enabled()) return {};
        return !plan.result.error.isEmpty() ? plan.result.error : m_applicationError;
    }

private:
    struct Drink {
        qint64 recipeId = 0;
        qint64 espressoOperation = 0;
        WaterAmount::Plan pending;
        std::optional<WaterAmount::Plan> capture;
        quint64 cycle = 0;
    };
    void updateDrinkTarget(double targetG) {
        if (m_drink->pending.spec.ratio() && m_drink->pending.espressoTargetG != targetG)
            m_drink->pending = WaterAmount::plan(m_drink->pending.spec.snapshot, targetG);
    }
    qint64 m_recipeId = 0;
    WaterAmount::Plan m_setup;
    std::optional<Drink> m_drink;
    QString m_applicationError;
};
