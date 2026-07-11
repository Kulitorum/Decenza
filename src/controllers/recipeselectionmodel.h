#pragma once

#include <QtGlobal>

// Pure, dependency-free state machine behind MainController::selectedRecipeId
// and the deferred recipe-shot start (add-recipes, PR #1470). Extracted from
// MainController so it can be unit-tested directly — MainController itself is
// never linked into the test suite (it pulls in nearly the whole app), so the
// policy lives here and MainController only wires it to Qt signals + the device.
//
// selected() is the synchronous "user has chosen this recipe" marker. It leads
// Settings.dye.activeRecipeId during activation (the async DB read + BLE profile
// upload) and then converges to it: a successful activation confirms it, an
// external deactivation clears it, a failed activation rolls it back. -1 = none.
//
// The deferred start (requestStart / onActivationResult) ensures a start gesture
// that arrives before the recipe's profile is actually applied fires only once
// activation completes — never pulling a shot on the previous, not-yet-replaced
// profile. Selecting a different recipe cancels an armed start.
class RecipeSelectionModel {
public:
    enum class StartDecision {
        None,      // nothing selected — ignore
        StartNow,  // selected recipe already applied — start immediately
        Deferred,  // selected recipe still applying — armed; fires on completion
    };

    // Result of onActivationResult, so the caller can emit / log / start.
    struct Outcome {
        bool selectedChanged = false;  // emit selectedRecipeIdChanged()
        bool reverted = false;         // a failed selection was rolled back (log)
        bool fireStart = false;        // an armed deferred start should now fire
        bool startDropped = false;     // an armed start was cancelled, not fired (log)
    };

    qint64 selected() const { return m_selected; }
    qint64 pendingStart() const { return m_pendingStart; }

    // Startup restore: seed the selection with no side effects.
    void reset(qint64 activeId) {
        m_selected = norm(activeId);
        m_pendingStart = -1;
    }

    // activateRecipe(): a new selection cancels any armed start for a different
    // recipe, then optimistically selects id. Returns true if selected changed.
    bool onActivate(qint64 id) {
        id = norm(id);
        if (m_pendingStart > 0 && m_pendingStart != id)
            m_pendingStart = -1;
        return setSelected(id);
    }

    // activeRecipeIdChanged(): follow the confirmed active recipe (success
    // confirms the lead; external deactivation clears it). Returns true if changed.
    bool onActiveRecipeChanged(qint64 activeId) {
        return setSelected(norm(activeId));
    }

    // A start gesture on the selected recipe: start now if its activation is
    // already confirmed, else arm a deferred start.
    StartDecision requestStart(qint64 activeId) {
        if (m_selected <= 0)
            return StartDecision::None;
        if (norm(activeId) == m_selected)
            return StartDecision::StartNow;
        m_pendingStart = m_selected;
        return StartDecision::Deferred;
    }

    // recipeActivated(id, success): roll back a failed selection and/or resolve
    // an armed deferred start. Rollback runs first so a failed start can't fire.
    Outcome onActivationResult(qint64 id, bool success, qint64 activeId) {
        Outcome o;
        if (!success && m_selected == id) {
            o.reverted = true;
            o.selectedChanged = setSelected(norm(activeId));
        }
        if (m_pendingStart > 0 && m_pendingStart == id) {
            m_pendingStart = -1;  // one-shot: consume regardless of outcome
            if (success && m_selected == id)
                o.fireStart = true;
            else
                o.startDropped = true;
        }
        return o;
    }

private:
    static qint64 norm(qint64 id) { return id > 0 ? id : -1; }
    bool setSelected(qint64 id) {
        if (m_selected == id)
            return false;
        m_selected = id;
        return true;
    }

    qint64 m_selected = -1;
    qint64 m_pendingStart = -1;
};
