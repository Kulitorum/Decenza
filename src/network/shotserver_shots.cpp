#include "shotserver.h"
#include "webdebuglogger.h"
#include "webtemplates.h"
#include "webtemplates/menu_js.h"
#include "webtemplates/grind_datalist_js.h"
#include "../history/shothistorystorage.h"
#include "../history/shotcomparison.h"
#include "../history/shotcomparisontext.h"
#include "../history/shotprojection.h"
#include "../history/equipmentstorage.h"
#include "../profile/profiledialintext.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../screensaver/screensavervideomanager.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../core/settingsserializer.h"
#include "../ai/aimanager.h"
#include "version.h"

#include <QNetworkInterface>
#include <QUdpSocket>
#include <QSet>
#include <QFile>
#include <QBuffer>
#include <algorithm>
#include <cmath>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QLocale>
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#ifndef Q_OS_IOS
#include <QProcess>
#endif
#include <QCoreApplication>
#include <QRegularExpression>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

// Drink-type glyph for the web, the counterpart of the app's DrinkType.icon().
// It cannot share that function's qrc SVGs — this is a browser — and an emoji is
// this page's established idiom (the promote button already uses one). The
// macOS colour-glyph render-thread hazard is a Qt text-rendering problem and
// does not apply to HTML. Kept as ONE function so the mapping is not respelled
// per call site; the type strings are the same set RecipeStorage stores.
static QString drinkTypeEmoji(const QString& drinkType)
{
    // No `filter` branch on purpose: it falls through to the coffee cup below.
    // Unicode has no dripper/pour-over glyph, and the app's distinct
    // filter.svg has no emoji counterpart. Collapsing filter into ESPRESSO
    // (both black coffee) loses less than the alternative this replaced, which
    // gave filter U+1F375 — the exact codepoint used for tea four lines down,
    // so filter and tea rendered identically while a comment called it a
    // "filter cone". If the distinction ever needs to be visible here, ship an
    // inline SVG rather than hunting for a closer emoji.
    if (drinkType == QLatin1String("americano")
        || drinkType == QLatin1String("long_black")) return QStringLiteral("&#128167;");   // 💧 U+1F4A7 water
    if (drinkType == QLatin1String("latte")
        || drinkType == QLatin1String("latte_hotwater")) return QStringLiteral("&#129371;"); // 🥛 U+1F95B milk
    if (drinkType == QLatin1String("tea")
        || drinkType == QLatin1String("tea_hotwater")) return QStringLiteral("&#127861;");  // 🍵 U+1F375 teacup
    return QStringLiteral("&#9749;");  // ☕ espresso, and the fallback for a
                                       // legacy recipe with no stored type
}

