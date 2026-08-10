#include "steamheaterpolicy.h"

#include "../core/settings.h"
#include "../core/settings_brew.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

SteamHeaterPolicy::SteamHeaterPolicy(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
}

SteamHeaterPolicy::RecipeSteamIntent SteamHeaterPolicy::intentForRecipe(const QVariantMap& recipe)
{
    if (recipe.isEmpty())
        return RecipeSteamIntent::NoRecipe;

    const QJsonObject steam = QJsonDocument::fromJson(
        recipe.value(QStringLiteral("steamJson")).toString().toUtf8()).object();

    // An explicit "Heater off" DOMINATES hasMilk. A recipe can carry both — the
    // migration writes the marker onto recipes that named a removed Off preset
    // and leaves hasMilk untouched — and reading hasMilk first granted shot-start
    // permission to the very recipes whose purpose is a cold boiler.
    if (steam.value(QStringLiteral("heaterOff")).toBool())
        return RecipeSteamIntent::NoSteam;

    // Profile-less (hot-water tea) recipes never hold the heater, even if an
    // MCP or web author attached a milk block to one.
    if (recipe.value(QStringLiteral("profileTitle")).toString().trimmed().isEmpty())
        return RecipeSteamIntent::NoSteam;

    return steam.value(QStringLiteral("hasMilk")).toBool()
        ? RecipeSteamIntent::WantsSteam
        : RecipeSteamIntent::NoSteam;
}

void SteamHeaterPolicy::setRecipeIntentProvider(std::function<RecipeSteamIntent()> provider)
{
    m_recipeIntent = std::move(provider);
}

SteamHeaterPolicy::RecipeSteamIntent SteamHeaterPolicy::recipeIntent() const
{
    return m_recipeIntent ? m_recipeIntent() : RecipeSteamIntent::NoRecipe;
}

void SteamHeaterPolicy::setEventPermission(bool granted)
{
    if (m_eventPermission == granted)
        return;
    m_eventPermission = granted;
    emit resolvedChanged();
}

SteamTarget SteamHeaterPolicy::resolve() const
{
    if (!m_settings)
        return SteamTarget::off();

    auto* brew = m_settings->brew();
    if (!brew)
        return SteamTarget::off();

    const QVariantMap pitcher = brew->getSteamPitcherPreset(brew->selectedSteamPitcher());
    const bool heaterOffPitcher = SettingsBrew::isHeaterOffPitcher(pitcher);

    // Event permission short-circuits every veto — see setEventPermission().
    if (!eventPermission()) {
        // VETO — the effective pitcher is "Heater off", or the transient session
        // flag is set. Either forces the heater cold whatever the settings say.
        if (heaterOffPitcher)
            return SteamTarget::off();
        if (brew->steamDisabled())
            return SteamTarget::off();
        // VETO — Let the recipe decide, with an active recipe that steams
        // nothing. This is the setting's whole point: park an espresso and the
        // boiler goes cold even for a Keep-warm user. It applies only when a
        // recipe is actually active; with none, the recipe layer has no opinion
        // and Keep warm when idle is unopposed.
        if (brew->letRecipeDecide() && recipeIntent() == RecipeSteamIntent::NoSteam)
            return SteamTarget::off();

        // STATE permission — one source. Not "the user selected a pitcher": the
        // pitcher row says what the user would steam WITH, it is not a heater
        // switch, and selecting a real pitcher only removes the veto above.
        //
        // Note what is NOT here: a milk recipe being active. Users park a recipe
        // as the machine's resting state between drinks, so "a latte is
        // selected" is a stale signal for "milk is coming". Let the recipe
        // decide grants its permission as an EVENT at shot start instead.
        if (!brew->keepWarmWhenIdle())
            return SteamTarget::off();
    }

    // The effective pitcher's own temperature when it carries one, else the
    // global. A pitcher preset is the steam spec, so a recipe or a pill that
    // selects one must heat to ITS temperature, not the last global value.
    //
    // The built-in "Heater off" entry deliberately carries no values, so steaming
    // with it selected falls through to the live settings — which hold the last
    // real pitcher's duration, flow and temperature. That is the pre-existing
    // behaviour, kept on purpose: a GHC press cannot be refused, and inventing
    // parameters for it would be worse than reusing the ones the user last used.
    const double pitcherTemp = heaterOffPitcher
        ? 0.0
        : pitcher.value(QStringLiteral("temperature")).toDouble();
    return SteamTarget::heating(pitcherTemp > 0.0 ? pitcherTemp : brew->steamTemperature());
}
