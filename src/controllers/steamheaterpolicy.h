#pragma once

#include <QObject>

#include <functional>

class Settings;

// The commanded steam-heater state: whether the heater should be warm, and the
// temperature it is aiming for. `commandedTemperatureC()` is what goes on the
// wire — the DE1 reads a TargetSteamTemp of 0 as "heater off".
struct SteamTarget {
    bool on = false;
    double temperatureC = 0.0;

    double commandedTemperatureC() const { return on ? temperatureC : 0.0; }
};

// THE single derivation of the steam-heater target.
//
// Every path that writes ShotSettings to the machine, and every path that
// reports heater state, resolves through this class. There must not be a second
// derivation anywhere: `ProfileManager::uploadCurrentProfile()` used to hand-roll
// its own copy that knew only two of the five inputs, so every profile upload
// re-sent steam = 0 for a user with `keepSteamHeaterOn` off — silently undoing
// the heater state a recipe activation had just applied, because the upload it
// triggered was deferred behind `m_uploadInFlight` and landed afterwards. The
// user-visible symptom was "picking a milk recipe does not warm the steam
// heater". Centralising is the fix; keeping it centralised is what stops it
// coming back.
//
// The inputs the resolution reads are deliberately not duplicated into members:
// they are read live from Settings, so a caller cannot resolve against a stale
// copy. The one piece of state that does not live in Settings — whether the
// active recipe wants steam — is pushed in by MainController.
class SteamHeaterPolicy : public QObject {
    Q_OBJECT

public:
    explicit SteamHeaterPolicy(Settings* settings, QObject* parent = nullptr);

    SteamTarget resolve() const;

    // Convenience for the call sites that only need the wire value.
    double commandedTemperatureC() const { return resolve().commandedTemperatureC(); }

    // What the ACTIVE RECIPE asks of the heater. Three states, not two: "no
    // recipe" and "a recipe that does not steam" pull in opposite directions,
    // and collapsing them turned Let the recipe decide into a heater-off switch
    // for anyone who had simply not activated anything.
    enum class RecipeSteamIntent {
        NoRecipe,    // nothing active — the recipe layer has no opinion
        WantsSteam,  // milk drink, or a pitcher that is not "Heater off"
        NoSteam,     // an active recipe that steams nothing — an espresso
    };

    // PULLED, not pushed: the active recipe is mutated at six sites in
    // MainController, and a pushed copy would only have to be missed at one of
    // them to resolve against a stale answer. MainController installs this once.
    // Unset means NoRecipe, which is the correct answer for any caller that has
    // no recipe layer at all.
    void setRecipeIntentProvider(std::function<RecipeSteamIntent()> provider);
    RecipeSteamIntent recipeIntent() const;
    bool activeRecipeWantsSteam() const { return recipeIntent() == RecipeSteamIntent::WantsSteam; }

    // EVENT permission — an explicit steam action is under way: the user tapped
    // steam, pressed the GHC steam lever, started a shot whose recipe steams.
    // It is transient, revoked on the return to Idle and never restored by a
    // settings re-send, which is what separates it from the two STATE sources
    // (`keepWarmWhenIdle`, `letRecipeDecide`) that a re-send re-derives.
    //
    // It outranks both vetoes on purpose. A GHC press puts the DE1 into Steam
    // in firmware and the app cannot refuse it, so resolving "off" there would
    // make every readout a lie about a boiler that is actively heating.
    void setEventPermission(bool granted);
    bool eventPermission() const { return m_eventPermission; }

signals:
    // The resolved target may have changed. Settings-driven inputs announce
    // themselves; this covers the event permission, which lives here.
    void resolvedChanged();

private:
    Settings* m_settings = nullptr;
    std::function<RecipeSteamIntent()> m_recipeIntent;
    bool m_eventPermission = false;
};