QString ShotServer::generateShotListPage(const QVariantList& shots) const
{
    QString rows;
    for (const QVariant& v : std::as_const(shots)) {
        // queryShotList() emits maps with ShotProjection-aligned keys; routing
        // them through fromVariantMap() gives the rest of the loop compile-time
        // safety on every field name read below.
        const ShotProjection shot = ShotProjection::fromVariantMap(v.toMap());

        int rating = qRound(static_cast<double>(shot.enjoyment0to100));

        // Rating chip (matches in-app ShotHistoryPage: "N%" when rated, hidden
        // when unrated). data-rating= and the rating:N+ search syntax keep using
        // the numeric value above.
        QString ratingChip;
        if (rating > 0) {
            const QString r = QString::number(rating);
            ratingChip = QStringLiteral(
                "<span class=\"shot-rating clickable\" "
                "onclick=\"event.preventDefault(); event.stopPropagation(); setSearch('rating:") + r + QStringLiteral("+')\">")
                + r + QStringLiteral("%</span>");
        }

        double ratio = 0;
        if (shot.doseWeightG > 0) {
            ratio = shot.finalWeightG / shot.doseWeightG;
        }

        const QString& profileName = shot.profileName;
        const QString& beanBrand = shot.beanBrand;
        const QString& beanType = shot.beanType;
        const QString& dateTime = shot.dateTime;
        const double doseWeight = shot.doseWeightG;
        const double finalWeight = shot.finalWeightG;
        const double duration = shot.durationSec;
        const QString& grinderSetting = shot.grinderSetting;
        const double tempOverride = shot.temperatureOverrideC;  // Always has value
        const double targetWeight = shot.targetWeightG;  // Always has value

        // Escape for JavaScript string (single quotes) and HTML attribute.
        //
        // Does NOT double `%` → `%%`: that pattern looks like an arg-placeholder
        // escape but isn't one — Qt's QString::arg() never reduces `%%` back to
        // `%` (verified in qtbase qstring.cpp findArgEscapes/replaceArgEscapes),
        // so doubling caused user-visible `%%` whenever a profile/bean name
        // contained a `%` followed by a non-digit (the common case, e.g.
        // "50% off" rendered as "50%% off"). It also failed to escape the
        // shadow case it was meant to prevent — "%12" would still parse as a
        // %12 placeholder after doubling because the parser rescans after
        // each non-match. The residual shadow risk (user typing the literal
        // text of an unfilled placeholder number into a name) is real but
        // rare; the rendering bug was always-on and visible.
        auto escapeForJs = [](const QString& s) -> QString {
            QString escaped = s;
            // '&' FIRST, before anything that emits a character reference below.
            // The result lands inside a double-quoted HTML attribute
            // (onclick="… setSearch('VALUE') …"), so the parser decodes character
            // references BEFORE the JS is compiled: a name containing the literal
            // text "&#39;" would decode to an apostrophe and close the string.
            // Pre-existing hole, but recipe names reach here from device transfer
            // and MCP recipe_create, not only from this device's keyboard.
            escaped.replace("&", "&amp;");
            escaped.replace("\\", "\\\\");
            // Newlines and CR would be raw inside a single-quoted JS string in an
            // onclick attribute — a SyntaxError, so the click silently does
            // nothing. Names arrive from device transfer and MCP recipe_create,
            // not only from this device's keyboard.
            escaped.replace("\n", "\\n");
            escaped.replace("\r", "\\r");
            escaped.replace("'", "\\'");
            escaped.replace("\"", "&quot;");
            escaped.replace("<", "&lt;");
            escaped.replace(">", "&gt;");
            return escaped;
        };

        QString profileJs = escapeForJs(profileName);
        QString brandJs = escapeForJs(beanBrand);
        QString coffeeJs = escapeForJs(beanType);
        QString profileHtml = profileName.toHtmlEscaped();
        QString brandHtml = beanBrand.toHtmlEscaped();
        QString coffeeHtml = beanType.toHtmlEscaped();

        // Build profile header: "Profile (Temp°C)"
        QString profileDisplay = profileHtml;
        if (tempOverride > 0) {
            profileDisplay += QString(" <span class=\"shot-temp\">(%1\u00B0C)</span>")
                .arg(tempOverride, 0, 'f', 0);
        }

        // Build yield display: "Actual (Target) out" or just "Actual out"
        QString yieldDisplay;
        if (targetWeight > 0 && qAbs(targetWeight - finalWeight) > 0.5) {
            yieldDisplay = QString("<span class=\"metric-value\">%1g</span><span class=\"metric-target\">(%2g)</span>")
                .arg(finalWeight, 0, 'f', 1)
                .arg(targetWeight, 0, 'f', 0);
        } else {
            yieldDisplay = QString("<span class=\"metric-value\">%1g</span>")
                .arg(finalWeight, 0, 'f', 1);
        }

        // Build bean display: "Brand Type (Grind)"
        QString beanDisplay;
        if (!beanBrand.isEmpty() || !beanType.isEmpty()) {
            beanDisplay = QString("<span class=\"clickable\" onclick=\"event.preventDefault(); event.stopPropagation(); setSearch('%1')\">%2</span>"
                                  "<span class=\"clickable\" onclick=\"event.preventDefault(); event.stopPropagation(); setSearch('%3')\">%4</span>")
                .arg(brandJs, brandHtml, coffeeJs, coffeeHtml);
            if (!grinderSetting.isEmpty()) {
                beanDisplay += QString(" <span class=\"shot-grind\">(%1)</span>")
                    .arg(grinderSetting.toHtmlEscaped());
            }
        }

        // Recipe identity (history-recipe-identity), mirroring the in-app row:
        // when the shot came from a recipe, the recipe takes the header's
        // identity slot and the profile demotes to the beans line. Everything
        // below goes in via __MARKER__ + replace() rather than the .arg() chain
        // — a recipe name is user text and may contain '%', which .arg() does
        // not make safe (see the escapeForJs comment above).
        // Name must resolve too — an id whose recipes row is gone would
        // otherwise render an empty identity slot with the profile already
        // demoted out of it. Same gate as the in-app row.
        const bool hasRecipe = shot.recipeId > 0 && !shot.recipeName.isEmpty();
        // toHtmlEscaped() leaves underscores alone, so a name containing a literal
        // "__IDENTITY__" / "__SECONDARY__" / "__RATING_CHIP__" would survive into
        // the template and be substituted by a LATER replace() below. Neutralize
        // the token itself rather than trying to order the replacements — user
        // text can contain any marker, so no ordering is safe.
        const QString recipeNameHtml =
            shot.recipeName.toHtmlEscaped().replace(QStringLiteral("__"),
                                                    QStringLiteral("&#95;&#95;"));

        QString identityHtml;
        QString secondaryHtml = beanDisplay;
        QString recipeButtonHtml;
        QString recipeAttr;

        if (hasRecipe) {
            // Clicking the recipe scopes the search to that name — the web
            // analogue of the app's tap-through. Quoted so a multi-word name
            // stays one term.
            // Same `__` neutralization as the display name: this string is
            // embedded in identityHtml, which is substituted BEFORE __SECONDARY__
            // and __RECIPE_BTN__, so a recipe named "Dad __SECONDARY__" would
            // otherwise get the beans line spliced into its onclick handler.
            // escapeForJs does not touch underscores.
            const QString recipeSearchJs = escapeForJs(
                QStringLiteral("recipe:\"") + shot.recipeName + QStringLiteral("\""))
                .replace(QStringLiteral("__"), QStringLiteral("&#95;&#95;"));
            // The archived state gets BOTH the dimming class and a title, so it
            // is not carried by colour alone.
            const QString archivedClass = shot.recipeArchived ? QStringLiteral(" archived") : QString();
            const QString archivedTitle = shot.recipeArchived
                                              ? QStringLiteral(" title=\"Archived recipe\"") : QString();
            identityHtml =
                QStringLiteral("<span class=\"shot-drink-icon\">") + drinkTypeEmoji(shot.recipeDrinkType)
                + QStringLiteral("</span><span class=\"shot-profile clickable") + archivedClass
                + QStringLiteral("\"") + archivedTitle
                + QStringLiteral(" onclick=\"event.preventDefault(); event.stopPropagation(); setSearch('")
                + recipeSearchJs + QStringLiteral("')\">") + recipeNameHtml + QStringLiteral("</span>");

            // Profile leads the secondary line, exactly as on the app row.
            const QString profileCell =
                QStringLiteral("<span class=\"shot-secondary-profile\">") + profileDisplay
                + QStringLiteral("</span>");
            secondaryHtml = beanDisplay.isEmpty()
                                ? profileCell
                                : profileCell + QStringLiteral(" &middot; ") + beanDisplay;

            recipeAttr = QStringLiteral(" data-recipe=\"") + recipeNameHtml + QStringLiteral("\"");
        } else {
            identityHtml =
                QStringLiteral("<span class=\"shot-profile clickable\" onclick=\"event.preventDefault(); "
                               "event.stopPropagation(); setSearch('")
                + profileJs + QStringLiteral("')\">") + profileDisplay + QStringLiteral("</span>");

            // Only a shot with no recipe offers to become one.
            recipeButtonHtml = QStringLiteral(
                "<button class=\"shot-recipe-btn\" onclick=\"event.preventDefault(); "
                "event.stopPropagation(); promoteToRecipe(")
                + QString::number(shot.id)
                + QStringLiteral(")\" title=\"Create recipe from this shot\">&#128209; Recipe</button>");
        }

        const double drinkTds = shot.drinkTdsPct;
        const double drinkEy = shot.drinkEyPct;

        QString row = QString(R"HTML(
            <div class="shot-card" onclick="toggleSelect(%1, this)" data-id="%1"
                 data-profile="%2" data-brand="%3" data-coffee="%4" data-rating="%5"
                 data-ratio="%6" data-duration="%7" data-date="%14" data-dose="%9" data-yield="%10"
                 data-tds="%12" data-ey="%13"__RECIPE_ATTR__>
                <a href="/shot/%1" onclick="event.stopPropagation()" style="text-decoration:none;color:inherit;display:block;">
                    <div class="shot-header">
                        __IDENTITY__
                        <div class="shot-header-right">
                            <span class="shot-date">%8</span>
                            <input type="checkbox" class="shot-checkbox" data-id="%1" onclick="event.stopPropagation(); toggleSelect(%1, this.closest('.shot-card'))">
                        </div>
                    </div>
                    <div class="shot-metrics">
                        <div class="dose-group">
                            <div class="shot-metric">
                                <span class="metric-value">%9g</span>
                                <span class="metric-label">in</span>
                            </div>
                            <div class="shot-arrow">&#8594;</div>
                            <div class="shot-metric">
                                %11
                                <span class="metric-label">out</span>
                            </div>
                        </div>
                        <div class="shot-metric">
                            <span class="metric-value">1:%6</span>
                            <span class="metric-label">ratio</span>
                        </div>
                        <div class="shot-metric">
                            <span class="metric-value">%7s</span>
                            <span class="metric-label">time</span>
                        </div>
                    </div>
                    <div class="shot-footer">
                        <span class="shot-beans">__SECONDARY__</span>
                        __RECIPE_BTN__
                        __RATING_CHIP__
                    </div>
                </a>
            </div>
        )HTML")
        .arg(shot.id)                       // %1
        .arg(profileHtml)                   // %2 (data attr, undecorated)
        .arg(brandHtml)                     // %3
        .arg(coffeeHtml)                    // %4
        .arg(rating)                        // %5
        .arg(ratio, 0, 'f', 1)              // %6
        .arg(duration, 0, 'f', 1)           // %7
        .arg(dateTime)                      // %8
        .arg(doseWeight, 0, 'f', 1)         // %9
        .arg(finalWeight, 0, 'f', 1)        // %10
        .arg(yieldDisplay)                  // %11 (yield with target)
        .arg(drinkTds, 0, 'f', 2)           // %12
        .arg(drinkEy, 0, 'f', 2)            // %13
        .arg(shot.timestamp);               // %14 (epoch for sorting)
        // profileJs / profileDisplay / beanDisplay left the .arg() chain because
        // the header and footer became CONDITIONAL (recipe row vs not), which a
        // fixed placeholder cannot express — they are injected as __IDENTITY__ /
        // __SECONDARY__ markers instead. Their old %11/%12/%14 slots are gone and
        // the rest were renumbered contiguously: arg() fills the LOWEST remaining
        // placeholder, so leaving a dead .arg() in the chain would silently shift
        // every later value into the wrong slot rather than failing.
        //
        // It closes the '%'-shadowing hazard for those three, but NOT for the
        // chain as a whole: profileHtml, brandHtml and coffeeHtml are equally
        // user text and are still at %2/%3/%4 with only toHtmlEscaped() applied,
        // so a bean brand containing "%5" can still shadow a later placeholder.

        // Inject ratingChip via replace() AFTER the .arg() chain so the
        // literal `%</span>` (and any future user-derived content) cannot
        // shadow numbered placeholders. Doubling `%` to `%%` is the file's
        // older convention but doesn't actually work — Qt's QString::arg()
        // never reduces `%%` back to `%` (see qtbase qstring.cpp
        // findArgEscapes/replaceArgEscapes), so doubling either leaks `%%`
        // to the rendered output or still shadows ("%5" → "%%5" still
        // parses as a %5 placeholder via continue + re-scan).
        row.replace(QStringLiteral("__RATING_CHIP__"), ratingChip);
        row.replace(QStringLiteral("__RECIPE_ATTR__"), recipeAttr);
        row.replace(QStringLiteral("__IDENTITY__"), identityHtml);
        row.replace(QStringLiteral("__SECONDARY__"), secondaryHtml);
        row.replace(QStringLiteral("__RECIPE_BTN__"), recipeButtonHtml);
        rows += row;
    }

    // Build HTML in chunks to avoid MSVC string literal size limit
    QString html;

    // Part 1: DOCTYPE and head start
    html += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Shot History - Decenza</title>
    <style>
)HTML";
    html += WEB_CSS_VARIABLES;
    html += WEB_CSS_HEADER;
    html += WEB_CSS_MENU;

    // Part 2: Layout CSS
    html += R"HTML(
        .shot-count { color: var(--text-secondary); font-size: 0.875rem; }
        .shot-grid {
            display: grid;
            gap: 1rem;
            grid-template-columns: repeat(auto-fill, minmax(340px, 1fr));
        }
)HTML";

    // Part 3: Shot card CSS
    html += R"HTML(
        .shot-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 0.5rem 0.75rem;
            text-decoration: none;
            color: inherit;
            transition: background 0.2s ease, border-color 0.2s ease;
            display: block;
            content-visibility: auto;
            contain-intrinsic-size: auto 110px;
            position: relative;
        }
        .shot-card:hover { background: var(--surface-hover); border-color: var(--accent); }
        .shot-card.selected { border-color: var(--accent); }
        .shot-header { display: flex; justify-content: space-between; align-items: center; }
        .shot-header-right { display: flex; align-items: center; gap: 0.5rem; }
        .shot-profile { font-weight: 600; font-size: 1rem; color: var(--text); }
        .shot-date { font-size: 0.75rem; color: var(--text-secondary); white-space: nowrap; }
        .shot-metrics { display: flex; align-items: center; justify-content: space-between; }
        .dose-group {
            display: flex;
            align-items: center;
            gap: 0.3rem;
            padding: 0 0.3rem;
            border: 1px solid var(--border);
            border-radius: 4px;
        }
        .shot-metric { display: flex; flex-direction: column; align-items: center; }
        .shot-metric .metric-value { font-size: 1.125rem; font-weight: 600; color: var(--accent); }
        .shot-metric .metric-label { font-size: 0.625rem; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.05em; }
        .shot-arrow { color: var(--text-secondary); font-size: 1rem; }
        .shot-footer { display: flex; justify-content: space-between; align-items: center; }
        .shot-beans { font-size: 0.8125rem; color: var(--text-secondary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 60%; }
        .shot-recipe-btn { background: var(--surface-hover); color: var(--text-secondary); border: 1px solid var(--border);
                           border-radius: 6px; padding: 0.15rem 0.5rem; font-size: 0.75rem; cursor: pointer; }
        .shot-recipe-btn:hover { color: var(--text); background: var(--border); }
        .shot-rating { color: var(--accent); font-size: 0.875rem; }
        .shot-temp { color: var(--text-secondary); font-weight: normal; }
        .shot-grind { color: var(--text-secondary); font-weight: normal; }
        /* Recipe identity (history-recipe-identity). An archived recipe dims,
           matching the app row; the title attribute carries the state as text so
           the dimming is not the only thing saying it. */
        .shot-drink-icon { margin-right: 0.35rem; }
        .shot-profile.archived { color: var(--text-secondary); font-weight: 500; }
        .shot-secondary-profile { color: var(--text-secondary); }
        .metric-target { font-size: 0.75rem; color: var(--text-secondary); margin-left: 2px; }
        .empty-state { text-align: center; padding: 4rem 2rem; color: var(--text-secondary); }
        .empty-state h2 { margin-bottom: 0.5rem; color: var(--text); }
)HTML";

    // Part 4: Search and compare bar CSS
    html += R"HTML(
        .search-bar { display: flex; gap: 1rem; margin-bottom: 1.5rem; flex-wrap: wrap; align-items: center; }
        .search-help { font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 0.5rem; }
        .search-input {
            flex: 1;
            min-width: 200px;
            padding: 0.75rem 1rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            color: var(--text);
            font-size: 1rem;
        }
        .search-input:focus { outline: none; border-color: var(--accent); }
        .search-input::placeholder { color: var(--text-secondary); }
        .compare-bar {
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1rem 1.5rem;
            display: none;
            justify-content: center;
            align-items: center;
            gap: 1rem;
            z-index: 100;
        }
        .compare-bar.visible { display: flex; }
        .compare-btn {
            padding: 0.75rem 2rem;
            background: var(--accent);
            color: var(--bg);
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
        }
        .compare-btn:hover { opacity: 0.9; }
        .compare-btn:disabled { opacity: 0.4; cursor: default; }
        .delete-btn {
            padding: 0.75rem 1.5rem;
            background: #c0392b;
            color: #fff;
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
        }
        .delete-btn:hover { opacity: 0.9; }
        .clear-btn {
            padding: 0.75rem 1.5rem;
            background: transparent;
            color: var(--text-secondary);
            border: 1px solid var(--border);
            border-radius: 8px;
            cursor: pointer;
        }
)HTML";

    // Part 5: Checkbox and menu CSS
    html += R"HTML(
        .shot-checkbox {
            width: 24px;
            height: 24px;
            min-width: 24px;
            appearance: none;
            -webkit-appearance: none;
            background: var(--bg);
            border: 2px solid var(--border);
            border-radius: 4px;
            cursor: pointer;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        .shot-checkbox:checked { background: var(--accent); border-color: var(--accent); }
        .shot-checkbox:checked::after { content: "✓"; color: var(--bg); font-size: 18px; font-weight: bold; line-height: 1; }
        .clickable { cursor: pointer; transition: color 0.2s; }
        .clickable:hover { color: var(--accent) !important; text-decoration: underline; }
)HTML";

    // Part 6: Sort dropdown CSS
    html += R"HTML(
        .sort-dir-btn { min-width: 2.2rem; padding-left: 0.5rem; padding-right: 0.5rem; text-align: center; }
        .sort-dropdown {
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.25rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            box-shadow: 0 4px 16px rgba(0,0,0,0.4);
            min-width: 10rem;
            z-index: 100;
            display: none;
        }
        .sort-dropdown.open { display: block; }
        .sort-option {
            padding: 0.5rem 0.75rem;
            cursor: pointer;
            font-size: 0.85rem;
            color: var(--text);
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .sort-option:first-child { border-radius: 8px 8px 0 0; }
        .sort-option:last-child { border-radius: 0 0 8px 8px; }
        .sort-option:hover { background: var(--surface-hover); }
        .sort-option .sort-check { display: none; color: var(--accent); margin-left: 0.5rem; }
        .sort-option.active .sort-check { display: inline; }
        .sort-anchor { position: relative; }
        .visible-count { font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 0.5rem; }
)HTML";

    // Part 7: Search bar and panels CSS
    html += R"HTML(
        .search-row {
            display: flex;
            gap: 0.5rem;
            margin-bottom: 1rem;
            align-items: center;
        }
        .search-row .search-input {
            flex: 1;
            min-width: 0;
            padding: 0.6rem 0.75rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-size: 0.9rem;
        }
        .search-row .search-input:focus { outline: none; border-color: var(--accent); }
        .search-row .search-input::placeholder { color: var(--text-secondary); }
        .search-action-btn {
            padding: 0.6rem 0.75rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-size: 0.8rem;
            cursor: pointer;
            white-space: nowrap;
            transition: all 0.2s;
        }
        .search-action-btn:hover { border-color: var(--accent); color: var(--accent); }
        .search-action-btn:disabled { opacity: 0.4; cursor: default; }
        .search-action-btn:disabled:hover { border-color: var(--border); color: var(--text); }
        .search-panels-anchor { position: relative; z-index: 10; }
        .saved-searches-panel, .search-help-panel {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 0.75rem 1rem;
            position: absolute;
            left: 0;
            right: 0;
            top: 0;
            box-shadow: 0 4px 16px rgba(0,0,0,0.4);
            visibility: hidden;
            opacity: 0;
            transition: opacity 0.15s;
        }
        .saved-searches-panel.open, .search-help-panel.open { visibility: visible; opacity: 1; }
        .saved-search-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
        }
        .saved-search-item:last-child { border-bottom: none; }
        .saved-search-text {
            cursor: pointer;
            color: var(--text);
            flex: 1;
            font-size: 0.875rem;
        }
        .saved-search-text:hover { color: var(--accent); }
        .saved-search-delete {
            cursor: pointer;
            color: var(--text-secondary);
            font-size: 1.1rem;
            padding: 0 0.3rem;
            line-height: 1;
        }
        .saved-search-delete:hover { color: #c0392b; }
        .help-table {
            width: 100%;
            border-collapse: collapse;
            font-size: 0.8rem;
        }
        .help-table th {
            text-align: left;
            padding: 0.4rem 0.6rem;
            border-bottom: 1px solid var(--border);
            color: var(--text-secondary);
            font-weight: 600;
        }
        .help-table td {
            padding: 0.4rem 0.6rem;
            border-bottom: 1px solid var(--border);
            color: var(--text);
        }
        .help-keyword {
            color: var(--accent);
            font-weight: 600;
            cursor: pointer;
            padding: 0.15rem 0.3rem;
            border-radius: 3px;
            transition: background 0.2s;
        }
        .help-keyword:hover { background: var(--surface-hover); }
        .help-syntax {
            font-family: monospace;
            background: var(--bg);
            padding: 0.15rem 0.4rem;
            border-radius: 3px;
            font-size: 0.8rem;
        }
        @media (max-width: 600px) {
            .shot-grid { grid-template-columns: minmax(0, 1fr); }
            .container { padding: 1rem; padding-bottom: 5rem; }
            .search-row { flex-wrap: wrap; }
        }
    </style>
</head>
)HTML";

    // Part 8: Body header with menu
    html += QString(R"HTML(<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="logo">&#9749; Decenza</a>
            <div class="header-right">
                <span class="shot-count">%1 shots</span>)HTML").arg(m_storage->totalShots());

    html += generateMenuHtml(true);

    html += R"HTML(
            </div>
        </div>
    </header>
)HTML";

    // Part 9: Main content - search bar
    html += R"HTML(
    <main class="container">
        <div class="search-row">
            <input type="text" class="search-input" id="searchInput" placeholder="Search... (e.g. rating:70+ dose:16-18 ethiopia)" oninput="onSearchChange()">
            <button class="search-action-btn" id="keywordsBtn" onclick="toggleKeywords()">Keywords</button>
            <button class="search-action-btn" id="saveBtn" onclick="saveSearch()" disabled>Save</button>
            <button class="search-action-btn" id="savedBtn" onclick="toggleSavedSearches()" style="display:none;">&#9776; Saved</button>
            <span class="sort-anchor">
                <button class="search-action-btn sort-field-btn" id="sortFieldBtn" onclick="toggleSortMenu()">Date &#9662;</button>
                <div class="sort-dropdown" id="sortDropdown">
                    <div class="sort-option active" data-sort="date" data-default-dir="desc" onclick="selectSort('date')">Date <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="profile" data-default-dir="asc" onclick="selectSort('profile')">Profile <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="brand" data-default-dir="asc" onclick="selectSort('brand')">Roaster <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="coffee" data-default-dir="asc" onclick="selectSort('coffee')">Coffee <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="rating" data-default-dir="desc" onclick="selectSort('rating')">Rating <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="ratio" data-default-dir="desc" onclick="selectSort('ratio')">Ratio <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="duration" data-default-dir="asc" onclick="selectSort('duration')">Duration <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="dose" data-default-dir="desc" onclick="selectSort('dose')">Dose <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="yield" data-default-dir="desc" onclick="selectSort('yield')">Yield <span class="sort-check">&#10003;</span></div>
                </div>
                <button class="search-action-btn sort-dir-btn" id="sortDirBtn" onclick="toggleSortDir()">&#9660;</button>
            </span>
        </div>
        <div class="search-panels-anchor">
        <div class="saved-searches-panel" id="savedPanel">
            <div id="savedList"></div>
        </div>
        <div class="search-help-panel" id="helpPanel">
            <p style="margin-bottom:0.6rem;font-size:0.85rem;color:var(--text-secondary);">Use keywords to filter by numeric fields. Click a keyword to add it to your search.</p>
            <table class="help-table">
                <tr><th>Keyword</th><th>Filters</th><th>Example</th></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('rating:')">rating:</span></td><td>Enjoyment (0-100)</td><td><span class="help-syntax">rating:70+</span></td></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('dose:')">dose:</span></td><td>Dose weight (g)</td><td><span class="help-syntax">dose:16-18</span></td></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('yield:')">yield:</span></td><td>Yield weight (g)</td><td><span class="help-syntax">yield:30-40</span></td></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('time:')">time:</span></td><td>Duration (seconds)</td><td><span class="help-syntax">time:25-35</span></td></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('tds:')">tds:</span></td><td>TDS</td><td><span class="help-syntax">tds:1.3-1.5</span></td></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('ey:')">ey:</span></td><td>Extraction yield (%)</td><td><span class="help-syntax">ey:18-22</span></td></tr>
            </table>
            <p style="margin-top:0.5rem;font-size:0.75rem;color:var(--text-secondary);">Syntax: <span class="help-syntax">N</span> (exact), <span class="help-syntax">N-M</span> (range), <span class="help-syntax">N+</span> (minimum)<br>Combine keywords with text: <span class="help-syntax">ethiopia dose:18 rating:70+</span></p>
        </div>
        </div>
)HTML";

    // Part 10: Grid
    html += QString(R"HTML(
        <div class="visible-count" id="visibleCount">Showing %1 shots</div>
        <div class="shot-grid" id="shotGrid">
            %2
        </div>
    </main>
    <div class="compare-bar" id="compareBar">
        <span id="selectedCount">0 selected</span>
        <button class="compare-btn" id="compareBtn" onclick="compareSelected()" disabled>Compare Shots</button>
        <button class="delete-btn" onclick="deleteSelected()">Delete</button>
        <button class="clear-btn" onclick="clearSelection()">Clear</button>
    </div>
)HTML").arg(m_storage->totalShots())
      .arg(rows.isEmpty() ? "<div class='empty-state'><h2>No shots yet</h2><p>Pull some espresso to see your history here</p></div>" : rows);

    // Part 11: Script - selection functions
    html += R"HTML(
    <script>
        var selectedShots = [];
        var currentSort = { field: 'date', dir: 'desc' };
        var savedSearches = [];

        // Promote a shot to a recipe (add-recipes): same action as the app's
        // "Recipe" button beside Load — prefills from the shot server-side.
        function promoteToRecipe(id) {
            var name = prompt('Name for the new recipe (e.g. Morning cappuccino):');
            if (!name || !name.trim()) return;
            var hasMilk = confirm('Is this a milk drink? (OK = yes, Cancel = no)');
            fetch('/api/recipes/from-shot/' + id, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ name: name.trim(), hasMilk: hasMilk })
            })
            .then(function(r) { return r.json().then(function(d) {
                if (!r.ok || d.error) throw new Error(d.error || ('Server error (' + r.status + ')'));
                return d;
            }); })
            .then(function() {
                if (confirm('Recipe created. Open the Recipes page?'))
                    window.location.href = '/recipes';
            })
            .catch(function(e) { alert('Could not create recipe: ' + e.message); });
        }

        function toggleSelect(id, card) {
            var idx = selectedShots.indexOf(id);
            if (idx >= 0) {
                selectedShots.splice(idx, 1);
                card.classList.remove("selected");
            } else {
                if (selectedShots.length < 5) {
                    selectedShots.push(id);
                    card.classList.add("selected");
                }
            }
            updateCompareBar();
        }

        function updateCompareBar() {
            var bar = document.getElementById("compareBar");
            var count = document.getElementById("selectedCount");
            var compareBtn = document.getElementById("compareBtn");
            if (selectedShots.length >= 1) {
                bar.classList.add("visible");
                count.textContent = selectedShots.length + " selected";
                compareBtn.disabled = selectedShots.length < 2;
            } else {
                bar.classList.remove("visible");
            }
            document.querySelectorAll(".shot-checkbox").forEach(function(cb) {
                cb.checked = selectedShots.indexOf(parseInt(cb.dataset.id)) >= 0;
            });
        }

        function clearSelection() {
            selectedShots = [];
            document.querySelectorAll(".shot-card").forEach(function(c) { c.classList.remove("selected"); });
            updateCompareBar();
        }

        function compareSelected() {
            if (selectedShots.length >= 2) {
                window.location.href = "/compare/" + selectedShots.join(",");
            }
        }

        function deleteSelected() {
            if (selectedShots.length === 0) return;
            var n = selectedShots.length;
            if (!confirm("Delete " + n + " shot" + (n > 1 ? "s" : "") + "? This cannot be undone.")) return;
            fetch("/api/shots/delete", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ ids: selectedShots })
            }).then(function(resp) {
                if (!resp.ok) throw new Error("Server error (" + resp.status + ")");
                return resp.json();
            }).then(function(data) {
                if (data.deleted > 0) {
                    window.location.reload();
                } else {
                    alert("Failed to delete shots.");
                }
            }).catch(function(e) { alert("Failed to delete shots: " + e.message); });
        }

)HTML";

    // Part 12: Script - search parsing (port of QML buildFilter)
    html += R"HTML(
        function parseSearchKeywords(text) {
            var filters = {};
            var searchText = text;
            var keywords = [
                { pattern: /\brating:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minRating", maxKey: "maxRating" },
                { pattern: /\brating:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minRating", maxKey: null },
                { pattern: /\brating:(\d+(?:\.\d+)?)\b/g, minKey: "minRating", maxKey: "maxRating", exact: true },
                { pattern: /\bdose:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minDose", maxKey: "maxDose" },
                { pattern: /\bdose:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minDose", maxKey: null },
                { pattern: /\bdose:(\d+(?:\.\d+)?)\b/g, minKey: "minDose", maxKey: "maxDose", exact: true },
                { pattern: /\byield:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minYield", maxKey: "maxYield" },
                { pattern: /\byield:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minYield", maxKey: null },
                { pattern: /\byield:(\d+(?:\.\d+)?)\b/g, minKey: "minYield", maxKey: "maxYield", exact: true },
                { pattern: /\btime:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minDuration", maxKey: "maxDuration" },
                { pattern: /\btime:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minDuration", maxKey: null },
                { pattern: /\btime:(\d+(?:\.\d+)?)\b/g, minKey: "minDuration", maxKey: "maxDuration", exact: true },
                { pattern: /\btds:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minTds", maxKey: "maxTds" },
                { pattern: /\btds:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minTds", maxKey: null },
                { pattern: /\btds:(\d+(?:\.\d+)?)\b/g, minKey: "minTds", maxKey: "maxTds", exact: true },
                { pattern: /\bey:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minEy", maxKey: "maxEy" },
                { pattern: /\bey:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minEy", maxKey: null },
                { pattern: /\bey:(\d+(?:\.\d+)?)\b/g, minKey: "minEy", maxKey: "maxEy", exact: true }
            ];
            for (var i = 0; i < keywords.length; i++) {
                var kw = keywords[i];
                var match = kw.pattern.exec(searchText);
                if (match) {
                    if (match.length === 3) {
                        filters[kw.minKey] = parseFloat(match[1]);
                        filters[kw.maxKey] = parseFloat(match[2]);
                    } else if (kw.exact) {
                        filters[kw.minKey] = parseFloat(match[1]);
                        filters[kw.maxKey] = parseFloat(match[1]);
                    } else {
                        filters[kw.minKey] = parseFloat(match[1]);
                    }
                    searchText = searchText.replace(match[0], "");
                }
            }
            // Strip remaining keyword tokens
            searchText = searchText.replace(/\b(rating|dose|yield|time|tds|ey):\d+(?:\.\d+)?(?:-\d+(?:\.\d+)?|\+)?/g, "");

            // recipe: — the one string-valued keyword, kept in step with the app's
            // buildFilter(). Two forms (recipe:dad, recipe:"dad tuesday"), both
            // SUBSTRING matches against the card's data-recipe. Unlike the bare
            // text below — which matches the whole card, recipe name included —
            // this narrows to the recipe name alone.
            // Kept in step with the app's buildFilter(), \S+ included: a bare
            // "recipe:" is not a keyword (so "recipe: dad" still searches for
            // "dad"), while an explicitly empty quoted term matches nothing.
            var recipeMatch = /\brecipe:(?:"([^"]*)"?|(\S+))/i.exec(searchText);
            if (recipeMatch) {
                var recipeTerm = recipeMatch[1] !== undefined ? recipeMatch[1] : recipeMatch[2];
                recipeTerm = (recipeTerm || "").trim();
                // A FLAG, not a sentinel term. The app encodes "empty" as a
                // whitespace term because its SQL path splits on whitespace and
                // degenerates to a hard-false condition — but this path tests
                // `includes()`, and "dad monday".includes(" ") is TRUE, so the
                // same sentinel would show every multi-word recipe instead of
                // none. Same intent, opposite result, on two surfaces that must
                // agree.
                if (recipeTerm.length > 0)
                    filters.recipeName = recipeTerm.toLowerCase();
                else
                    filters.recipeNameEmpty = true;
                searchText = searchText.replace(recipeMatch[0], "");
            }
            searchText = searchText.replace(/\brecipe:(?:"[^"]*"?|\S*)/gi, "");

            searchText = searchText.trim().replace(/\s+/g, " ");
            return { filters: filters, remaining: searchText };
        }

        function onSearchChange() {
            var input = document.getElementById('searchInput');
            var text = input.value;
            var parsed = parseSearchKeywords(text);
            var f = parsed.filters;
            var remaining = parsed.remaining.toLowerCase();

            var cards = Array.from(document.querySelectorAll('.shot-card'));
            var visibleCount = 0;
            cards.forEach(function(card) {
                var show = true;
                // Numeric keyword filters
                if (f.minRating !== undefined) { if (parseFloat(card.dataset.rating) < f.minRating) show = false; }
                if (f.maxRating !== undefined) { if (parseFloat(card.dataset.rating) > f.maxRating) show = false; }
                if (f.minDose !== undefined) { if (parseFloat(card.dataset.dose) < f.minDose) show = false; }
                if (f.maxDose !== undefined) { if (parseFloat(card.dataset.dose) > f.maxDose) show = false; }
                if (f.minYield !== undefined) { if (parseFloat(card.dataset.yield) < f.minYield) show = false; }
                if (f.maxYield !== undefined) { if (parseFloat(card.dataset.yield) > f.maxYield) show = false; }
                if (f.minDuration !== undefined) { if (parseFloat(card.dataset.duration) < f.minDuration) show = false; }
                if (f.maxDuration !== undefined) { if (parseFloat(card.dataset.duration) > f.maxDuration) show = false; }
                if (f.minTds !== undefined) { if (parseFloat(card.dataset.tds) < f.minTds) show = false; }
                if (f.maxTds !== undefined) { if (parseFloat(card.dataset.tds) > f.maxTds) show = false; }
                if (f.minEy !== undefined) { if (parseFloat(card.dataset.ey) < f.minEy) show = false; }
                if (f.maxEy !== undefined) { if (parseFloat(card.dataset.ey) > f.maxEy) show = false; }
                // Scoped to the recipe name only. A card with no recipe has no
                // data-recipe at all, so it correctly never matches.
                if (f.recipeNameEmpty) {
                    show = false;   // narrowing request with no term matches nothing
                } else if (f.recipeName !== undefined) {
                    var cardRecipe = (card.dataset.recipe || '').toLowerCase();
                    // Word-order independent, matching the app's per-term ANDs.
                    var recipeWords = f.recipeName.split(/\s+/);
                    for (var rw = 0; rw < recipeWords.length; rw++) {
                        if (recipeWords[rw] && !cardRecipe.includes(recipeWords[rw])) {
                            show = false;
                            break;
                        }
                    }
                }
                // Text search: split into words (AND logic, matching app behavior)
                if (remaining) {
                    var searchWords = remaining.replace(/[\-\/.]/g, ' ').split(/\s+/);
                    var cardText = card.textContent.toLowerCase();
                    for (var w = 0; w < searchWords.length; w++) {
                        if (searchWords[w] && !cardText.includes(searchWords[w])) { show = false; break; }
                    }
                }
                card.style.display = show ? '' : 'none';
                if (show) visibleCount++;
            });
            sortVisibleCards();
            document.getElementById('visibleCount').textContent = 'Showing ' + visibleCount + ' shots';
            updateSaveButton();
        }

        function sortVisibleCards() {
            var grid = document.getElementById('shotGrid');
            var cards = Array.from(document.querySelectorAll('.shot-card'));
            var visibleCards = cards.filter(function(c) { return c.style.display !== 'none'; });
            visibleCards.sort(function(a, b) {
                var aVal, bVal;
                var field = currentSort.field;
                var dir = currentSort.dir === 'asc' ? 1 : -1;
                if (field === 'date') { aVal = parseInt(a.dataset.date) || 0; bVal = parseInt(b.dataset.date) || 0; return dir * (aVal - bVal); }
                else if (field === 'profile') { aVal = (a.dataset.profile || '').toLowerCase(); bVal = (b.dataset.profile || '').toLowerCase(); return dir * aVal.localeCompare(bVal); }
                else if (field === 'brand') { aVal = (a.dataset.brand || '').toLowerCase(); bVal = (b.dataset.brand || '').toLowerCase(); return dir * aVal.localeCompare(bVal); }
                else if (field === 'coffee') { aVal = (a.dataset.coffee || '').toLowerCase(); bVal = (b.dataset.coffee || '').toLowerCase(); return dir * aVal.localeCompare(bVal); }
                else if (field === 'rating') { aVal = parseFloat(a.dataset.rating) || 0; bVal = parseFloat(b.dataset.rating) || 0; return dir * (aVal - bVal); }
                else if (field === 'ratio') { aVal = parseFloat(a.dataset.ratio) || 0; bVal = parseFloat(b.dataset.ratio) || 0; return dir * (aVal - bVal); }
                else if (field === 'duration') { aVal = parseFloat(a.dataset.duration) || 0; bVal = parseFloat(b.dataset.duration) || 0; return dir * (aVal - bVal); }
                else if (field === 'dose') { aVal = parseFloat(a.dataset.dose) || 0; bVal = parseFloat(b.dataset.dose) || 0; return dir * (aVal - bVal); }
                else if (field === 'yield') { aVal = parseFloat(a.dataset.yield) || 0; bVal = parseFloat(b.dataset.yield) || 0; return dir * (aVal - bVal); }
                return 0;
            });
            visibleCards.forEach(function(card) { grid.appendChild(card); });
        }

        function setSearch(text) {
            document.getElementById('searchInput').value = text;
            onSearchChange();
        }
)HTML";

    // Part 13: Script - saved searches
    html += WEB_JS_ESCAPE_HTML;
    html += R"HTML(

        function loadSavedSearches() {
            fetch('/api/saved-searches')
                .then(function(r) {
                    if (!r.ok) throw new Error('Server error (' + r.status + ')');
                    return r.json();
                })
                .then(function(data) {
                    savedSearches = data.searches || [];
                    updateSavedUI();
                })
                .catch(function(e) { console.warn('loadSavedSearches:', e.message); });
        }

        function saveSearch() {
            var text = document.getElementById('searchInput').value.trim();
            if (!text) return;
            if (savedSearches.indexOf(text) >= 0) return;
            fetch('/api/saved-searches', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ search: text })
            }).then(function(r) {
                if (!r.ok) throw new Error('Server error (' + r.status + ')');
                return r.json();
            }).then(function(data) {
                if (data.success) {
                    savedSearches.push(text);
                    updateSavedUI();
                    updateSaveButton();
                }
            }).catch(function(e) { alert('Failed to save search: ' + e.message); });
        }

        function deleteSavedSearch(text) {
            fetch('/api/saved-searches', {
                method: 'DELETE',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ search: text })
            }).then(function(r) {
                if (!r.ok) throw new Error('Server error (' + r.status + ')');
                return r.json();
            }).then(function(data) {
                if (data.success) {
                    var idx = savedSearches.indexOf(text);
                    if (idx >= 0) savedSearches.splice(idx, 1);
                    updateSavedUI();
                    updateSaveButton();
                }
            }).catch(function(e) { alert('Failed to delete search: ' + e.message); });
        }

        function applySavedSearch(text) {
            document.getElementById('searchInput').value = text;
            document.getElementById('savedPanel').classList.remove('open');
            onSearchChange();
        }

        function updateSavedUI() {
            var list = document.getElementById('savedList');
            var btn = document.getElementById('savedBtn');
            while (list.firstChild) list.removeChild(list.firstChild);
            if (savedSearches.length === 0) {
                btn.style.display = 'none';
                document.getElementById('savedPanel').classList.remove('open');
                return;
            }
            btn.style.display = '';
            for (var i = 0; i < savedSearches.length; i++) {
                var item = document.createElement('div');
                item.className = 'saved-search-item';
                var textSpan = document.createElement('span');
                textSpan.className = 'saved-search-text';
                textSpan.textContent = savedSearches[i];
                textSpan.addEventListener('click', (function(s) {
                    return function() { applySavedSearch(s); };
                })(savedSearches[i]));
                var delSpan = document.createElement('span');
                delSpan.className = 'saved-search-delete';
                delSpan.textContent = '\u00d7';
                delSpan.addEventListener('click', (function(s) {
                    return function() { deleteSavedSearch(s); };
                })(savedSearches[i]));
                item.appendChild(textSpan);
                item.appendChild(delSpan);
                list.appendChild(item);
            }
        }

        function updateSaveButton() {
            var btn = document.getElementById('saveBtn');
            var text = document.getElementById('searchInput').value.trim();
            btn.disabled = !text || savedSearches.indexOf(text) >= 0;
        }

        function toggleSavedSearches() {
            document.getElementById('savedPanel').classList.toggle('open');
            document.getElementById('helpPanel').classList.remove('open');
        }

        function toggleKeywords() {
            document.getElementById('helpPanel').classList.toggle('open');
            document.getElementById('savedPanel').classList.remove('open');
        }

        function insertSearchKeyword(keyword) {
            var input = document.getElementById('searchInput');
            var text = input.value;
            if (text.length > 0 && !text.endsWith(' ')) {
                text += ' ';
            }
            input.value = text + keyword;
            document.getElementById('helpPanel').classList.remove('open');
            input.focus();
        }
)HTML";

    // Part 14: Script - sort and menu functions
    html += R"HTML(
        var sortLabels = { date: "Date", profile: "Profile", brand: "Roaster", coffee: "Coffee", rating: "Rating", ratio: "Ratio", duration: "Duration", dose: "Dose", yield: "Yield" };

        function toggleSortMenu() {
            var dd = document.getElementById("sortDropdown");
            dd.classList.toggle("open");
            // Right-aligned to its button; flipped when that would leave the screen
            // (on a phone the button can wrap to the left edge).
            dd.style.left = ""; dd.style.right = "";
            if (dd.classList.contains("open") && dd.getBoundingClientRect().left < 0) {
                dd.style.left = "0"; dd.style.right = "auto";
            }
        }

        function selectSort(field) {
            var opts = document.querySelectorAll('.sort-option');
            var defaultDir = 'desc';
            opts.forEach(function(opt) {
                if (opt.dataset.sort === field) {
                    opt.classList.add('active');
                    defaultDir = opt.dataset.defaultDir;
                } else {
                    opt.classList.remove('active');
                }
            });
            currentSort.field = field;
            currentSort.dir = defaultDir;
            document.getElementById("sortFieldBtn").innerHTML = sortLabels[field] + " &#9662;";
            document.getElementById("sortDirBtn").innerHTML = defaultDir === 'asc' ? "&#9650;" : "&#9660;";
            document.getElementById("sortDropdown").classList.remove("open");
            onSearchChange();
        }

        function toggleSortDir() {
            currentSort.dir = currentSort.dir === 'asc' ? 'desc' : 'asc';
            document.getElementById("sortDirBtn").innerHTML = currentSort.dir === 'asc' ? "&#9650;" : "&#9660;";
            onSearchChange();
        }

        function toggleMenu() {
            document.getElementById("menuDropdown").classList.toggle("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            if (!e.target.closest(".menu-btn") && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
            var sortDrop = document.getElementById("sortDropdown");
            if (!e.target.closest(".sort-anchor") && sortDrop.classList.contains("open")) {
                sortDrop.classList.remove("open");
            }
        });
)HTML";

    // Part 15: Script - power functions and init
    html += WEB_JS_POWER_CONTROL;
    html += R"HTML(
        loadSavedSearches();
    </script>
</body>
</html>
)HTML";

    return html;
}

QString ShotServer::generateShotDetailPage(qint64 shotId, const ShotProjection& shot) const
{
    if (!shot.isValid()) {
        return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Not Found</title></head>"
                  "<body style=\"background:#0d1117;color:#fff;font-family:sans-serif;padding:2rem;\">"
                  "<h1>Shot not found</h1><a href=\"/\" style=\"color:#c9a227;\">Back to list</a></body></html>");
    }

    double ratio = 0;
    if (shot.doseWeightG > 0) {
        ratio = shot.finalWeightG / shot.doseWeightG;
    }

    // Rating display: "N%" when rated, "-" when unrated. Mirrors ShotDetailPage.qml
    // ("rating: N%" / "-"). 0-100 enjoyment is the canonical scale across the app.
    QString ratingText = (shot.enjoyment0to100 > 0)
        ? (QString::number(shot.enjoyment0to100) + QStringLiteral("%"))
        : QStringLiteral("-");

    // Escape for embedding in JavaScript string literals (inside double quotes).
    // Does NOT double `%` → `%%` — see escapeForJs in generateShotListPage above
    // for the full rationale. Short version: the doubling was visible-output
    // wrong (Qt's arg() never reduces `%%` back to `%`) and didn't actually
    // prevent placeholder shadowing either ("%12" still parses as %12 after
    // doubling).
    auto jsEscape = [](const QString& s) -> QString {
        QString r = s;
        r.replace(QLatin1String("\\"), QLatin1String("\\\\"));
        r.replace(QLatin1String("\""), QLatin1String("\\\""));
        r.replace(QLatin1String("\n"), QLatin1String("\\n"));
        r.replace(QLatin1String("\r"), QLatin1String(""));
        r.replace(QLatin1String("<"), QLatin1String("\\u003c")); // Prevent script tag breakout
        return r;
    };

    // Temperature and target weight (always have values)
    const double tempOverride = shot.temperatureOverrideC;
    const double targetWeight = shot.targetWeightG;
    const double finalWeight = shot.finalWeightG;

    // Build yield display with optional target
    QString yieldDisplay = QString("%1g").arg(finalWeight, 0, 'f', 1);
    if (targetWeight > 0 && qAbs(targetWeight - finalWeight) > 0.5) {
        yieldDisplay += QString(" <span class=\"target\">(%1g)</span>").arg(targetWeight, 0, 'f', 0);
    }

    // Convert time-series data to JSON arrays for Chart.js
    auto pointsToJson = [](const QVariantList& points) -> QString {
        QStringList items;
        for (const QVariant& p : points) {
            QVariantMap pt = p.toMap();
            items << QString("{x:%1,y:%2}").arg(pt["x"].toDouble(), 0, 'f', 2).arg(pt["y"].toDouble(), 0, 'f', 2);
        }
        return "[" + items.join(",") + "]";
    };

    // Convert goal data with nulls at gaps (where time jumps > 0.5s)
    auto goalPointsToJson = [](const QVariantList& points) -> QString {
        QStringList items;
        double lastX = -999;
        for (const QVariant& p : points) {
            QVariantMap pt = p.toMap();
            double x = pt["x"].toDouble();
            double y = pt["y"].toDouble();
            // Insert null to break line if there's a gap > 0.5 seconds
            if (lastX >= 0 && (x - lastX) > 0.5) {
                items << QString("{x:%1,y:null}").arg((lastX + x) / 2, 0, 'f', 2);
            }
            items << QString("{x:%1,y:%2}").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2);
            lastX = x;
        }
        return "[" + items.join(",") + "]";
    };

    QString pressureData = pointsToJson(shot.pressure);
    QString flowData = pointsToJson(shot.flow);
    QString tempData = pointsToJson(shot.temperature);
    QString weightData = pointsToJson(shot.weight);
    QString weightFlowRateData = pointsToJson(shot.weightFlowRate);
    QString resistanceData = pointsToJson(shot.resistance);
    QString pressureGoalData = goalPointsToJson(shot.pressureGoal);
    QString flowGoalData = goalPointsToJson(shot.flowGoal);

    // Convert phase markers to JSON for Chart.js
    auto phasesToJson = [](const QVariantList& phases) -> QString {
        QStringList items;
        for (const QVariant& p : phases) {
            QVariantMap phase = p.toMap();
            QString label = phase["label"].toString();
            if (label == "Start") continue;  // Skip start marker
            // Escape both label and reason for safe embedding in JS string literals
            auto jsStringEscape = [](QString s) -> QString {
                s.replace(QLatin1String("\\"), QLatin1String("\\\\"));
                s.replace(QLatin1String("\""), QLatin1String("\\\""));
                s.replace(QLatin1String("\n"), QLatin1String("\\n"));
                s.replace(QLatin1String("\r"), QLatin1String(""));
                s.replace(QLatin1String("<"), QLatin1String("\\u003c"));
                return s;
            };
            QString reason = jsStringEscape(phase["transitionReason"].toString());
            label = jsStringEscape(label);
            items << QString("{time:%1,label:\"%2\",reason:\"%3\"}")
                .arg(phase["time"].toDouble(), 0, 'f', 2)
                .arg(label)
                .arg(reason);
        }
        return "[" + items.join(",") + "]";
    };
    QString phaseData = phasesToJson(shot.phases);

    // Build quality-badge chips and the Shot Summary modal contents from the
    // analyzeShot() outputs that already arrived on the projection. Mirrors
    // the in-app QualityBadges + ShotAnalysisDialog. Both blobs contain
    // detector-generated text that may include literal `%` (e.g. "75% of
    // goal"), so they are NOT passed through .arg() — they are injected via
    // replace() AFTER the .arg() chain below into __BADGES_HTML__ /
    // __SUMMARY_LINES_HTML__ markers in the template. That sidesteps the
    // QString::arg() placeholder-shadowing trap entirely; doubling `%` to
    // `%%` doesn't actually escape (Qt's arg never reduces `%%` back to
    // `%`, and "%5"→"%%5" still parses as a %5 placeholder via the
    // continue+rescan in qstring.cpp).
    auto badgeChip = [](const QString& kind, const QString& text) -> QString {
        return QString("<span class=\"badge %1\"><span class=\"dot\"></span>%2</span>")
            .arg(kind, text);
    };
    QString badgesHtml = QStringLiteral("<div class=\"shot-quality\">");
    const bool hasFlag = shot.channelingDetected
                       || shot.grindIssueDetected || shot.pourTruncatedDetected
                       || shot.skipFirstFrameDetected;
    if (shot.channelingDetected)     badgesHtml += badgeChip("danger",  "Channeling detected");
    if (shot.grindIssueDetected)     badgesHtml += badgeChip("warning", "Grind issue");
    if (shot.pourTruncatedDetected)  badgesHtml += badgeChip("danger",  "Puck failed");
    if (shot.skipFirstFrameDetected) badgesHtml += badgeChip("danger",  "First step skipped");
    if (!hasFlag)                    badgesHtml += badgeChip("success", "Clean extraction");
    if (!shot.summaryLines.isEmpty()) {
        badgesHtml += QStringLiteral(
            "<button class=\"summary-btn\" onclick=\"openSummaryDialog()\">"
            "&#128202; Shot Summary</button>");
    }
    badgesHtml += QStringLiteral("</div>");

    QString summaryLinesHtml;
    for (const QVariant& line : shot.summaryLines) {
        const QVariantMap m = line.toMap();
        const QString type = m.value("type").toString();
        const QString text = m.value("text").toString().toHtmlEscaped();
        summaryLinesHtml += QString("<div class=\"summary-line %1\"><span class=\"line-dot\"></span><span>%2</span></div>")
            .arg(type, text);
    }
    if (summaryLinesHtml.isEmpty()) {
        summaryLinesHtml = QStringLiteral("<div class=\"summary-line\"><span>No summary available.</span></div>");
    }

    QString html = QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>%1 - Decenza</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
    <style>)HTML" + QString::fromLatin1(WEB_CSS_VARIABLES) + WEB_CSS_HEADER + WEB_CSS_MENU + R"HTML(
        .header-content { max-width: 1400px; gap: 1rem; }
        .back-btn { line-height: 1; padding: 0.25rem; }
        .menu-wrapper { margin-left: auto; }
        .header-title {
            flex: 1;
        }
        .header-title h1 {
            font-size: 1.125rem;
            font-weight: 600;
        }
        .header-title .subtitle {
            font-size: 0.75rem;
            color: var(--text-secondary);
        }
        /* Recipe identity on the detail page (history-recipe-identity). Archived
           dims AND carries a title attribute, so the state is not colour-only. */
        .shot-drink-icon { margin-right: 0.35rem; }
        .header-title .shot-detail-recipe { color: var(--accent); font-weight: 600; }
        .header-title .shot-detail-recipe.archived {
            color: var(--text-secondary);
            font-weight: 500;
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1.5rem;
        }
        .metrics-bar {
            display: flex;
            gap: 1rem;
            flex-wrap: wrap;
            margin-bottom: 1.5rem;
        }
        .metric-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 1rem 1.25rem;
            min-width: 100px;
            text-align: center;
        }
        .metric-card .value {
            font-size: 1.5rem;
            font-weight: 700;
            color: var(--accent);
        }
        .metric-card .value .target {
            font-size: 0.875rem;
            font-weight: 400;
            color: var(--text-secondary);
        }
        .metric-card .label {
            font-size: 0.6875rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .shot-quality {
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            gap: 0.5rem;
            flex: 1;
            min-width: 0;
            padding: 0 0.5rem;
        }
        .badge {
            display: inline-flex;
            align-items: center;
            gap: 0.375rem;
            padding: 0 0.75rem;
            height: 28px;
            border-radius: 14px;
            border: 1px solid;
            font-size: 0.75rem;
            line-height: 1;
            white-space: nowrap;
        }
        .badge .dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
        }
        .badge.danger { color: #e73249; border-color: #e73249; background: rgba(231,50,73,0.15); }
        .badge.danger .dot { background: #e73249; }
        .badge.warning { color: #f0a020; border-color: #f0a020; background: rgba(240,160,32,0.15); }
        .badge.warning .dot { background: #f0a020; }
        .badge.success { color: #18c37e; border-color: #18c37e; background: rgba(24,195,126,0.15); }
        .badge.success .dot { background: #18c37e; }
        .summary-btn {
            display: inline-flex;
            align-items: center;
            gap: 0.375rem;
            padding: 0 0.75rem;
            height: 28px;
            border-radius: 14px;
            background: var(--surface);
            border: 1px solid var(--border);
            color: var(--text-secondary);
            font-size: 0.75rem;
            cursor: pointer;
            font-family: inherit;
            line-height: 1;
            white-space: nowrap;
        }
        .summary-btn:hover { color: var(--accent); border-color: var(--accent); }
        .summary-modal {
            display: none;
            position: fixed;
            inset: 0;
            background: rgba(0,0,0,0.6);
            z-index: 300;
            align-items: center;
            justify-content: center;
            padding: 1rem;
        }
        .summary-modal.open { display: flex; }
        .summary-modal-content {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.5rem;
            max-width: 450px;
            width: 100%;
            max-height: 80vh;
            overflow-y: auto;
        }
        .summary-modal-content h2 {
            text-align: center;
            font-size: 1rem;
            font-weight: 600;
            margin-bottom: 1rem;
        }
        .summary-line {
            display: flex;
            align-items: flex-start;
            gap: 0.5rem;
            padding: 0.375rem 0;
            color: var(--text-secondary);
            font-size: 0.875rem;
            line-height: 1.4;
        }
        .summary-line .line-dot {
            width: 6px;
            height: 6px;
            border-radius: 50%;
            margin-top: 0.5rem;
            flex-shrink: 0;
        }
        .summary-line.good .line-dot { background: #18c37e; }
        .summary-line.caution .line-dot { background: #f0a020; }
        .summary-line.warning .line-dot { background: #e73249; }
        .summary-line.observation .line-dot { background: var(--text-secondary); }
        .summary-line.verdict {
            color: var(--text);
            font-weight: 500;
            padding-top: 0.75rem;
            margin-top: 0.5rem;
            border-top: 1px solid var(--border);
        }
        .summary-line.verdict .line-dot { display: none; }
        .summary-modal-close {
            width: 100%;
            margin-top: 1rem;
            padding: 0.625rem;
            background: var(--accent);
            border: none;
            border-radius: 8px;
            color: #000;
            font-weight: 500;
            cursor: pointer;
            font-family: inherit;
            font-size: 0.875rem;
        }
        .summary-modal-close:hover { opacity: 0.9; }
        .chart-container {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1.5rem;
        }
        .chart-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 1rem;
            flex-wrap: wrap;
            gap: 0.5rem;
        }
        .chart-title {
            font-size: 1rem;
            font-weight: 600;
        }
        .chart-toggles {
            display: flex;
            gap: 0.5rem;
            flex-wrap: wrap;
        }
        .toggle-btn {
            padding: 0.375rem 0.75rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text-secondary);
            font-size: 0.75rem;
            cursor: pointer;
            transition: all 0.15s ease;
            display: flex;
            align-items: center;
            gap: 0.375rem;
        }
        .toggle-btn:hover { border-color: var(--text-secondary); }
        .toggle-btn.active { background: var(--surface-hover); color: var(--text); }
        .toggle-btn .dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
        }
        .toggle-btn.pressure .dot { background: var(--pressure); }
        .toggle-btn.flow .dot { background: var(--flow); }
        .toggle-btn.temp .dot { background: var(--temp); }
        .toggle-btn.weight .dot { background: var(--weight); }
        .toggle-btn.weightFlow .dot { background: var(--weightFlow); }
        .toggle-btn.resistance .dot { background: var(--resistance); }
        .chart-wrapper {
            position: relative;
            height: 400px;
        }
        .info-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 1rem;
        }
        .info-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.25rem;
        }
        .info-card h3 {
            font-size: 0.875rem;
            font-weight: 600;
            margin-bottom: 0.75rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .info-row {
            display: flex;
            justify-content: space-between;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
        }
        .info-row:last-child { border-bottom: none; }
        .info-row .label { color: var(--text-secondary); }
        .info-row .value { font-weight: 500; }
        .notes-text {
            color: var(--text-secondary);
            font-style: italic;
        }
        .rating { color: var(--accent); font-size: 1.125rem; }
        .edit-btn {
            background: none;
            border: 1px solid var(--border);
            color: var(--text-secondary);
            font-size: 0.875rem;
            cursor: pointer;
            padding: 0.375rem 0.75rem;
            border-radius: 6px;
            white-space: nowrap;
        }
        .edit-btn:hover { color: var(--accent); border-color: var(--accent); }
        .edit-bar {
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1rem 1.5rem;
            display: none;
            justify-content: center;
            gap: 1rem;
            z-index: 200;
        }
        .edit-bar.visible { display: flex; }
        .edit-bar button {
            padding: 0.75rem 2rem;
            border: none;
            border-radius: 8px;
            font-size: 0.9375rem;
            font-weight: 600;
            cursor: pointer;
        }
        .save-btn { background: var(--accent); color: #000; }
        .save-btn:hover { opacity: 0.9; }
        .cancel-btn { background: var(--surface-hover); color: var(--text); border: 1px solid var(--border) !important; }
        .cancel-btn:hover { border-color: var(--text-secondary) !important; }
        .edit-input, .edit-select, .edit-textarea {
            width: 100%;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-family: inherit;
            font-size: 0.875rem;
            padding: 0.5rem 0.75rem;
        }
        .edit-input:focus, .edit-select:focus, .edit-textarea:focus {
            outline: none;
            border-color: var(--accent);
        }
        .edit-select { cursor: pointer; }
        .edit-select option { background: var(--surface); color: var(--text); }
        .edit-textarea { min-height: 15em; resize: vertical; }
        .notes-card-edit { grid-column: 1 / -1; }
        .edit-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
            gap: 1rem;
        }
        .edit-row:last-child { border-bottom: none; }
        .edit-row .label { color: var(--text-secondary); white-space: nowrap; min-width: 80px; }
        .edit-row .edit-field { flex: 1; text-align: right; }
        .edit-row .edit-input, .edit-row .edit-select { text-align: right; }
        .metric-card .edit-input { text-align: center; width: 80px; }
        @media (max-width: 600px) {
            .container { padding: 1rem; }
            .chart-wrapper { height: 300px; }
            .metrics-bar { justify-content: center; }
        }
    </style>
</head>)HTML" R"HTML(
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <div class="header-title">
                <h1>%1</h1>
                <div class="subtitle">%2</div>
                __DETAIL_RECIPE__
            </div>
            <button class="edit-btn" id="editBtn" onclick="toggleEditMode()">&#9998; Edit</button>
)HTML" + generateMenuHtml() + R"HTML(
        </div>
    </header>
    <main class="container">
        <div class="metrics-bar">
            <div class="metric-card">
                <div class="value">%3g</div>
                <div class="label">Dose</div>
            </div>
            <div class="metric-card">
                <div class="value">%4</div>
                <div class="label">Yield</div>
            </div>
            <div class="metric-card">
                <div class="value">1:%5</div>
                <div class="label">Ratio</div>
            </div>
            <div class="metric-card">
                <div class="value">%6s</div>
                <div class="label">Time</div>
            </div>
            <div class="metric-card">
                <div class="value rating">%7</div>
                <div class="label">Rating</div>
            </div>
            __BADGES_HTML__
        </div>

        <div class="chart-container">
            <div class="chart-header">
                <div class="chart-title">Extraction Curves</div>
                <div class="chart-toggles">
                    <button class="toggle-btn pressure active" onclick="toggleDataset(0, this)">
                        <span class="dot"></span> Pressure
                    </button>
                    <button class="toggle-btn flow active" onclick="toggleDataset(1, this)">
                        <span class="dot"></span> Flow
                    </button>
                    <button class="toggle-btn weight active" onclick="toggleDataset(2, this)">
                        <span class="dot"></span> Yield
                    </button>
                    <button class="toggle-btn temp active" onclick="toggleDataset(3, this)">
                        <span class="dot"></span> Temp
                    </button>
                    <button class="toggle-btn weightFlow active" onclick="toggleDataset(6, this)">
                        <span class="dot"></span> Weight Flow
                    </button>
                    <button class="toggle-btn resistance" onclick="toggleDataset(7, this)">
                        <span class="dot"></span> Resistance
                    </button>
                </div>
            </div>
            <div class="chart-wrapper">
                <canvas id="shotChart"></canvas>
            </div>
        </div>

        <div class="info-grid">
            <div class="info-card" style="grid-column:1/-1;">
                <h3>Notes</h3>
                <p class="notes-text">%14</p>
            </div>
            <div class="info-card">
                <h3>Beans (%13)</h3>
                <div class="info-row">
                    <span class="label">Brand</span>
                    <span class="value">%8</span>
                </div>
                <div class="info-row">
                    <span class="label">Type</span>
                    <span class="value">%9</span>
                </div>
                <div class="info-row">
                    <span class="label">Roast Date</span>
                    <span class="value">%10</span>
                </div>
                <div class="info-row">
                    <span class="label">Roast Level</span>
                    <span class="value">%11</span>
                </div>
            </div>
            <div class="info-card">
                <h3>Grinder</h3>
                <div class="info-row">
                    <span class="label">Model</span>
                    <span class="value">%12</span>
                </div>
                <div class="info-row">
                    <span class="label">Setting</span>
                    <span class="value">%13</span>
                </div>
            </div>
        </div>

        <div class="actions-bar" style="margin-top:1.5rem;display:flex;gap:1rem;flex-wrap:wrap;">
            <button onclick="downloadProfile()" style="display:inline-flex;align-items:center;gap:0.5rem;padding:0.75rem 1.25rem;background:var(--surface);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:0.875rem;cursor:pointer;">
                &#128196; Download Profile JSON
            </button>
            <button onclick="window.location.href=window.location.pathname+'/shot.json'" style="display:inline-flex;align-items:center;gap:0.5rem;padding:0.75rem 1.25rem;background:var(--surface);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:0.875rem;cursor:pointer;">
                &#11015; Download Shot JSON
            </button>
            <button onclick="var c=document.getElementById('debugLogContainer'); if(c){if(c.style.display==='none'){c.style.display='block';c.scrollIntoView({behavior:'smooth'});}else{c.style.display='none';}}" style="display:inline-flex;align-items:center;gap:0.5rem;padding:0.75rem 1.25rem;background:var(--surface);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:0.875rem;cursor:pointer;">
                &#128203; View Debug Log
            </button>
        </div>

        <div id="debugLogContainer" style="display:none;margin-top:1rem;">
            <div class="info-card">
                <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:0.75rem;">
                    <h3 style="margin-bottom:0;">Debug Log</h3>
                    <button onclick="copyDebugLog()" style="padding:0.5rem 1rem;background:var(--accent);border:none;border-radius:6px;color:#000;font-weight:500;cursor:pointer;font-size:0.8125rem;">Copy to Clipboard</button>
                </div>
                <pre id="debugLogContent" style="background:var(--bg);padding:1rem;border-radius:8px;overflow-x:auto;font-size:0.75rem;line-height:1.4;white-space:pre-wrap;word-break:break-all;max-height:500px;overflow-y:auto;">%21</pre>
            </div>
        </div>
    </main>

    <div class="edit-bar" id="editBar">
        <button class="save-btn" onclick="saveChanges()">Save</button>
        <button class="cancel-btn" onclick="cancelEdit()">Cancel</button>
    </div>

    <div class="summary-modal" id="summaryModal" onclick="if(event.target===this)closeSummaryDialog()">
        <div class="summary-modal-content">
            <h2>Shot Summary</h2>
            __SUMMARY_LINES_HTML__
            <button class="summary-modal-close" onclick="closeSummaryDialog()">OK</button>
        </div>
    </div>

    <script>
        var shotData = {
            id: %24,
            beanBrand: "%25",
            beanType: "%26",
            roastDate: "%27",
            roastLevel: "%28",
            grinderBrand: "%40",
            grinderModel: "%29",
            grinderBurrs: "%41",
            grinderSetting: "%30",
            espressoNotes: "%31",
            doseWeightG: %32,
            finalWeightG: %33,
            enjoyment: %34,
            barista: "%35",
            beverageType: "%36",
            drinkTds: %37,
            drinkEy: %38
        };
    </script>
)HTML" R"HTML(

    <script>
        function downloadProfile() {
            window.location.href = window.location.pathname + '/profile.json';
        }
        function showDebugLog() {
            var container = document.getElementById('debugLogContainer');
            if (container) {
                container.style.display = container.style.display === 'none' ? 'block' : 'none';
            } else {
                alert('Debug log container not found');
            }
        }
        function copyDebugLog() {
            var text = document.getElementById('debugLogContent').textContent;
            // Use fallback for non-HTTPS (clipboard API requires secure context)
            var textarea = document.createElement('textarea');
            textarea.value = text;
            textarea.style.position = 'fixed';
            textarea.style.opacity = '0';
            document.body.appendChild(textarea);
            textarea.select();
            try {
                document.execCommand('copy');
            } catch (err) {
                alert('Failed to copy: ' + err);
            }
            document.body.removeChild(textarea);
        }
        var isEditMode = false;
        var originalMetricsHTML = '';
        var originalInfoGridHTML = '';
        var originalActionsDisplay = '';
        var originalDebugDisplay = '';

        function toggleEditMode() {
            if (isEditMode) return;
            isEditMode = true;

            var metricsBar = document.querySelector('.metrics-bar');
            var infoGrid = document.querySelector('.info-grid');
            var actionsBar = document.querySelector('.actions-bar');
            var editBar = document.getElementById('editBar');
            var editBtn = document.getElementById('editBtn');
            var debugContainer = document.getElementById('debugLogContainer');

            originalMetricsHTML = metricsBar.innerHTML;
            originalInfoGridHTML = infoGrid.innerHTML;
            originalActionsDisplay = actionsBar.style.display;

            // Build edit form for metrics bar using DOM
            // Note: shotData values are server-escaped and trusted (from our own database)
            var ratingValue = shotData.enjoyment > 0 ? shotData.enjoyment : 0;
            var metricsHtml =
                '<div class="metric-card"><input type="number" class="edit-input" id="editDose" step="0.1" value="' + shotData.doseWeightG + '" oninput="autoCalcEY()"><div class="label">Dose (g)</div></div>' +
                '<div class="metric-card"><input type="number" class="edit-input" id="editYield" step="0.1" value="' + shotData.finalWeightG + '" oninput="autoCalcEY()"><div class="label">Yield (g)</div></div>' +
                '<div class="metric-card"><input type="number" class="edit-input" id="editRating" min="0" max="100" step="1" value="' + ratingValue + '"><div class="label">Rating (%)</div></div>';
            metricsBar.innerHTML = metricsHtml;

            var roastLevels = ['', 'Light', 'Medium-Light', 'Medium', 'Medium-Dark', 'Dark'];
            var roastOptions = '';
            for (var j = 0; j < roastLevels.length; j++) {
                var rl = roastLevels[j];
                roastOptions += '<option value="' + rl + '"' + (rl === shotData.roastLevel ? ' selected' : '') + '>' + (rl || '\u2014') + '</option>';
            }

            var bevTypes = ['espresso', 'pourover', 'tea', 'other'];
            var bevOptions = '';
            for (var k = 0; k < bevTypes.length; k++) {
                var bt = bevTypes[k];
                bevOptions += '<option value="' + bt + '"' + (bt === shotData.beverageType ? ' selected' : '') + '>' + bt.charAt(0).toUpperCase() + bt.slice(1) + '</option>';
            }

            // Build edit form for info grid
            // All values come from shotData which is server-escaped in the C++ template
            infoGrid.innerHTML =
                '<div class="info-card notes-card-edit"><h3>Notes</h3>' +
                    '<textarea class="edit-textarea" id="editNotes">' + escapeHtml(shotData.espressoNotes) + '</textarea>' +
                '</div>' +
                '<div class="info-card"><h3>Beans</h3>' +
                    '<div class="edit-row"><span class="label">Brand</span><div class="edit-field"><input type="text" class="edit-input" id="editBrand" value="' + escapeHtml(shotData.beanBrand) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">Type</span><div class="edit-field"><input type="text" class="edit-input" id="editType" value="' + escapeHtml(shotData.beanType) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">Roast Date</span><div class="edit-field"><input type="text" class="edit-input" id="editRoastDate" value="' + escapeHtml(shotData.roastDate) + '" placeholder="YYYY-MM-DD"></div></div>' +
                    '<div class="edit-row"><span class="label">Roast Level</span><div class="edit-field"><select class="edit-select" id="editRoastLevel">' + roastOptions + '</select></div></div>' +
                '</div>' +
                '<div class="info-card"><h3>Grinder</h3>' +
                    '<div class="edit-row"><span class="label">Brand</span><div class="edit-field"><input type="text" class="edit-input" id="editGrinderBrand" value="' + escapeHtml(shotData.grinderBrand) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">Model</span><div class="edit-field"><input type="text" class="edit-input" id="editGrinderModel" value="' + escapeHtml(shotData.grinderModel) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">Burrs</span><div class="edit-field"><input type="text" class="edit-input" id="editGrinderBurrs" value="' + escapeHtml(shotData.grinderBurrs) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">Setting</span><div class="edit-field"><input type="text" class="edit-input" id="editGrinderSetting" value="' + escapeHtml(shotData.grinderSetting) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">RPM</span><div class="edit-field"><input type="number" class="edit-input" id="editRpm" step="1" min="0" value="' + (shotData.rpm > 0 ? shotData.rpm : '') + '"></div></div>' +
                '</div>' +
                '<div class="info-card"><h3>Additional</h3>' +
                    '<div class="edit-row"><span class="label">Barista</span><div class="edit-field"><input type="text" class="edit-input" id="editBarista" value="' + escapeHtml(shotData.barista) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">Beverage</span><div class="edit-field"><select class="edit-select" id="editBeverageType">' + bevOptions + '</select></div></div>' +
                    '<div class="edit-row"><span class="label">TDS</span><div class="edit-field"><input type="number" class="edit-input" id="editTds" step="0.01" value="' + (shotData.drinkTds || '') + '" oninput="autoCalcEY()"></div></div>' +
                    '<div class="edit-row"><span class="label">EY (%)</span><div class="edit-field"><input type="number" class="edit-input" id="editEy" step="0.1" value="' + (shotData.drinkEy || '') + '" readonly style="opacity:0.7"></div></div>' +
                '</div>';

            // Stepped candidates for the SHOT's own grinder — not the active
            // one (grind-value-entry). Free text stays accepted either way.
            attachGrindDatalist(document.getElementById('editGrinderSetting'),
                                document.getElementById('editRpm'),
                                shotData.grinderBrand, shotData.grinderModel);

            actionsBar.style.display = 'none';
            originalDebugDisplay = debugContainer ? debugContainer.style.display : '';
            if (debugContainer) debugContainer.style.display = 'none';
            editBar.classList.add('visible');
            editBtn.style.display = 'none';
            document.querySelector('.container').style.paddingBottom = '5rem';
            editStartData = collectEdits();
        }

        // The form as edit mode opened it: a save sends only what changed from
        // it, so a value Visualizer changed meanwhile is not written back over.
        var editStartData = {};

        function cancelEdit() {
            if (!isEditMode) return;
            isEditMode = false;

            document.querySelector('.metrics-bar').innerHTML = originalMetricsHTML;
            document.querySelector('.info-grid').innerHTML = originalInfoGridHTML;
            document.querySelector('.actions-bar').style.display = originalActionsDisplay;
            document.getElementById('editBar').classList.remove('visible');
            document.getElementById('editBtn').style.display = '';
            document.querySelector('.container').style.paddingBottom = '';
            var debugContainer = document.getElementById('debugLogContainer');
            if (debugContainer) debugContainer.style.display = originalDebugDisplay;
        }

        function autoCalcEY() {
            var dose = parseFloat(document.getElementById('editDose').value) || 0;
            var yieldVal = parseFloat(document.getElementById('editYield').value) || 0;
            var tds = parseFloat(document.getElementById('editTds').value) || 0;
            var eyField = document.getElementById('editEy');
            if (dose > 0 && yieldVal > 0 && tds > 0) {
                eyField.value = ((yieldVal * tds) / dose).toFixed(1);
            }
        }

        function collectEdits() {
            var ratingValue = parseInt(document.getElementById('editRating').value) || 0;
            ratingValue = Math.max(0, Math.min(100, ratingValue));

            return {
                beanBrand: document.getElementById('editBrand').value,
                beanType: document.getElementById('editType').value,
                roastDate: document.getElementById('editRoastDate').value,
                roastLevel: document.getElementById('editRoastLevel').value,
                grinderBrand: document.getElementById('editGrinderBrand').value,
                grinderModel: document.getElementById('editGrinderModel').value,
                grinderBurrs: document.getElementById('editGrinderBurrs').value,
                grinderSetting: document.getElementById('editGrinderSetting').value,
                rpm: parseInt(document.getElementById('editRpm').value) || 0,
                espressoNotes: document.getElementById('editNotes').value,
                doseWeight: parseFloat(document.getElementById('editDose').value) || 0,
                finalWeight: parseFloat(document.getElementById('editYield').value) || 0,
                enjoyment: ratingValue,
                barista: document.getElementById('editBarista').value,
                beverageType: document.getElementById('editBeverageType').value,
                drinkTds: parseFloat(document.getElementById('editTds').value) || 0,
                drinkEy: parseFloat(document.getElementById('editEy').value) || 0
            };
        }

        function saveChanges() {
            var edits = collectEdits();
            var data = {};
            Object.keys(edits).forEach(function(key) {
                if (edits[key] !== editStartData[key]) data[key] = edits[key];
            });
            if (Object.keys(data).length === 0) {
                cancelEdit();
                return;
            }

            var btn = document.querySelector('.save-btn');
            btn.textContent = 'Saving...';
            btn.disabled = true;

            fetch('/api/shot/' + shotData.id + '/metadata', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            }).then(function(r) {
                if (!r.ok) throw new Error('Server error (' + r.status + ')');
                return r.json();
            }).then(function(result) {
                if (result.success) {
                    window.location.reload();
                } else {
                    alert('Save failed: ' + (result.error || 'Unknown error'));
                    btn.textContent = 'Save';
                    btn.disabled = false;
                }
            }).catch(function(err) {
                alert('Save failed: ' + err);
                btn.textContent = 'Save';
                btn.disabled = false;
            });
        }

        )HTML" + QString::fromLatin1(WEB_JS_ESCAPE_HTML) + R"HTML(
    </script>
)HTML" R"HTML(
    <script>
        const pressureData = %15;
        const flowData = %16;
        const weightData = %17;
        const tempData = %18;
        const pressureGoalData = %19;
        const flowGoalData = %20;
        const phaseData = %22;
        const weightFlowRateData = %23;
        const resistanceData = %39;

        // Chart.js plugin: draw vertical phase marker lines and labels
        const phaseMarkerPlugin = {
            id: 'phaseMarkers',
            afterDraw: function(chart) {
                if (!phaseData || phaseData.length === 0) return;
                const ctx = chart.ctx;
                const xScale = chart.scales.x;
                const yScale = chart.scales.y;
                const top = yScale.top;
                const bottom = yScale.bottom;

                ctx.save();
                for (var i = 0; i < phaseData.length; i++) {
                    var marker = phaseData[i];
                    var x = xScale.getPixelForValue(marker.time);
                    if (x < xScale.left || x > xScale.right) continue;

                    // Draw vertical dotted line
                    ctx.beginPath();
                    ctx.setLineDash([3, 3]);
                    ctx.strokeStyle = marker.label === 'End' ? '#FF6B6B' : 'rgba(255,255,255,0.4)';
                    ctx.lineWidth = 1;
                    ctx.moveTo(x, top);
                    ctx.lineTo(x, bottom);
                    ctx.stroke();
                    ctx.setLineDash([]);

                    // Draw label
                    var suffix = '';
                    if (marker.reason === 'weight') suffix = ' [W]';
                    else if (marker.reason === 'pressure' || marker.reason === 'pressure_unconfirmed') suffix = ' [P]';
                    else if (marker.reason === 'flow' || marker.reason === 'flow_unconfirmed') suffix = ' [F]';
                    else if (marker.reason === 'time') suffix = ' [T]';
                    var text = marker.label + suffix;

                    ctx.save();
                    ctx.translate(x + 4, top + 10);
                    ctx.rotate(-Math.PI / 2);
                    ctx.font = (marker.label === 'End' ? 'bold ' : '') + '11px sans-serif';
                    ctx.fillStyle = marker.label === 'End' ? '#FF6B6B' : 'rgba(255,255,255,0.8)';
                    ctx.textAlign = 'right';
                    ctx.fillText(text, 0, 0);
                    ctx.restore();
                }
                ctx.restore();
            }
        };

        // Track mouse position for tooltip
        var mouseX = 0, mouseY = 0;
        document.addEventListener("mousemove", function(e) {
            mouseX = e.pageX;
            mouseY = e.pageY;
        });

        // Find closest data point to a given x value
        function findClosestPoint(data, targetX) {
            if (!data || data.length === 0) return null;
            var closest = data[0];
            var closestDist = Math.abs(data[0].x - targetX);
            for (var i = 1; i < data.length; i++) {
                var dist = Math.abs(data[i].x - targetX);
                if (dist < closestDist) {
                    closestDist = dist;
                    closest = data[i];
                }
            }
            return closest;
        }

        // External tooltip showing all curves
        function externalTooltip(context) {
            var tooltipEl = document.getElementById("chartTooltip");
            if (!tooltipEl) {
                tooltipEl = document.createElement("div");
                tooltipEl.id = "chartTooltip";
                tooltipEl.style.cssText = "position:absolute;background:#161b22;border:1px solid #30363d;border-radius:8px;padding:10px 14px;pointer-events:none;font-size:13px;color:#e6edf3;z-index:100;";
                document.body.appendChild(tooltipEl);
            }

            var tooltip = context.tooltip;
            if (tooltip.opacity === 0) {
                tooltipEl.style.opacity = 0;
                return;
            }

            if (!tooltip.dataPoints || !tooltip.dataPoints.length) {
                tooltipEl.style.opacity = 0;
                return;
            }

            var targetX = tooltip.dataPoints[0].parsed.x;
            var datasets = context.chart.data.datasets;
            var lines = [];)HTML" R"HTML(

            for (var i = 0; i < datasets.length; i++) {
                var ds = datasets[i];
                var meta = context.chart.getDatasetMeta(i);
                if (meta.hidden) continue;

                var pt = findClosestPoint(ds.data, targetX);
                if (!pt || pt.y === null) continue;

                var unit = "";
                if (ds.label.includes("Pressure")) unit = " bar";
                else if (ds.label.includes("Flow")) unit = " ml/s";
                else if (ds.label.includes("Yield")) unit = " g";
                else if (ds.label.includes("Temp")) unit = " °C";

                lines.push('<div style="display:flex;align-items:center;gap:6px;"><span style="display:inline-block;width:12px;height:12px;background:' + ds.borderColor + ';border-radius:2px;"></span>' + ds.label + ': ' + pt.y.toFixed(1) + unit + '</div>');
            }

            tooltipEl.innerHTML = '<div style="font-weight:600;margin-bottom:6px;">' + targetX.toFixed(1) + 's</div>' + lines.join('');
            tooltipEl.style.opacity = 1;
            tooltipEl.style.left = (mouseX + 15) + "px";
            tooltipEl.style.top = (mouseY - 10) + "px";
        }

        const ctx = document.getElementById('shotChart').getContext('2d');
        const chart = new Chart(ctx, {
            type: 'line',
            plugins: [phaseMarkerPlugin],
            data: {
                datasets: [
                    {
                        label: 'Pressure',
                        data: pressureData,
                        borderColor: '#18c37e',
                        backgroundColor: 'rgba(24, 195, 126, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Flow',
                        data: flowData,
                        borderColor: '#4e85f4',
                        backgroundColor: 'rgba(78, 133, 244, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Yield',
                        data: weightData,
                        borderColor: '#a2693d',
                        backgroundColor: 'rgba(162, 105, 61, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y2'
                    },
                    {
                        label: 'Temp',
                        data: tempData,
                        borderColor: '#e73249',
                        backgroundColor: 'rgba(231, 50, 73, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y3'
                    },
                    {
                        label: 'Pressure Goal',
                        data: pressureGoalData,
                        borderColor: '#69fdb3',
                        borderWidth: 1,
                        borderDash: [5, 5],
                        pointRadius: 0,
                        tension: 0.1,
                        yAxisID: 'y',
                        spanGaps: false
                    },
                    {
                        label: 'Flow Goal',
                        data: flowGoalData,
                        borderColor: '#7aaaff',
                        borderWidth: 1,
                        borderDash: [5, 5],
                        pointRadius: 0,
                        tension: 0.1,
                        yAxisID: 'y',
                        spanGaps: false
                    },
                    {
                        label: 'Weight Flow',
                        data: weightFlowRateData,
                        borderColor: '#d4a574',
                        backgroundColor: 'rgba(212, 165, 116, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Resistance',
                        data: resistanceData,
                        borderColor: '#eae83d',
                        backgroundColor: 'rgba(234, 232, 61, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y',
                        hidden: true
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: {
                    mode: 'nearest',
                    axis: 'x',
                    intersect: false
                },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        enabled: false,
                        external: externalTooltip
                    }
                },
                scales: {
                    x: {
                        type: 'linear',
                        title: { display: true, text: 'Time (s)', color: '#8b949e' },
                        grid: { color: 'rgba(48, 54, 61, 0.5)' },
                        ticks: { color: '#8b949e' }
                    },
                    y: {
                        type: 'linear',
                        position: 'left',
                        title: { display: true, text: 'Pressure / Flow', color: '#8b949e' },
                        min: 0,
                        max: 12,
                        grid: { color: 'rgba(48, 54, 61, 0.5)' },
                        ticks: { color: '#8b949e' }
                    },)HTML" R"HTML(
                    y2: {
                        type: 'linear',
                        position: 'right',
                        title: { display: true, text: 'Yield (g)', color: '#a2693d' },
                        min: 0,
                        grid: { display: false },
                        ticks: { color: '#a2693d' }
                    },
                    y3: {
                        type: 'linear',
                        position: 'right',
                        title: { display: false },
                        min: 80,
                        max: 100,
                        display: false
                    }
                }
            }
        });

        function toggleDataset(index, btn) {
            const meta = chart.getDatasetMeta(index);
            meta.hidden = !meta.hidden;
            btn.classList.toggle('active');

            // Also toggle goal lines for pressure/flow
            if (index === 0) chart.getDatasetMeta(4).hidden = meta.hidden;
            if (index === 1) chart.getDatasetMeta(5).hidden = meta.hidden;

            chart.update();
        }

        function toggleMenu() {
            var menu = document.getElementById("menuDropdown");
            menu.classList.toggle("open");
        }

        function openSummaryDialog() {
            document.getElementById("summaryModal").classList.add("open");
        }
        function closeSummaryDialog() {
            document.getElementById("summaryModal").classList.remove("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            var btn = e.target.closest(".menu-btn");
            if (!btn && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
        });

        // Power toggle
)HTML");
    html += WEB_JS_POWER_CONTROL;
    html += WEB_JS_GRIND_DATALIST;
    html += R"HTML(
    </script>
</body>
</html>
)HTML";
    // Recipe identity, injected AFTER the .arg() chain: a recipe name is user
    // text and may contain '%', which would shadow a numbered placeholder. Same
    // reason the list card uses markers. Requires the name to resolve, not just
    // the id — a dangling id must fall back to showing nothing rather than an
    // empty line with an icon beside it.
    QString detailRecipeHtml;
    if (shot.recipeId > 0 && !shot.recipeName.isEmpty()) {
        detailRecipeHtml =
            QStringLiteral("<div class=\"subtitle shot-detail-recipe")
            + (shot.recipeArchived ? QStringLiteral(" archived\" title=\"Archived recipe")
                                   : QString())
            + QStringLiteral("\"><span class=\"shot-drink-icon\">")
            + drinkTypeEmoji(shot.recipeDrinkType)
            + QStringLiteral("</span>")
            + shot.recipeName.toHtmlEscaped().replace(QStringLiteral("__"),
                                                      QStringLiteral("&#95;&#95;"))
            + QStringLiteral("</div>");
    }

    QString rendered = html
    .arg(tempOverride > 0
         ? shot.profileName.toHtmlEscaped() + QString(" (%1\u00B0C)").arg(tempOverride, 0, 'f', 0)
         : shot.profileName.toHtmlEscaped())
    .arg(shot.dateTime)
    .arg(shot.doseWeightG, 0, 'f', 1)
    .arg(yieldDisplay)
    .arg(ratio, 0, 'f', 1)
    .arg(shot.durationSec, 0, 'f', 1)
    .arg(ratingText)
    .arg(shot.beanBrand.isEmpty() ? "-" : shot.beanBrand.toHtmlEscaped())
    .arg(shot.beanType.isEmpty() ? "-" : shot.beanType.toHtmlEscaped())
    .arg(shot.roastDate.isEmpty() ? "-" : shot.roastDate.toHtmlEscaped())
    .arg(shot.roastLevel.isEmpty() ? "-" : shot.roastLevel.toHtmlEscaped())
    .arg(shot.grinderBrand.isEmpty() && shot.grinderModel.isEmpty()
         ? "-" : (shot.grinderBrand + " " + shot.grinderModel).trimmed().toHtmlEscaped())
    .arg(shot.grinderSetting.isEmpty() ? "-" : shot.grinderSetting.toHtmlEscaped())
    .arg(shot.espressoNotes.isEmpty() ? "No notes" : shot.espressoNotes.toHtmlEscaped())
    .arg(pressureData)
    .arg(flowData)
    .arg(weightData)
    .arg(tempData)
    .arg(pressureGoalData)
    .arg(flowGoalData)
    .arg(shot.debugLog.isEmpty() ? "No debug log available" : shot.debugLog.toHtmlEscaped())
    .arg(phaseData)
    .arg(weightFlowRateData)
    // shotData JS object fields (%24-%38)
    .arg(shotId)                                                                     // %24 id
    .arg(jsEscape(shot.beanBrand))                                                   // %25 beanBrand
    .arg(jsEscape(shot.beanType))                                                    // %26 beanType
    .arg(jsEscape(shot.roastDate))                                                   // %27 roastDate
    .arg(jsEscape(shot.roastLevel))                                                  // %28 roastLevel
    .arg(jsEscape(shot.grinderModel))                                                // %29 grinderModel
    .arg(jsEscape(shot.grinderSetting))                                              // %30 grinderSetting
    .arg(jsEscape(shot.espressoNotes))                                               // %31 espressoNotes
    .arg(shot.doseWeightG, 0, 'f', 1)                                                // %32 doseWeightG
    .arg(shot.finalWeightG, 0, 'f', 1)                                               // %33 finalWeightG
    .arg(shot.enjoyment0to100)                                                       // %34 enjoyment
    .arg(jsEscape(shot.barista))                                                     // %35 barista
    .arg(jsEscape(shot.beverageType.isEmpty()
                  ? QStringLiteral("espresso")
                  : shot.beverageType))                                              // %36 beverageType
    .arg(shot.drinkTdsPct, 0, 'f', 2)                                                // %37 drinkTds
    .arg(shot.drinkEyPct, 0, 'f', 1)                                                 // %38 drinkEy
    .arg(resistanceData)                                                             // %39 resistance
    .arg(jsEscape(shot.grinderBrand))                                                // %40 grinderBrand
    .arg(jsEscape(shot.grinderBurrs));                                               // %41 grinderBurrs

    // badgesHtml / summaryLinesHtml carry detector-generated text and CSS that
    // can contain literal `%`. Inject AFTER the .arg() chain via replace() so
    // they can never feed Qt's placeholder scanner. See the build-site comment
    // above for why doubling `%` doesn't actually escape.
    rendered.replace(QStringLiteral("__BADGES_HTML__"), badgesHtml);
    rendered.replace(QStringLiteral("__SUMMARY_LINES_HTML__"), summaryLinesHtml);
    rendered.replace(QStringLiteral("__DETAIL_RECIPE__"), detailRecipeHtml);
    return rendered;
}

// Runs on the worker thread that loaded the shots: the comparison parses every
// shot's profile once per base, which is no work for the thread carrying BLE.
QJsonObject ShotServer::comparisonPageData(const QList<ShotRecord>& shotsIn)
{
    // Oldest first, by when each shot was pulled: the default base, as in the app.
    QList<ShotRecord> shots = shotsIn;
    std::stable_sort(shots.begin(), shots.end(), [](const ShotRecord& a, const ShotRecord& b) {
        return a.summary.timestamp < b.summary.timestamp;
    });

    auto curve = [](const QVector<QPointF>& points) {
        QJsonArray out;
        for (const QPointF& p : points)
            out.append(QJsonArray{ std::round(p.x() * 100) / 100, std::round(p.y() * 100) / 100 });
        return out;
    };

    static const bool use12h = QLocale::system().timeFormat(QLocale::ShortFormat).contains("AP", Qt::CaseInsensitive);
    QJsonArray shotData;
    QList<ShotProjection> projections;
    for (const ShotRecord& r : std::as_const(shots)) {
        projections.append(ShotHistoryStorage::convertShotRecord(r));
        QJsonArray phases;
        for (const auto& ph : r.phases) {
            if (ph.label == QLatin1String("Start")) continue;
            phases.append(QJsonObject{ { "time", ph.time }, { "label", ph.label }, { "reason", ph.transitionReason } });
        }
        shotData.append(QJsonObject{
            { "id", r.summary.id },
            { "date", QDateTime::fromSecsSinceEpoch(r.summary.timestamp).toString(use12h ? "MMM d, h:mm AP" : "MMM d, HH:mm") },
            { "pourStartSec", r.cachedAnalysis ? r.cachedAnalysis->detectors.pourStartSec : 0.0 },
            { "phases", phases },
            { "curves", QJsonObject{
                { "pressure", curve(r.pressure) }, { "flow", curve(r.flow) },
                { "temp", curve(r.temperature) }, { "weight", curve(r.weight) },
                { "weightFlow", curve(r.weightFlowRate) }, { "resistance", curve(r.resistance) },
                { "darcyR", curve(r.darcyResistance) }, { "conductance", curve(r.conductance) },
                { "dCdt", curve(r.conductanceDerivative) }, { "mixTemp", curve(r.temperatureMix) },
                { "mixTempGoal", curve(r.temperatureMixGoal) },
            } },
        });
    }
    // The comparison against every possible base, so making a shot the base needs
    // no round trip. Same assembler as the app and MCP; at most ten shots.
    QJsonArray comparisons;
    for (qsizetype i = 0; i < projections.size(); ++i)
        comparisons.append(ShotComparison::compare(projections, i));
    return QJsonObject{ { QStringLiteral("shots"), shotData }, { QStringLiteral("comparisons"), comparisons } };
}

QString ShotServer::generateComparisonPage(const QJsonObject& data) const
{
    const QJsonArray shots = data.value(QStringLiteral("shots")).toArray();
    if (shots.size() < 2) {
        return QStringLiteral("<!DOCTYPE html><html><body>Not enough valid shots to compare</body></html>");
    }

    // JSON as a script literal. "<" only ever occurs inside a JSON string, where
    // \u003c means the same, so no note or name can close the <script> or open a
    // comment that swallows it.
    auto embed = [](const QJsonValue& v) {
        const QByteArray json = v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)
                                            : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
        return QString::fromUtf8(json).replace(QLatin1Char('<'), QStringLiteral("\\u003c"));
    };
    // The app's own icons, drawn in the text colour, so the two pages cannot drift.
    auto icon = [&](const QString& path) {
        QFile f(path);
        QString svg = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        svg.replace(QStringLiteral("\"white\""), QStringLiteral("\"currentColor\""));
        return embed(QJsonArray{ svg }).mid(1).chopped(1);   // the JSON string literal alone
    };

    QString html = QStringLiteral(R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Compare Shots - Decenza</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
    <style>)HTML");
    html += QString::fromLatin1(WEB_CSS_VARIABLES) + WEB_CSS_HEADER + WEB_CSS_MENU;
    html += QStringLiteral(R"HTML(
        :root { --surface2: #1c2129; --up: #ffaa00; --down: #4e85f4; --warn: #ffaa00; }
        .header { padding: 0.75rem 1.5rem; }
        .header-content { max-width: 1600px; gap: 1rem; }
        .menu-wrapper { margin-left: auto; }

        .layout { max-width: 1600px; margin: 0 auto; padding: 1rem; display: grid; gap: 1rem;
                  grid-template-columns: minmax(0, 1fr); }
        /* Side by side once both fit: the graph stays in view while the comparison scrolls. */
        @media (min-width: 1300px) {
            .layout { grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); align-items: start; }
            .graph-col { position: sticky; top: 4.5rem; }
        }
        .card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; padding: 1rem; }
        .chart-wrapper { position: relative; height: 360px; cursor: crosshair; }
        @media (min-width: 1300px) { .chart-wrapper { height: 52vh; } }

        .readout { margin-top: 0.5rem; overflow-x: auto; font-size: 0.8125rem; font-variant-numeric: tabular-nums; }
        .readout table { border-collapse: collapse; }
        .readout th, .readout td { padding: 0.15rem 0.6rem; text-align: right; white-space: nowrap; }
        .readout th:first-child, .readout td:first-child { text-align: left; }
        .readout .quiet { color: var(--text-secondary); opacity: 0.5; }

        .chips { display: flex; flex-wrap: wrap; gap: 0.4rem; margin-top: 0.75rem; align-items: center; }
        .chip { display: inline-flex; align-items: center; gap: 0.35rem; padding: 0.2rem 0.7rem; border-radius: 999px;
                border: 1px solid var(--border); background: transparent; color: var(--text-secondary);
                font-size: 0.8125rem; cursor: pointer; opacity: 0.6; }
        .chip.on { opacity: 1; color: var(--text); }
        .chip .dot { width: 7px; height: 7px; border-radius: 50%; }
        .chip-sep { width: 1px; height: 1.4rem; background: var(--border); margin: 0 0.2rem; }

        .swatch { display: inline-block; width: 22px; height: 2px; vertical-align: middle; background: var(--text); }
        .swatch.heavy { height: 3px; }
        .swatch.s1 { background: repeating-linear-gradient(90deg, var(--text) 0 4px, transparent 4px 6px); }
        .swatch.s2 { background: repeating-linear-gradient(90deg, var(--text) 0 7px, transparent 7px 9px, var(--text) 9px 11px, transparent 11px 13px); }

        .heads, .row { display: grid; gap: 0.5rem; grid-template-columns: 7rem repeat(var(--n), minmax(0, 1fr)); }
        .heads { margin-bottom: 0.75rem; }
        .head { container-type: inline-size; min-width: 0; }
        .head > div { display: flex; align-items: center; gap: 0.35rem; padding: 0.3rem 0.3rem 0.3rem 0.6rem; border: 1px solid var(--border);
                      border-radius: 999px; background: var(--surface2); cursor: pointer; font-size: 0.8125rem; }
        .head.base > div { border: 2px solid var(--down); cursor: default; }
        .head .date { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
        .head .tag { font-size: 0.6875rem; color: var(--down); background: rgba(78,133,244,0.18); padding: 0 0.4rem; border-radius: 999px; }
        /* Too narrow for the tag: the blue border still marks the base. */
        @container (max-width: 11.5rem) { .head .tag { display: none; } }
        /* Phone-narrow: the date takes two lines rather than an ellipsis. */
        @container (max-width: 8rem) {
            .head > div { gap: 0.25rem; padding-left: 0.4rem; border-radius: 12px; }
            .head .swatch { width: 14px; }
            .head .date { white-space: normal; line-height: 1.2; font-size: 0.75rem; }
        }
        .head .eye { display: flex; background: none; border: none; color: var(--text); cursor: pointer; padding: 0.1rem; }
        .head .eye.off { color: var(--text-secondary); }
        .head .eye svg { width: 16px; height: 16px; }

        .summary { display: flex; gap: 0.6rem; align-items: baseline; font-size: 0.9rem; margin: 0.2rem 0 0.2rem 0.2rem; }
        .section { font-size: 0.75rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--text-secondary); margin: 1.1rem 0 0.3rem; }
        .row { align-items: baseline; padding: 0.35rem 0; border-top: 1px solid rgba(48,54,61,0.6); font-variant-numeric: tabular-nums; }
        .row .label { color: var(--text-secondary); font-size: 0.875rem; }
        .row .label .unit { font-size: 0.75rem; opacity: 0.7; margin-left: 0.2rem; }
        .row .muted { color: var(--text-secondary); }
        .row .warn { color: var(--warn); }
        .pill { font-size: 0.75rem; padding: 0 0.45rem; border-radius: 999px; margin-left: 0.4rem; white-space: nowrap; }
        .pill.up { color: var(--up); background: rgba(255,170,0,0.16); }
        .pill.down { color: var(--down); background: rgba(78,133,244,0.16); }
        .note { color: var(--text-secondary); font-size: 0.8125rem; line-height: 1.6; }
        .quote { color: var(--text-secondary); font-style: italic; font-size: 0.875rem; margin-top: 0.5rem; }
        .more-btn { display: block; margin: 0.75rem auto 0; padding: 0.4rem 1rem; border-radius: 8px; border: 1px solid var(--border);
                    background: var(--surface2); color: var(--text); cursor: pointer; }
        .diff { font-size: 0.8125rem; color: var(--text-secondary); margin: 0.3rem 0; }
        /* Phone: each label takes its own line, so the value columns get the width. */
        @media (max-width: 600px) {
            .header { padding: 0.75rem 1rem; }
            .chip-sep { display: none; }
            .layout { padding: 0.5rem; }
            .heads, .row { grid-template-columns: repeat(var(--n), minmax(0, 1fr)); }
            .heads > .spacer { display: none; }
            .row .label { grid-column: 1 / -1; }
        }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Compare )HTML") + QString::number(shots.size()) + QStringLiteral(R"HTML( Shots</h1>
)HTML");
    html += generateMenuHtml();
    html += QStringLiteral(R"HTML(
        </div>
    </header>
    <main class="layout">
        <section class="graph-col">
            <div class="card">
                <div class="chart-wrapper"><canvas id="compareChart"></canvas></div>
                <div id="readout" class="readout"></div>
                <div id="chips" class="chips"></div>
            </div>
        </section>
        <section class="card" id="comparison"></section>
    </main>
    <script>
)HTML");
    html += QStringLiteral("        var shots = ") + embed(shots)
          + QStringLiteral(";\n        var comparisons = ") + embed(data.value(QStringLiteral("comparisons")))
          + QStringLiteral(";\n        var texts = ") + embed(ShotComparisonText::toJson())
          + QStringLiteral(";\n        var dialInLabels = ") + embed(QJsonObject::fromVariantMap(ProfileDialInText::labelMap()))
          + QStringLiteral(";\n        var puckFlags = ") + embed(QJsonArray::fromVariantList(EquipmentStorage::puckPrepFlags()))
          + QStringLiteral(";\n        var icons = { eye: ") + icon(QStringLiteral(":/icons/eye.svg"))
          + QStringLiteral(", eyeOff: ") + icon(QStringLiteral(":/icons/eye-off.svg")) + QStringLiteral(" };\n");
    html += QString::fromLatin1(WEB_JS_ESCAPE_HTML);
    html += QString::fromLatin1(WEB_JS_MENU);
    html += QString::fromLatin1(WEB_JS_POWER_CONTROL);
    html += QStringLiteral(R"HTML(
        // === Wording: ShotComparisonText, the table the app translates ===
        function txt(id, fb) { var e = texts[id]; return e ? e.label : (fb !== undefined ? fb : id); }
        var DASH = "—";

        // === State ===
        var base = 0;                 // index into shots (oldest first)
        var hiddenShots = {};          // by shot id
        var hiddenPhases = {};
        var alignPours = false;
        var showMore = false;
        var showAllCurves = false;
        var crosshair = null;
        var curves = [
            { key: "pressure", label: "P", name: "Pressure", color: "#18c37e", axis: "y", on: true, tip: "Pressure at the group (bar)" },
            { key: "flow", label: "F", name: "Flow", color: "#4e85f4", axis: "y", on: true, tip: "Flow through the puck (mL/s)" },
            { key: "temp", label: "T", name: "Temp", color: "#e73249", axis: "y3", on: true, tip: "Group temperature (°C)" },
            { key: "weight", label: "W", name: "Weight", color: "#a2693d", axis: "y2", on: true, tip: "Weight in the cup (g)" },
            { key: "weightFlow", label: "WF", name: "Weight flow", color: "#d4a574", axis: "y", on: true, tip: "Weight flow into the cup (g/s)" },
            { key: "resistance", label: "R", name: "Resistance", color: "#eae83d", axis: "y", on: false, tip: "Puck resistance (pressure / flow)" },
            { key: "darcyR", label: "dR", name: "Resistance (P/F²)", color: "#f0a500", axis: "y", on: false, tip: "Darcy resistance (pressure / flow²)" },
            { key: "conductance", label: "C", name: "Conductance", color: "#00c8d7", axis: "y", on: false, tip: "Conductance (flow² / pressure)" },
            { key: "dCdt", label: "dC/dt", name: "dC/dt", color: "#e05aa0", axis: "y4", on: false, tip: "Rate of change of conductance; spikes reveal transient channels" },
            { key: "mixTemp", label: "Tmix", name: "Mix temp", color: "#d79be0", axis: "y3", on: false, tip: "Water mix temperature" },
            { key: "mixTempGoal", label: "Tmixg", name: "Mix temp goal", color: "#a983c9", axis: "y3", on: false, tip: "Water mix temperature goal" }
        ];
        var phaseColors = ["#FFD600", "#E91E63", "#00E5FF", "#76FF03", "#FF6D00"];
        var dashes = [[], [6, 5], [10, 4, 2, 4]];

        var indexById = {};
        shots.forEach(function(s, i) { indexById[s.id] = i; });
        var phaseLabels = [];
        shots.forEach(function(s) { s.phases.forEach(function(p) { if (phaseLabels.indexOf(p.label) < 0) phaseLabels.push(p.label); }); });
        for (var pl = 0; pl < phaseLabels.length - 2; pl++) hiddenPhases[phaseLabels[pl]] = true;

        function cmp() { return comparisons[base]; }
        // Shot indices in column order: the base, then the rest oldest first.
        function columns() { return cmp().shots.map(function(s) { return indexById[s.shotId]; }); }
        function offsetFor(si) {
            if (!alignPours) return 0;
            var b = shots[base].pourStartSec, o = shots[si].pourStartSec;
            return b > 0 && o > 0 ? b - o : 0;
        }
        function swatch(col) { return "<span class='swatch s" + (col % 3) + (col === 0 ? " heavy" : "") + "'></span>"; }
)HTML");
    html += QStringLiteral(R"HTML(
        // === Chart ===
        var phasePlugin = {
            id: "phases",
            afterDraw: function(chart) {
                var ctx = chart.ctx, xs = chart.scales.x, ys = chart.scales.y;
                ctx.save();
                columns().forEach(function(si, col) {
                    if (hiddenShots[shots[si].id]) return;
                    shots[si].phases.forEach(function(p) {
                        if (hiddenPhases[p.label]) return;
                        var x = xs.getPixelForValue(p.time + offsetFor(si));
                        if (x < xs.left || x > xs.right) return;
                        var color = phaseColors[phaseLabels.indexOf(p.label) % phaseColors.length];
                        ctx.setLineDash(dashes[col % 3]); ctx.strokeStyle = color; ctx.globalAlpha = 0.7; ctx.lineWidth = 1.5;
                        ctx.beginPath(); ctx.moveTo(x, ys.top); ctx.lineTo(x, ys.bottom); ctx.stroke();
                        if (col === 0) {
                            ctx.setLineDash([]); ctx.globalAlpha = 0.9; ctx.fillStyle = color; ctx.font = "11px sans-serif";
                            ctx.fillText(p.label, x + 3, ys.top + 11);
                        }
                    });
                });
                if (crosshair !== null) {
                    var cx = xs.getPixelForValue(crosshair);
                    ctx.setLineDash([3, 3]); ctx.strokeStyle = "rgba(255,255,255,0.7)"; ctx.globalAlpha = 1; ctx.lineWidth = 1;
                    ctx.beginPath(); ctx.moveTo(cx, ys.top); ctx.lineTo(cx, ys.bottom); ctx.stroke();
                }
                ctx.restore();
            }
        };
        function datasets() {
            var out = [];
            columns().forEach(function(si, col) {
                if (hiddenShots[shots[si].id]) return;
                var dx = offsetFor(si);
                curves.forEach(function(c) {
                    if (!c.on) return;
                    var pts = (shots[si].curves[c.key] || []).map(function(p) { return { x: p[0] + dx, y: p[1] }; });
                    out.push({ data: pts, borderColor: c.color, borderWidth: (col === 0 ? 2.5 : 1.5), pointRadius: 0,
                               tension: 0.2, yAxisID: c.axis, borderDash: dashes[col % 3] });
                });
            });
            return out;
        }
        var chart = new Chart(document.getElementById("compareChart").getContext("2d"), {
            type: "line",
            plugins: [phasePlugin],
            data: { datasets: datasets() },
            options: {
                responsive: true, maintainAspectRatio: false, animation: false, events: [],
                plugins: { legend: { display: false }, tooltip: { enabled: false } },
                scales: {
                    x: { type: "linear", min: 0, title: { display: true, text: "Time (s)", color: "#8b949e" },
                         grid: { color: "rgba(48,54,61,0.5)" }, ticks: { color: "#8b949e" } },
                    y: { min: 0, max: 12, title: { display: true, text: "bar / mL/s", color: "#8b949e" },
                         grid: { color: "rgba(48,54,61,0.5)" }, ticks: { color: "#8b949e" } },
                    y2: { position: "right", min: 0, title: { display: true, text: "g", color: "#a2693d" },
                          grid: { display: false }, ticks: { color: "#a2693d" } },
                    y3: { display: false, min: 40, max: 100 },
                    y4: { display: false }
                }
            }
        });
        function redrawChart() { chart.data.datasets = datasets(); chart.update("none"); renderReadout(); }

        // === Crosshair, read under the plot ===
        var cvs = chart.canvas, dragging = false, touchDir = null, tsx = 0, tsy = 0;
        function timeAt(clientX) {
            var r = cvs.getBoundingClientRect(), xs = chart.scales.x;
            return Math.max(xs.min, Math.min(xs.max, xs.getValueForPixel(clientX - r.left)));
        }
        function inspect(t) { crosshair = t; chart.update("none"); renderReadout(); }
        cvs.addEventListener("mousedown", function(e) { dragging = true; inspect(timeAt(e.clientX)); });
        document.addEventListener("mousemove", function(e) { if (dragging) inspect(timeAt(e.clientX)); });
        document.addEventListener("mouseup", function() { dragging = false; });
        cvs.addEventListener("touchstart", function(e) { touchDir = null; tsx = e.touches[0].clientX; tsy = e.touches[0].clientY; }, { passive: true });
        cvs.addEventListener("touchmove", function(e) {
            var dx = e.touches[0].clientX - tsx, dy = e.touches[0].clientY - tsy;
            if (!touchDir && Math.hypot(dx, dy) > 10) touchDir = Math.abs(dx) > Math.abs(dy) ? "h" : "v";
            if (touchDir === "h") { e.preventDefault(); inspect(timeAt(e.touches[0].clientX)); }
        }, { passive: false });
        cvs.addEventListener("touchend", function(e) { if (!touchDir) inspect(timeAt(e.changedTouches[0].clientX)); touchDir = null; });

        function valueAt(si, key, t) {
            var pts = shots[si].curves[key] || [], best = null, bd = 1;
            var tt = t - offsetFor(si);
            for (var i = 0; i < pts.length; i++) { var d = Math.abs(pts[i][0] - tt); if (d < bd) { bd = d; best = pts[i][1]; } }
            return best;
        }
        // One row per visible shot whether or not anything is inspected, so a tap never moves the page.
        function renderReadout() {
            var on = curves.filter(function(c) { return c.on; });
            var h = "<table><tr><th>" + (crosshair !== null ? crosshair.toFixed(1) + " s" : "") + "</th>";
            on.forEach(function(c) { h += "<th style='color:" + c.color + "'>" + c.label + "</th>"; });
            h += "</tr>";
            columns().forEach(function(si, col) {
                if (hiddenShots[shots[si].id]) return;
                h += "<tr><td>" + swatch(col) + "</td>";
                on.forEach(function(c) {
                    var v = crosshair !== null ? valueAt(si, c.key, crosshair) : null;
                    h += v === null ? "<td class='quiet'>–</td>" : "<td>" + v.toFixed(1) + "</td>";
                });
                h += "</tr>";
            });
            document.getElementById("readout").innerHTML = h + "</table>";
        }

        // === Chips: curves, phases, align pours ===
        function renderChips() {
            var h = "", off = 0;
            curves.forEach(function(c, i) {
                if (!c.on) off++;
                if (!c.on && !showAllCurves) return;
                h += "<button class='chip" + (c.on ? " on" : "") + "' title='" + escapeHtml(c.name + ": " + c.tip) + "' onclick='toggleCurve(" + i + ")'"
                   + (c.on ? " style='border-color:" + c.color + "'" : "") + "><span class='dot' style='background:" + c.color + "'></span>" + c.label + "</button>";
            });
            if (off > 0) h += "<button class='chip' title='" + escapeHtml(txt(showAllCurves ? "tip.fewerCurves" : "tip.moreCurves"))
                            + "' onclick='showAllCurves=!showAllCurves;renderChips()'>" + (showAllCurves ? escapeHtml(txt("ui.fewerCurves")) : "+" + off) + "</button>";
            if (phaseLabels.length > 0) h += "<span class='chip-sep'></span>";
            phaseLabels.forEach(function(p, i) {
                var color = phaseColors[i % phaseColors.length], on = !hiddenPhases[p];
                h += "<button class='chip" + (on ? " on" : "") + "' title='" + escapeHtml(txt("tip.phase").replace("%1", p)) + "' onclick='togglePhase(" + i + ")'"
                   + (on ? " style='border-color:" + color + "'" : "") + "><span class='dot' style='background:" + color + "'></span>" + escapeHtml(p) + "</button>";
            });
            h += "<button class='chip" + (alignPours ? " on" : "") + "' title='" + escapeHtml(txt("tip.alignPours"))
               + "' onclick='alignPours=!alignPours;renderChips();redrawChart()'>" + escapeHtml(txt("ui.alignPours")) + "</button>";
            document.getElementById("chips").innerHTML = h;
        }
        function toggleCurve(i) { curves[i].on = !curves[i].on; renderChips(); redrawChart(); }
        function togglePhase(i) { var p = phaseLabels[i]; if (hiddenPhases[p]) delete hiddenPhases[p]; else hiddenPhases[p] = true; renderChips(); chart.update("none"); }
)HTML");
    html += QStringLiteral(R"HTML(
        // === Formatting (the app's ComparisonShotTable rules, in English) ===
        function unitLabel(u) {
            return { s: "s", g: "g", bar: "bar", mlPerSec: "mL/s", gPerSec: txt("unit.gPerSec"), celsius: "°C",
                     celsiusDelta: "°C", percent: "%" }[u] || "";
        }
        function signed(v, d) { return (v > 0 ? "+" : v < 0 ? "−" : "") + Math.abs(v).toFixed(d); }
        function metricText(row, v) {
            if (v === null || v === undefined) return DASH;
            var s = v.toFixed(row.decimals);
            return row.key === "ratio" ? "1:" + s : s;
        }
        function puckLabels(canon) {
            var flags = (canon || "").split(",");
            return puckFlags.filter(function(f) { return flags.indexOf(f.key) >= 0; }).map(function(f) { return f.label; });
        }
        function inputText(row, cell) {
            var has = cell.value !== null && cell.value !== undefined;
            if (row.key === "temperatureOverrideC" && !has) return txt("input.profileTemp");
            if (row.key === "puckPrep") return puckLabels(cell.text).join(" · ") || DASH;
            if (!has || row.unit === "") return cell.text || DASH;
            if (row.unit === "g") return cell.value.toFixed(1) + " g";
            if (row.unit === "rpm") return Math.round(cell.value) + " " + txt("unit.rpm");
            if (row.unit === "celsius") return cell.value.toFixed(1) + " °C";
            return cell.text || String(cell.value);
        }
        function inputDelta(row, d) {
            if (d === null || d === undefined) return "";
            var t = signed(row.unit === "rpm" ? Math.round(d) : d, row.unit === "rpm" ? 0 : row.key === "grinderSetting" ? 2 : 1);
            return t.indexOf(".") >= 0 ? t.replace(/0+$/, "").replace(/\.$/, "") : t;
        }
        function pill(delta, text) {
            if (!text) return "";
            return "<span class='pill " + (delta > 0 ? "up" : "down") + "'>" + (delta > 0 ? "▲ " : "▼ ") + text.replace(/^[+−]/, "") + "</span>";
        }
        function plain(key, v) {
            if (typeof v === "number") return key === "rpm" ? String(Math.round(v)) : key === "temperatureOverrideC" ? v.toFixed(1) + " °C" : v.toFixed(1);
            return String(v);
        }
        function summaryFor(c) {
            var out = [];
            (c.summary || []).forEach(function(f) {
                if (f.kind === "sameSetup") out.push(txt("phrase.sameSetup"));
                else if (f.kind === "noNotable") out.push(txt("phrase.noNotable"));
                else if (f.kind === "input") out.push(txt("input." + f.key, f.key) + " " + plain(f.key, f.from) + " → " + plain(f.key, f.to));
                else if (f.kind === "inputChanged") out.push(txt("phrase.changed").replace("%1", txt("input." + f.key, f.key)));
                else if (f.kind === "moreInputs") out.push(txt("phrase.moreInputs").replace("%1", f.count));
                else if (f.kind === "metric") {
                    var amount = Math.abs(f.delta).toFixed(f.decimals) + (unitLabel(f.unit) ? " " + unitLabel(f.unit) : "");
                    out.push(txt(f.phrase).replace("%1", txt("metric." + f.key, f.key)).replace("%2", amount));
                } else if (f.kind === "stopped") out.push(txt("stopped." + f.stoppedBy, ""));
                else if (f.kind === "badgeAppeared") out.push(txt("phrase.badgeAppeared").replace("%1", txt("badge." + f.badge, f.badge)));
                else if (f.kind === "badgeGone") out.push(txt("phrase.badgeGone").replace("%1", txt("badge." + f.badge, f.badge)));
            });
            out = out.filter(function(t) { return t.length > 0; });
            return out.length > 0 ? out.join("  ·  ") : txt("phrase.noNotable");
        }
        // Profile-diff rows: ProfileDialInText's labels and the decimals C++ chose,
        // as ProfileDialInDiffBlock.qml shows them.
        function dialInLabel(id) { var e = dialInLabels[id]; return e ? e.label : id; }
        function diffRowText(r) {
            var name = dialInLabel(r.kind);
            if (r.frameIndex >= 0) name = (r.frameName || dialInLabel("step").replace("%1", r.frameIndex + 1)) + " · " + name;
            function val(v) {
                var u = { celsius: " °C", celsiusTank: " °C", bar: " bar", mlPerSec: " mL/s", g: " g", ml: " mL" }[r.unit];
                return v.toFixed(r.decimals) + (u !== undefined ? u : r.unit ? " " + r.unit : "");
            }
            return name + " " + (r.numeric ? val(r.oldValue) + " → " + val(r.newValue)
                                           : (r.oldText || DASH) + " → " + (r.newText || DASH));
        }
)HTML");
    html += QStringLiteral(R"HTML(
        // === The comparison ===
        function rowHtml(label, unit, cells) {
            var h = "<div class='row'><div class='label'>" + escapeHtml(label)
                  + (unit ? "<span class='unit'>" + escapeHtml(unit) + "</span>" : "") + "</div>";
            cells.forEach(function(c) { h += "<div" + (c.cls ? " class='" + c.cls + "'" : "") + ">" + c.html + "</div>"; });
            return h + "</div>";
        }
        function renderComparison() {
            var c = cmp(), cols = columns();
            document.getElementById("comparison").style.setProperty("--n", cols.length);
            var h = "<div class='heads'><div class='spacer'></div>";
            cols.forEach(function(si, col) {
                var s = shots[si], hidden = !!hiddenShots[s.id];
                h += "<div class='head" + (col === 0 ? " base" : "") + "'><div title='" + escapeHtml(txt(col === 0 ? "ui.baseShot" : "ui.makeBase").replace("%1", s.date)) + "'"
                   + (col === 0 ? "" : " onclick='setBase(" + si + ")'") + ">"
                   + swatch(col) + "<span class='date'>" + escapeHtml(s.date) + "</span>"
                   + (col === 0 ? "<span class='tag'>" + escapeHtml(txt("ui.base")) + "</span>" : "")
                   + "<button class='eye" + (hidden ? " off" : "") + "' title='" + escapeHtml(txt(hidden ? "ui.showOnGraph" : "ui.hideOnGraph"))
                   + "' aria-pressed='" + !hidden + "' onclick='event.stopPropagation();toggleShot(" + s.id + ")'>"
                   + (hidden ? icons.eyeOff : icons.eye) + "</button></div></div>";
            });
            h += "</div>";

            c.comparisons.forEach(function(cc, i) {
                h += "<div class='summary'>" + swatch(i + 1) + "<span>" + escapeHtml(summaryFor(cc)) + "</span></div>";
            });

            h += "<div class='section'>" + escapeHtml(txt("ui.changed")) + "</div>";
            (c.inputs || []).forEach(function(r) {
                h += rowHtml(txt("input." + r.key, r.key), "", r.cells.map(function(cell) {
                    return { cls: cell.state === "same" ? "muted" : "", html: escapeHtml(inputText(r, cell)) + pill(cell.delta, inputDelta(r, cell.delta)) };
                }));
            });
            if (c.comparisons.every(function(cc) { return cc.nothingChanged === true; })) h += "<div class='note'>" + escapeHtml(txt("ui.nothingChanged")) + "</div>";
            var same = (c.unchanged || []).map(function(it) {
                var v = it.text;
                if (it.value !== null && it.value !== undefined && it.unit !== "") v = inputText(it, it);
                if (it.key === "profile") {
                    if (c.comparisons.every(function(cc) { return cc.profile && cc.profile.sameVersion === true; })) v += " " + txt("ui.sameVersion");
                } else if (it.key === "puckPrep") v = txt("ui.prep").replace("%1", puckLabels(it.text).join(", "));
                else if (["grinder", "burrs", "basket", "bean", "roast"].indexOf(it.key) < 0 && it.unit !== "rpm") v = txt("input." + it.key, it.key) + " " + v;
                return v;
            });
            if (same.length > 0) h += "<div class='note'>" + escapeHtml(txt(cols.length > 2 ? "ui.sameForAll" : "ui.sameForBoth")) + "  ·  " + escapeHtml(same.join(" · ")) + "</div>";
            c.comparisons.forEach(function(cc, i) {
                var rows = cc.profile ? cc.profile.rows || [] : [];
                if (rows.length === 0) return;
                h += "<div class='diff'>" + escapeHtml(txt("ui.profileChanged").replace("%1", shots[cols[i + 1]].date) + ": "
                   + rows.map(diffRowText).join("; ")) + "</div>";
            });

            h += "<div class='section'>" + escapeHtml(txt("ui.happened")) + "</div>";
            var hidden = 0;
            (c.metrics || []).forEach(function(r) {
                if (r.more) hidden++;
                if (r.more && !showMore) return;
                h += rowHtml(txt("metric." + r.key, r.key), unitLabel(r.unit), r.cells.map(function(cell) {
                    var d = cell.delta;
                    return { html: escapeHtml(metricText(r, cell.value)) + (d !== null && d !== undefined ? pill(d, signed(d, r.decimals)) : "") };
                }));
            });
            var ss = c.shots;
            if (ss.some(function(s) { return s.stoppedBy !== ss[0].stoppedBy; }))
                h += rowHtml(txt("row.stopped"), "", ss.map(function(s) { return { html: escapeHtml(s.stoppedBy ? txt("stop." + s.stoppedBy, DASH) : DASH) }; }));
            var counts = {};
            ss.forEach(function(s) { (s.badges || []).forEach(function(b) { counts[b] = (counts[b] || 0) + 1; }); });
            Object.keys(counts).forEach(function(b) {
                if (counts[b] === ss.length) return;
                h += rowHtml(txt("badge." + b, b), "", ss.map(function(s) {
                    var on = (s.badges || []).indexOf(b) >= 0;
                    return { cls: on ? "warn" : "muted", html: escapeHtml(txt(on ? "ui.yes" : "ui.no")) };
                }));
            });
            if (ss.some(function(s) { return s.rating0to100 !== null || s.tasteBalance || s.tasteBody; }))
                h += rowHtml(txt("row.rating"), "", ss.map(function(s, col) {
                    var p = [];
                    if (s.rating0to100 !== null) p.push(s.rating0to100 + "%");
                    if (s.tasteBalance) p.push(txt("taste." + s.tasteBalance, s.tasteBalance));
                    if (s.tasteBody) p.push(txt("taste." + s.tasteBody, s.tasteBody));
                    // A Δ only between two rated shots.
                    var d = col > 0 && s.rating0to100 !== null && ss[0].rating0to100 !== null ? s.rating0to100 - ss[0].rating0to100 : 0;
                    return { html: escapeHtml(p.length ? p.join(" · ") : DASH) + (d !== 0 ? pill(d, signed(d, 0)) : "") };
                }));
            if (hidden > 0) h += "<button class='more-btn' onclick='showMore=!showMore;renderComparison()'>" + escapeHtml(showMore ? txt("ui.showLess") : txt("ui.showMore").replace("%1", hidden)) + "</button>";
            ss.forEach(function(s) {
                if (s.notes) h += "<div class='quote'>" + escapeHtml(shots[indexById[s.shotId]].date) + "  “" + escapeHtml(s.notes) + "”</div>";
            });
            document.getElementById("comparison").innerHTML = h;
        }

        function setBase(si) { base = si; renderComparison(); redrawChart(); }
        function toggleShot(id) { if (hiddenShots[id]) delete hiddenShots[id]; else hiddenShots[id] = true; renderComparison(); redrawChart(); }

        renderChips();
        renderComparison();
        renderReadout();
    </script>
</body>
</html>
)HTML");

    return html;
}


QString ShotServer::generateDebugPage() const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Debugging Tools - Decenza</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
    <script>if (typeof Chart === "undefined") document.getElementById("memoryBody").innerHTML = "<p style='color:#8b949e;padding:1em'>Chart.js failed to load (no internet?). Memory data is still available via <code>/api/memory</code>.</p>";</script>
    <style>)HTML" + QString::fromLatin1(WEB_CSS_VARIABLES) + WEB_CSS_HEADER + R"HTML(
        .header-content { max-width: 1400px; gap: 1rem; }
        h1 { flex: 1; }
        .status {
            font-size: 0.75rem;
            color: var(--text-secondary);
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: #18c37e;
            animation: pulse 2s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        .controls {
            display: flex;
            gap: 0.5rem;
        }
        .btn {
            padding: 0.5rem 1rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text);
            cursor: pointer;
            font-size: 0.875rem;
        }
        .btn:hover { border-color: var(--accent); color: var(--accent); }
        .btn.active { background: var(--accent); color: var(--bg); border-color: var(--accent); }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1rem;
        }
        /* Memory section */
        .memory-section {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-bottom: 1rem;
            overflow: hidden;
        }
        .memory-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 0.75rem 1rem;
            cursor: pointer;
            user-select: none;
        }
        .memory-header:hover { background: rgba(255,255,255,0.03); }
        .memory-header h2 { font-size: 0.875rem; font-weight: 600; }
        .memory-toggle { color: var(--text-secondary); font-size: 0.75rem; }
        .memory-body { padding: 0 1rem 1rem; }
        .memory-body.collapsed { display: none; }
        .memory-cards {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
            gap: 0.75rem;
            margin-bottom: 1rem;
        }
        .memory-card {
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 0.75rem;
            text-align: center;
        }
        .memory-card .label {
            font-size: 0.6875rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .memory-card .value {
            font-size: 1.25rem;
            font-weight: 700;
            color: var(--accent);
            margin-top: 0.25rem;
        }
        .memory-card .unit {
            font-size: 0.75rem;
            font-weight: 400;
            color: var(--text-secondary);
        }
        .chart-container {
            position: relative;
            height: 200px;
        }
        .class-table-wrap {
            max-height: 260px;
            overflow-y: auto;
            margin-top: 1rem;
            border: 1px solid var(--border);
            border-radius: 6px;
        }
        .class-table {
            width: 100%;
            border-collapse: collapse;
            font-size: 0.8125rem;
        }
        .class-table th {
            position: sticky;
            top: 0;
            background: var(--surface);
            text-align: left;
            padding: 0.5rem 0.75rem;
            color: var(--text-secondary);
            font-weight: 600;
            font-size: 0.6875rem;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            border-bottom: 1px solid var(--border);
        }
        .class-table td {
            padding: 0.35rem 0.75rem;
            border-bottom: 1px solid rgba(48,54,61,0.4);
            font-family: "Consolas", "Monaco", "Courier New", monospace;
        }
        .class-table tr:hover td { background: rgba(255,255,255,0.03); }
        .delta-pos { color: #f85149; }
        .delta-neg { color: #18c37e; }
        .delta-zero { color: var(--text-secondary); }
        .log-container {
            background: #000;
            border: 1px solid var(--border);
            border-radius: 8px;
            height: calc(100vh - 280px);
            overflow-y: auto;
            font-family: "Consolas", "Monaco", "Courier New", monospace;
            font-size: 12px;
            padding: 0.5rem;
            margin-bottom: 1rem;
        }
        .log-line {
            white-space: pre;
            padding: 1px 0;
        }
        .log-line:hover { background: rgba(255,255,255,0.05); }
        .DEBUG { color: #8b949e; }
        .INFO { color: #58a6ff; }
        .WARN { color: #d29922; }
        .ERROR { color: #f85149; }
        .FATAL { color: #ff0000; font-weight: bold; }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Debugging Tools</h1>
            <div class="status">
                <span class="status-dot"></span>
                <span id="lineCount">0 lines</span>
            </div>
            <div class="controls">
                <button class="btn active" id="autoScrollBtn" onclick="toggleAutoScroll()">Auto-scroll</button>
                <button class="btn" onclick="clearLog()">Clear</button>
                <button class="btn" onclick="loadPersistedLog()">Load Saved Log</button>
                <button class="btn" onclick="clearAll()">Clear All</button>
            </div>
        </div>
    </header>
    <main class="container">
        <div style="margin-bottom:1rem;display:flex;gap:0.5rem;flex-wrap:wrap;">
            <a href="/database.db" class="btn" style="text-decoration:none;">&#128190; Download Database</a>
            <button class="btn" onclick="downloadLog()">&#128196; Download Log</button>
            <a href="/upload" class="btn" style="text-decoration:none;">&#128230; Upload APK</a>
        </div>
        <div class="log-container" id="logContainer"></div>
        <div class="memory-section" id="memorySection">
            <div class="memory-header" onclick="toggleMemory()">
                <h2>Memory</h2>
                <span class="memory-toggle" id="memoryToggle">Collapse</span>
            </div>
            <div class="memory-body" id="memoryBody">
                <div class="memory-cards">
                    <div class="memory-card">
                        <div class="label">Current RSS</div>
                        <div class="value" id="memCurrent">--</div>
                    </div>
                    <div class="memory-card">
                        <div class="label">Peak RSS</div>
                        <div class="value" id="memPeak">--</div>
                    </div>
                    <div class="memory-card">
                        <div class="label">Startup RSS</div>
                        <div class="value" id="memStartup">--</div>
                    </div>
                    <div class="memory-card">
                        <div class="label">Growth</div>
                        <div class="value" id="memGrowth">--</div>
                    </div>
                    <div class="memory-card">
                        <div class="label">QObjects</div>
                        <div class="value" id="memObjects">--</div>
                    </div>
                    <div class="memory-card">
                        <div class="label">Uptime</div>
                        <div class="value" id="memUptime">--</div>
                    </div>
                </div>
                <div class="chart-container">
                    <canvas id="memoryChart"></canvas>
                </div>
                <div style="display:flex;justify-content:flex-end;margin-top:0.75rem;margin-bottom:0.25rem;">
                    <button class="btn" id="copyClassesBtn" onclick="copyClassTable()">Copy to clipboard</button>
                </div>
                <div class="class-table-wrap">
                    <table class="class-table">
                        <thead><tr><th>Class</th><th style="text-align:right">Count</th><th style="text-align:right">Since startup</th></tr></thead>
                        <tbody id="classTableBody"></tbody>
                    </table>
                </div>
            </div>
        </div>
    </main>
    <script>
)HTML"
// Split string literal to stay within MSVC's 16380-char per-segment limit (C2026)
R"HTML(        /* --- Memory section --- */
        var memoryCollapsed = false;
        var memoryChart = null;

        function toggleMemory() {
            memoryCollapsed = !memoryCollapsed;
            document.getElementById("memoryBody").classList.toggle("collapsed", memoryCollapsed);
            document.getElementById("memoryToggle").textContent = memoryCollapsed ? "Expand" : "Collapse";
        }

        function formatUptime(minutes) {
            if (minutes < 60) return minutes + "m";
            var h = Math.floor(minutes / 60);
            var m = minutes % 60;
            return h + "h " + m + "m";
        }

        function initChart() {
            var ctx = document.getElementById("memoryChart").getContext("2d");
            memoryChart = new Chart(ctx, {
                type: "line",
                data: {
                    labels: [],
                    datasets: [
                        {
                            label: "RSS (MB)",
                            data: [],
                            borderColor: "#c9a227",
                            backgroundColor: "rgba(201,162,39,0.1)",
                            borderWidth: 2,
                            pointRadius: 0,
                            fill: true,
                            yAxisID: "y",
                            tension: 0.3
                        },
                        {
                            label: "QObjects",
                            data: [],
                            borderColor: "#58a6ff",
                            backgroundColor: "rgba(88,166,255,0.1)",
                            borderWidth: 2,
                            pointRadius: 0,
                            fill: false,
                            yAxisID: "y1",
                            tension: 0.3
                        }
                    ]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    interaction: { mode: "index", intersect: false },
                    plugins: {
                        legend: {
                            labels: { color: "#8b949e", boxWidth: 12, font: { size: 11 } }
                        }
                    },
                    scales: {
                        x: {
                            ticks: { color: "#8b949e", font: { size: 10 }, maxTicksLimit: 12 },
                            grid: { color: "rgba(48,54,61,0.5)" }
                        },
                        y: {
                            type: "linear",
                            position: "left",
                            title: { display: true, text: "RSS (MB)", color: "#c9a227", font: { size: 11 } },
                            ticks: { color: "#c9a227", font: { size: 10 } },
                            grid: { color: "rgba(48,54,61,0.5)" }
                        },
                        y1: {
                            type: "linear",
                            position: "right",
                            title: { display: true, text: "QObjects", color: "#58a6ff", font: { size: 11 } },
                            ticks: { color: "#58a6ff", font: { size: 10 } },
                            grid: { drawOnChartArea: false }
                        }
                    }
                }
            });
        }

        function setCardText(id, text) {
            document.getElementById(id).textContent = text;
        }

        function updateMemory(data) {
            if (!data || !data.current || !data.peak || !data.startup) {
                console.warn("[Memory] Unexpected JSON shape:", data);
                return;
            }
            lastMemoryData = data;
            setCardText("memCurrent", data.current.rssMB.toFixed(1) + " MB");
            setCardText("memPeak", data.peak.rssMB.toFixed(1) + " MB");
            setCardText("memStartup", data.startup.rssMB.toFixed(1) + " MB");

            var growthMB = data.current.rssMB - data.startup.rssMB;
            var growthPct = data.startup.rssMB > 0 ? (growthMB / data.startup.rssMB * 100) : 0;
            var sign = growthMB >= 0 ? "+" : "";
            setCardText("memGrowth", sign + growthMB.toFixed(1) + " MB (" + sign + growthPct.toFixed(1) + "%)");

            setCardText("memObjects", data.current.qobjectCount.toLocaleString());
            setCardText("memUptime", formatUptime(data.uptimeMinutes));

            if (memoryChart && data.samples && data.samples.length > 0) {
                var firstT = data.samples[0].t;
                var labels = data.samples.map(function(s) {
                    var mins = Math.round((s.t - firstT) / 60000);
                    if (mins < 60) return mins + "m";
                    return Math.floor(mins / 60) + "h" + (mins % 60 ? (mins % 60) + "m" : "");
                });
                memoryChart.data.labels = labels;
                memoryChart.data.datasets[0].data = data.samples.map(function(s) { return s.rss; });
                memoryChart.data.datasets[1].data = data.samples.map(function(s) { return s.obj; });
                memoryChart.update("none");
            }

            // Per-class breakdown table
            var tbody = document.getElementById("classTableBody");
            if (data.topClasses && data.topClasses.length > 0) {
                var rows = "";
                for (var i = 0; i < data.topClasses.length; i++) {
                    var c = data.topClasses[i];
                    var dc = c.delta > 0 ? "delta-pos" : (c.delta < 0 ? "delta-neg" : "delta-zero");
                    var ds = c.delta > 0 ? "+" + c.delta : (c.delta === 0 ? "-" : String(c.delta));
                    rows += "<tr><td>" + escapeHtml(c.name) + "</td>"
                          + "<td style='text-align:right'>" + c.count.toLocaleString() + "</td>"
                          + "<td style='text-align:right' class='" + dc + "'>" + ds + "</td></tr>";
                }
                tbody.innerHTML = rows;
            }
        }

        var lastMemoryData = null;

        function copyClassTable() {
            if (!lastMemoryData || !lastMemoryData.topClasses) return;
            var lines = ["Class\tCount\tSince startup"];
            for (var i = 0; i < lastMemoryData.topClasses.length; i++) {
                var c = lastMemoryData.topClasses[i];
                var ds = c.delta > 0 ? "+" + c.delta : (c.delta === 0 ? "0" : String(c.delta));
                lines.push(c.name + "\t" + c.count + "\t" + ds);
            }
            lines.push("");
            lines.push("RSS: " + lastMemoryData.current.rssMB.toFixed(1) + " MB (peak: " + lastMemoryData.peak.rssMB.toFixed(1) + " MB, startup: " + lastMemoryData.startup.rssMB.toFixed(1) + " MB)");
            lines.push("QObjects: " + lastMemoryData.current.qobjectCount + ", Uptime: " + formatUptime(lastMemoryData.uptimeMinutes));
            var text = lines.join("\n");
            var btn = document.getElementById("copyClassesBtn");
            function onCopied() {
                btn.textContent = "Copied!";
                setTimeout(function() { btn.textContent = "Copy to clipboard"; }, 2000);
            }
            if (navigator.clipboard && navigator.clipboard.writeText) {
                navigator.clipboard.writeText(text).then(onCopied).catch(function() {
                    // Fallback for HTTP (clipboard API requires HTTPS)
                    var ta = document.createElement("textarea");
                    ta.value = text;
                    ta.style.position = "fixed";
                    ta.style.opacity = "0";
                    document.body.appendChild(ta);
                    ta.select();
                    document.execCommand("copy");
                    document.body.removeChild(ta);
                    onCopied();
                });
            } else {
                var ta = document.createElement("textarea");
                ta.value = text;
                ta.style.position = "fixed";
                ta.style.opacity = "0";
                document.body.appendChild(ta);
                ta.select();
                document.execCommand("copy");
                document.body.removeChild(ta);
                onCopied();
            }
        }

        function fetchMemory() {
            fetch("/api/memory")
                .then(function(r) {
                    if (!r.ok) throw new Error("Server error " + r.status);
                    return r.json();
                })
                .then(updateMemory)
                .catch(function(err) { console.warn("[Memory] fetch failed:", err); });
        }

        initChart();
        fetchMemory();
        var memTimer = setInterval(fetchMemory, 30000);
        document.addEventListener("visibilitychange", function() {
            if (document.hidden) { clearInterval(memTimer); }
            else { fetchMemory(); memTimer = setInterval(fetchMemory, 30000); }
        });

        /* --- Log section --- */
        var lastIndex = 0;
        var autoScroll = true;
        var container = document.getElementById("logContainer");
        var lineCountEl = document.getElementById("lineCount");

        function colorize(line) {
            var category = "";
            if (line.includes("] DEBUG ")) category = "DEBUG";
            else if (line.includes("] INFO ")) category = "INFO";
            else if (line.includes("] WARN ")) category = "WARN";
            else if (line.includes("] ERROR ")) category = "ERROR";
            else if (line.includes("] FATAL ")) category = "FATAL";
            return "<div class=\"log-line " + category + "\">" + escapeHtml(line) + "</div>";
        }

        )HTML" + QString::fromLatin1(WEB_JS_ESCAPE_HTML) + R"HTML(

        function fetchLogs() {
            fetch("/api/debug?after=" + lastIndex)
                .then(function(r) {
                    if (!r.ok) throw new Error("Server error (" + r.status + ")");
                    return r.json();
                })
                .then(function(data) {
                    if (data.lines && data.lines.length > 0) {
                        var html = "";
                        for (var i = 0; i < data.lines.length; i++) {
                            html += colorize(data.lines[i]);
                        }
                        container.insertAdjacentHTML("beforeend", html);
                        if (autoScroll) {
                            container.scrollTop = container.scrollHeight;
                        }
                    }
                    lastIndex = data.lastIndex;
                    lineCountEl.textContent = lastIndex + " lines";
                })
                .catch(function(e) { console.warn("fetchLogs:", e.message); });
        }

        function toggleAutoScroll() {
            autoScroll = !autoScroll;
            document.getElementById("autoScrollBtn").classList.toggle("active", autoScroll);
            if (autoScroll) {
                container.scrollTop = container.scrollHeight;
            }
        }

        function clearLog() {
            fetch("/api/debug/clear", { method: "POST" })
                .then(function(r) {
                    if (!r.ok) throw new Error("Server error " + r.status);
                    container.textContent = "";
                    lastIndex = 0;
                })
                .catch(function(e) { alert("Clear failed: " + e.message); });
        }

        function clearAll() {
            if (confirm("Clear both live log and saved log file?")) {
                fetch("/api/debug/clearall", { method: "POST" })
                    .then(function(r) {
                        if (!r.ok) throw new Error("Server error " + r.status);
                        container.textContent = "";
                        lastIndex = 0;
                    })
                    .catch(function(e) { alert("Clear failed: " + e.message); });
            }
        }

        function downloadLog() {
            fetch("/api/debug/file/zip")
                .then(function(r) {
                    if (!r.ok) throw new Error("Server error " + r.status);
                    return r.blob();
                })
                .then(function(blob) {
                    var a = document.createElement("a");
                    a.href = URL.createObjectURL(blob);
                    a.download = "debug.zip";
                    a.click();
                    URL.revokeObjectURL(a.href);
                })
                .catch(function(e) { alert("Download failed: " + e.message); });
        }

        function loadPersistedLog() {
            fetch("/api/debug/file")
                .then(function(r) {
                    if (!r.ok) throw new Error("Server error " + r.status);
                    return r.json();
                })
                .then(function(data) {
                    if (data.log) {
                        container.innerHTML = "";
                        var lines = data.log.split("\n");
                        var html = "";
                        for (var i = 0; i < lines.length; i++) {
                            if (lines[i]) html += colorize(lines[i]);
                        }
                        container.innerHTML = html;
                        lineCountEl.textContent = lines.length + " lines (from file)";
                        if (autoScroll) {
                            container.scrollTop = container.scrollHeight;
                        }
                    } else {
                        alert("No saved log file found");
                    }
                })
                .catch(function(e) { alert("Load failed: " + e.message); });
        }

        // Poll every 500ms
        var logTimer = setInterval(fetchLogs, 500);
        document.addEventListener('visibilitychange', function() {
            if (document.hidden) { clearInterval(logTimer); }
            else { fetchLogs(); logTimer = setInterval(fetchLogs, 500); }
        });
        fetchLogs();
    </script>
</body>
</html>
)HTML");
}

