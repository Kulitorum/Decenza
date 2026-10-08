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
#include "../history/decentuploadstate.h"
#include "decentshotuploader.h"
#include "beanbase_blob.h"
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
#include <QSqlDatabase>
#include <QSqlQuery>
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

        // The app's "Recipe" button beside Load.
        function promoteToRecipe(id) { createRecipeFromShot(id, alert); }

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
    html += WEB_JS_RECIPE_FROM_SHOT;
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

// JSON as a script literal. "<" only ever occurs inside a JSON string, where
// the escape \u003c reads the same, so no note or name can close the <script>
// or open a comment that swallows it.
static QString embedJson(const QJsonValue& v)
{
    const QByteArray json = v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)
                                        : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json).replace(QLatin1Char('<'), QStringLiteral("\\u003c"));
}

// Worker thread: what the shot page shows beyond the shot itself — its curves, how
// it went against the previous shot on its profile, and its neighbours in history.
QJsonObject ShotServer::shotPageData(QSqlDatabase& db, const ShotRecord& record)
{
    const qint64 id = record.summary.id;
    QJsonParseError phaseError;
    const QJsonDocument phases = QJsonDocument::fromJson(record.phaseSummariesJson.toUtf8(), &phaseError);
    if (!record.phaseSummariesJson.isEmpty() && phaseError.error != QJsonParseError::NoError)
        DIAG_WARN(NETWORK, "ShotServer") << "Phase summaries of shot" << id << "unreadable:" << phaseError.errorString();
    DecentUploadState decent;
    ShotHistoryStorage::loadDecentUploadStateStatic(db, id, &decent);
    return QJsonObject{
        { QStringLiteral("graph"), graphTraceJson(record, true) },
        { QStringLiteral("outcome"), QJsonObject::fromVariantMap(ShotHistoryStorage::shotOutcomeStatic(db, id)) },
        { QStringLiteral("newerId"), ShotHistoryStorage::neighbourShotIdStatic(db, id, true) },
        { QStringLiteral("olderId"), ShotHistoryStorage::neighbourShotIdStatic(db, id, false) },
        { QStringLiteral("phaseSummaries"), phases.array() },
        { QStringLiteral("decent"), QJsonObject{
            { QStringLiteral("uploaded"), decent.uploaded() },
            { QStringLiteral("rejectedStatus"), decent.rejectedStatus },
            { QStringLiteral("viewUrl"), decent.uploaded() ? DecentShotUploader::shotViewUrl(decent.serial, decent.serverShotId) : QString() },
        } },
    };
}

QString ShotServer::generateShotDetailPage(const ShotProjection& shot, const QJsonObject& pageData) const
{
    if (!shot.isValid()) {
        return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Not Found</title></head>"
                  "<body style=\"background:#0d1117;color:#fff;font-family:sans-serif;padding:2rem;\">"
                  "<h1>Shot not found</h1><a href=\"/\" style=\"color:#c9a227;\">Back to list</a></body></html>");
    }

    // The fields the page shows and edits. Text goes in as JSON and reaches the
    // page through escapeHtml(), never through string templating.
    QJsonObject fields{
        { "id", shot.id },
        // The app shows RPM only for grinders that report it (grinderRpmCapable).
        { "rpmCapable", m_settings && m_settings->dye()->grinderRpmCapable(shot.grinderBrand, shot.grinderModel) },
        { "profileName", shot.profileName },
        { "dateTime", shot.dateTime },
        { "temperatureOverrideC", shot.temperatureOverrideC },
        { "doseWeightG", shot.doseWeightG },
        { "finalWeightG", shot.finalWeightG },
        { "enjoyment", shot.enjoyment0to100 },
        { "tasteBalance", shot.tasteBalance },
        { "tasteBody", shot.tasteBody },
        { "beanBrand", shot.beanBrand },
        { "beanType", shot.beanType },
        { "roastDate", shot.roastDate },
        { "roastLevel", shot.roastLevel },
        { "grinderBrand", shot.grinderBrand },
        { "grinderModel", shot.grinderModel },
        { "grinderBurrs", shot.grinderBurrs },
        { "grinderSetting", shot.grinderSetting },
        { "rpm", shot.rpm },
        { "targetWeightG", shot.targetWeightG },
        { "espressoNotes", shot.espressoNotes },
        { "barista", shot.barista },
        { "beverageType", shot.beverageType.isEmpty() ? QStringLiteral("espresso") : shot.beverageType },
        { "drinkTds", shot.drinkTdsPct },
        { "drinkEy", shot.drinkEyPct },
        { "debugLog", shot.debugLog },
        { "visualizerId", shot.visualizerId },
        { "visualizerUrl", shot.visualizerUrl },
        { "equipmentId", shot.equipmentId },
        { "equipmentName", shot.equipmentName },
        // The bag link and snapshot, so a bean pick can be undone to what was there.
        { "bagId", shot.bagId },
        { "beanBaseJson", shot.beanBaseJson },
        { "beanBaseId", BeanBaseBlob::canonicalId(shot.beanBaseJson) },
        { "frozenDate", shot.frozenDate },
        { "defrostDate", shot.defrostDate },
        { "storageHint", shot.storageHint },
        { "openedDate", shot.openedDate },
        { "basketBrand", shot.basketBrand },
        { "basketModel", shot.basketModel },
        { "puckPrep", shot.puckPrep },
    };
    // A recipe whose row is gone shows as no recipe.
    if (shot.recipeId > 0 && !shot.recipeName.isEmpty()) {
        fields[QStringLiteral("recipe")] = QJsonObject{
            { "name", shot.recipeName },
            { "archived", shot.recipeArchived },
            { "icon", drinkTypeEmoji(shot.recipeDrinkType) },
        };
    }
    QJsonArray summaryLines;
    for (const QVariant& line : shot.summaryLines) {
        const QVariantMap m = line.toMap();
        summaryLines.append(QJsonObject{ { "type", m.value("type").toString() }, { "text", m.value("text").toString() } });
    }
    fields[QStringLiteral("summaryLines")] = summaryLines;

    // Which destinations an Upload can go to, read here on the main thread.
    QJsonObject page = pageData;
    page[QStringLiteral("uploads")] = QJsonObject{
        { QStringLiteral("visualizerActive"), m_settings && m_settings->visualizer()->visualizerActive() },
        { QStringLiteral("decentActive"), m_settings && m_settings->decent()->active() },
    };

    QString html = QStringLiteral(R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Shot - Decenza</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
    <style>)HTML");
    html += QString::fromLatin1(WEB_CSS_VARIABLES) + WEB_CSS_HEADER + WEB_CSS_MENU + WEB_CSS_TOAST
          + QString::fromUtf8(WEB_CSS_SHOT_GRAPH) + QString::fromUtf8(WEB_CSS_COMPARISON_TEXT);
    html += QStringLiteral(R"HTML(
        .header { padding: 0.75rem 1.5rem; }
        .header-content { max-width: 1600px; gap: 1rem; }
        .back-btn { line-height: 1; padding: 0.25rem; }
        .menu-wrapper { margin-left: auto; }
        .header-title { flex: 1; min-width: 0; }
        .header-title h1 { font-size: 1.125rem; font-weight: 600; }
        .header-title .subtitle { font-size: 0.75rem; color: var(--text-secondary); }
        .header-title .override { color: var(--accent); }
        .header-title .plan { font-size: 0.8125rem; color: var(--text); margin-top: 0.15rem; }
        .header-title .plan:empty { display: none; }
        /* Archived dims AND carries a title attribute, so the state is not colour-only. */
        .shot-detail-recipe { color: var(--accent); font-weight: 600; }
        .shot-detail-recipe.archived { color: var(--text-secondary); font-weight: 500; }
        .nav-btn, .edit-btn { background: none; border: 1px solid var(--border); color: var(--text-secondary); font-size: 0.875rem;
                              cursor: pointer; padding: 0.375rem 0.75rem; border-radius: 6px; white-space: nowrap; text-decoration: none; font-family: inherit; }
        .nav-btn:hover, .edit-btn:hover { color: var(--accent); border-color: var(--accent); }
        .nav-btn.disabled { opacity: 0.35; pointer-events: none; }
        .col { display: flex; flex-direction: column; gap: 1rem; min-width: 0; }

        .row { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 0.75rem; align-items: baseline; padding: 0.35rem 0;
               border-top: 1px solid rgba(48,54,61,0.6); font-variant-numeric: tabular-nums; }
        .row .label { color: var(--text-secondary); font-size: 0.875rem; }
        .row .value { font-weight: 500; text-align: right; }
        .row .value .unit { font-size: 0.75rem; color: var(--text-secondary); margin-left: 0.2rem; font-weight: 400; }
        .row .value .target { font-size: 0.75rem; color: var(--text-secondary); font-weight: 400; margin-left: 0.3rem; }
        .compared { display: flex; justify-content: space-between; gap: 1rem; color: var(--text-secondary); font-size: 0.875rem; margin: 0.2rem 0 0.4rem; }
        .compared a { color: var(--accent); text-decoration: none; white-space: nowrap; }
        .since-summary { font-size: 0.95rem; line-height: 1.5; }
        .changes { margin: 0.5rem 0 0; padding-left: 1.1rem; color: var(--text-secondary); font-size: 0.875rem; line-height: 1.6; }

        .rating-line { font-size: 1.25rem; font-weight: 600; color: var(--accent); }
        .rating-line .unrated { color: var(--text-secondary); font-weight: 400; font-size: 1rem; }
        .shot-quality { display: flex; flex-wrap: wrap; align-items: center; gap: 0.5rem; margin-top: 0.6rem; }
        .badge { display: inline-flex; align-items: center; gap: 0.375rem; padding: 0 0.75rem; height: 28px; border-radius: 14px;
                 border: 1px solid; font-size: 0.75rem; line-height: 1; white-space: nowrap; }
        .badge .dot { width: 8px; height: 8px; border-radius: 50%; }
        .badge.danger { color: #e73249; border-color: #e73249; background: rgba(231,50,73,0.15); }
        .badge.danger .dot { background: #e73249; }
        .badge.warning { color: #f0a020; border-color: #f0a020; background: rgba(240,160,32,0.15); }
        .badge.warning .dot { background: #f0a020; }
        .badge.success { color: #18c37e; border-color: #18c37e; background: rgba(24,195,126,0.15); }
        .badge.success .dot { background: #18c37e; }
        .summary-btn { display: inline-flex; align-items: center; gap: 0.375rem; padding: 0 0.75rem; height: 28px; border-radius: 14px;
                       background: var(--surface); border: 1px solid var(--border); color: var(--text-secondary); font-size: 0.75rem;
                       cursor: pointer; font-family: inherit; line-height: 1; white-space: nowrap; }
        .summary-btn:hover { color: var(--accent); border-color: var(--accent); }
        .summary-modal { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.6); z-index: 300; align-items: center;
                         justify-content: center; padding: 1rem; }
        .summary-modal.open { display: flex; }
        .summary-modal-content { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; padding: 1.5rem;
                                 max-width: 450px; width: 100%; max-height: 80vh; overflow-y: auto; }
        .summary-modal-content h2 { text-align: center; font-size: 1rem; font-weight: 600; margin-bottom: 1rem; }
        .summary-line { display: flex; align-items: flex-start; gap: 0.5rem; padding: 0.375rem 0; color: var(--text-secondary);
                        font-size: 0.875rem; line-height: 1.4; }
        .summary-line .line-dot { width: 6px; height: 6px; border-radius: 50%; margin-top: 0.5rem; flex-shrink: 0; }
        .summary-line.good .line-dot { background: #18c37e; }
        .summary-line.caution .line-dot { background: #f0a020; }
        .summary-line.warning .line-dot { background: #e73249; }
        .summary-line.observation .line-dot { background: var(--text-secondary); }
        .summary-line.verdict { color: var(--text); font-weight: 500; padding-top: 0.75rem; margin-top: 0.5rem; border-top: 1px solid var(--border); }
        .summary-line.verdict .line-dot { display: none; }
        .modal-btn { width: 100%; margin-top: 1rem; padding: 0.625rem; background: var(--accent); border: none; border-radius: 8px;
                     color: #000; font-weight: 500; cursor: pointer; font-family: inherit; font-size: 0.875rem; }
        .modal-btn.secondary { background: var(--surface-hover); color: var(--text); border: 1px solid var(--border); }
        .modal-btn.danger { background: #e73249; color: #fff; }

        .card h3 { font-size: 0.75rem; font-weight: 600; margin-bottom: 0.5rem; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.08em; }
        .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 1rem; }
        .notes-text { color: var(--text-secondary); font-style: italic; white-space: pre-wrap; }
        .actions { display: flex; gap: 0.6rem; flex-wrap: wrap; }
        .action-btn { display: inline-flex; align-items: center; gap: 0.5rem; padding: 0.6rem 1rem; background: var(--surface);
                      border: 1px solid var(--border); border-radius: 8px; color: var(--text); font-size: 0.875rem; cursor: pointer; font-family: inherit; }
        .action-btn:hover { border-color: var(--text-secondary); }
        .action-btn.danger { color: #e73249; }
        .action-btn.danger:hover { border-color: #e73249; }
        #debugLogContent { background: var(--bg); padding: 1rem; border-radius: 8px; overflow-x: auto; font-size: 0.75rem; line-height: 1.4;
                           white-space: pre-wrap; word-break: break-all; max-height: 500px; overflow-y: auto; }

        .edit-input, .edit-textarea { width: 100%; background: var(--bg); border: 1px solid var(--border); border-radius: 6px;
                                      color: var(--text); font-family: inherit; font-size: 0.875rem; padding: 0.5rem 0.75rem; }
        .edit-input:focus, .edit-textarea:focus { outline: none; border-color: var(--accent); }
        .edit-textarea { min-height: 6em; resize: vertical; }
        .edit-row { display: flex; justify-content: space-between; align-items: center; padding: 0.4rem 0; gap: 1rem; }
        .edit-row .label { color: var(--text-secondary); white-space: nowrap; min-width: 80px; font-size: 0.875rem; }
        .edit-row .edit-field { flex: 1; max-width: 22rem; }
        .edit-row .edit-input { text-align: right; }
        .taste { display: flex; gap: 0.4rem; flex-wrap: wrap; justify-content: flex-end; }
        .rating-row { display: flex; align-items: center; gap: 0.5rem; flex-wrap: wrap; }
        .rating-row input[type=range] { flex: 1; min-width: 8rem; accent-color: var(--accent); }
        .rating-row #ratingPct { min-width: 3rem; text-align: right; font-weight: 600; color: var(--accent); }
        .measure { display: grid; grid-template-columns: repeat(auto-fit, minmax(7.5rem, 1fr)); gap: 0.6rem; }
        .measure label { display: flex; flex-direction: column; gap: 0.25rem; font-size: 0.75rem; color: var(--text-secondary); }
        .measure .edit-input { text-align: center; font-weight: 600; color: var(--text); }
        .phases { width: 100%; border-collapse: collapse; font-size: 0.8125rem; font-variant-numeric: tabular-nums; }
        .phases th { text-align: left; color: var(--text-secondary); font-weight: 500; padding: 0.2rem 0.4rem 0.2rem 0; }
        .phases td { padding: 0.2rem 0.4rem 0.2rem 0; border-top: 1px solid rgba(48,54,61,0.6); }
        .uploads .up.ok { color: #18c37e; } .uploads .up.bad { color: #e73249; }
        .uploads a { color: var(--accent); text-decoration: none; margin-left: 0.5rem; font-size: 0.875rem; }
        .action-btn.small { padding: 0.35rem 0.7rem; font-size: 0.8125rem; margin-top: 0.5rem; }
        .picker-item { display: flex; flex-direction: column; gap: 0.1rem; width: 100%; text-align: left; padding: 0.6rem 0.75rem; margin-top: 0.4rem;
                       background: var(--bg); border: 1px solid var(--border); border-radius: 8px; color: var(--text); cursor: pointer; font-family: inherit; font-size: 0.9rem; }
        .picker-item.active { border-color: var(--accent); }
        .picker-item:hover { border-color: var(--text-secondary); }
        /* Phone: the title takes the first line; the shot buttons wrap under it. */
        @media (max-width: 600px) {
            .header { padding: 0.75rem 1rem; }
            .header-content { flex-wrap: wrap; }
            .header-title { flex: 1 1 calc(100% - 3rem); }
            .nav-btn, .edit-btn { padding: 0.3rem 0.55rem; }
        }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <div class="header-title">
                <h1 id="title"></h1>
                <div class="subtitle" id="subtitle"></div>
                <div class="plan" id="plan"></div>
            </div>
            <a class="nav-btn" id="newerBtn" title="Newer shot">&#8249; Newer</a>
            <a class="nav-btn" id="olderBtn" title="Older shot">Older &#8250;</a>
            <button class="edit-btn" id="undoBtn" style="display:none" onclick="undoLast()">&#8630; Undo</button>
)HTML");
    html += generateMenuHtml();
    html += QStringLiteral(R"HTML(
        </div>
    </header>
    <main class="layout" id="page">
        <section class="col graph-col">
            <div class="card">
                <div class="chart-wrapper"><canvas id="shotChart"></canvas></div>
                <div id="readout" class="readout"></div>
                <div id="chips" class="chips"></div>
            </div>
        </section>
        <section class="col" id="details"></section>
    </main>
)HTML") + WEB_HTML_TOAST + QStringLiteral(R"HTML(

    <div class="summary-modal" id="summaryModal" onclick="if(event.target===this)closeModal('summaryModal')">
        <div class="summary-modal-content">
            <h2>Shot Summary</h2>
            <div id="summaryLines"></div>
            <button class="modal-btn" onclick="closeModal('summaryModal')">OK</button>
        </div>
    </div>
    <div class="summary-modal" id="pickerModal" onclick="if(event.target===this)closeModal('pickerModal')">
        <div class="summary-modal-content">
            <h2 id="pickerTitle"></h2>
            <div id="pickerList"></div>
            <button class="modal-btn secondary" onclick="closeModal('pickerModal')">Cancel</button>
        </div>
    </div>
    <div class="summary-modal" id="deleteModal" onclick="if(event.target===this)closeModal('deleteModal')">
        <div class="summary-modal-content">
            <h2>Delete this shot?</h2>
            <p class="note" style="text-align:center">It is removed from your history on this device. This cannot be undone.</p>
            <button class="modal-btn danger" id="deleteConfirm" onclick="deleteShot()">Delete</button>
            <button class="modal-btn secondary" onclick="closeModal('deleteModal')">Cancel</button>
        </div>
    </div>

    <script>
)HTML");
    html += QStringLiteral("        var shot = ") + embedJson(fields)
          + QStringLiteral(";\n        var page = ") + embedJson(page)
          + QStringLiteral(";\n        var texts = ") + embedJson(ShotComparisonText::toJson())
          + QStringLiteral(";\n        var dialInLabels = ") + embedJson(QJsonObject::fromVariantMap(ProfileDialInText::labelMap()))
          + QStringLiteral(";\n        var puckFlags = ") + embedJson(QJsonArray::fromVariantList(EquipmentStorage::puckPrepFlags()))
          + QStringLiteral(";\n");
    html += QString::fromLatin1(WEB_JS_ESCAPE_HTML);
    html += QString::fromLatin1(WEB_JS_MENU);
    html += QString::fromLatin1(WEB_JS_POWER_CONTROL);
    html += QString::fromLatin1(WEB_JS_GRIND_DATALIST);
    html += QString::fromLatin1(WEB_JS_TOAST);
    html += QString::fromLatin1(WEB_JS_RECIPE_FROM_SHOT);
    html += QString::fromUtf8(WEB_JS_COMPARISON_TEXT);
    html += QString::fromUtf8(WEB_JS_SHOT_GRAPH);
    html += QStringLiteral(R"HTML(
        // === Header and navigation ===
        var cmp = (page.outcome && page.outcome.comparison) || {};
        var cmpShots = cmp.shots || [];
        var me = cmpShots.length - 1;                 // this shot's column: last, after the previous shot
        var since = (cmp.comparisons || [])[0] || null;
        function setHeader() {
            var t = escapeHtml(shot.profileName);
            if (shot.temperatureOverrideC > 0) t += " <span class='override'>(" + shot.temperatureOverrideC.toFixed(0) + "°C)</span>";
            document.getElementById("title").innerHTML = t;
            var sub = escapeHtml(shot.dateTime);
            if (shot.recipe) sub += "  ·  <span class='shot-detail-recipe" + (shot.recipe.archived ? " archived' title='Archived recipe" : "") + "'>"
                                  + shot.recipe.icon + " " + escapeHtml(shot.recipe.name) + "</span>";
            document.getElementById("subtitle").innerHTML = sub;
            // A fixed plan line in the app's default Shot Plan order (the app follows the user's widget layout).
            var plan = [];
            if (shot.doseWeightG > 0) {
                var y = shot.finalWeightG.toFixed(1) + "g";
                if (shot.targetWeightG > 0 && Math.abs(shot.targetWeightG - shot.finalWeightG) >= 0.05) y += " (target " + shot.targetWeightG.toFixed(1) + "g)";
                plan.push(shot.doseWeightG.toFixed(1) + "g in → " + y);
                if (shot.finalWeightG > 0) plan.push("1:" + (shot.finalWeightG / shot.doseWeightG).toFixed(1));
            }
            if (shot.grinderSetting) plan.push("grind " + shot.grinderSetting + (shot.rpmCapable && shot.rpm > 0 ? " · " + shot.rpm + " rpm" : ""));
            document.getElementById("plan").textContent = plan.join("  ·  ");
            document.title = shot.profileName + " - Decenza";
            [["newerBtn", page.newerId], ["olderBtn", page.olderId]].forEach(function(b) {
                var el = document.getElementById(b[0]);
                if (b[1] > 0) el.href = "/shot/" + b[1]; else el.classList.add("disabled");
            });
        }
        document.addEventListener("keydown", function(e) {
            if (e.target.closest("input, textarea, select")) return;
            if (e.key === "ArrowLeft" && page.newerId > 0) location.href = "/shot/" + page.newerId;
            if (e.key === "ArrowRight" && page.olderId > 0) location.href = "/shot/" + page.olderId;
        });

        // === Shot results: against the previous shot on the profile, when there is one ===
        var showMore = false;
        function sinceHtml() {
            if (!since || !page.outcome.previousShotId) return "";
            var h = "<div class='compared'><span>Compared with your last " + escapeHtml(cmpShots[0].profileName) + " shot · "
                  + escapeHtml(page.outcome.previousDateTime) + "</span><a href='/compare/" + page.outcome.previousShotId + "," + shot.id
                  + "'>Compare &#8250;</a></div><div class='since-summary'>" + escapeHtml(summaryFor(since)) + "</div>";
            var items = [];
            (cmp.inputs || []).forEach(function(r) {
                var a = r.cells[0], b = r.cells[me];
                if (!b || b.state === "same") return;
                items.push(escapeHtml(txt("input." + r.key, r.key) + " " + inputText(r, a) + " → " + inputText(r, b)) + pill(b.delta, inputDelta(r, b.delta)));
            });
            ((since.profile && since.profile.rows) || []).forEach(function(r) { items.push(escapeHtml(diffRowText(r))); });
            // The summary already says "Same setup" when nothing changed.
            if (items.length) h += "<ul class='changes'>" + items.map(function(i) { return "<li>" + i + "</li>"; }).join("") + "</ul>";
            return h;
        }
        function renderHappened() {
            var h = "<div class='section'>Shot results</div>" + sinceHtml() + "<div style='height:0.6rem'></div>", hidden = 0;
            (cmp.metrics || []).forEach(function(r) {
                if (r.more) hidden++;
                if (r.more && !showMore) return;
                var cell = r.cells[me] || {}, unit = unitLabel(r.unit);
                var has = cell.value !== null && cell.value !== undefined;
                var target = cmpShots[me] && cmpShots[me].targetYieldG;
                var v = "<span>" + escapeHtml(metricText(r, cell.value)) + "</span>"
                      + (has && r.key === "yieldG" && target ? "<span class='target'>/ " + target.toFixed(1) + "</span>" : "")
                      + (has && unit && r.key !== "ratio" ? "<span class='unit'>" + escapeHtml(unit) + "</span>" : "");
                if (cell.delta !== null && cell.delta !== undefined) v += pill(cell.delta, signed(cell.delta, r.decimals));
                h += "<div class='row'><div class='label'>" + escapeHtml(txt("metric." + r.key, r.key)) + "</div><div class='value'>" + v + "</div></div>";
            });
            var stop = cmpShots[me] && cmpShots[me].stoppedBy;
            h += "<div class='row'><div class='label'>" + escapeHtml(txt("row.stopped")) + "</div><div class='value'>" + escapeHtml(stop ? txt("stop." + stop, DASH) : DASH) + "</div></div>";
            if (hidden > 0) h += "<button class='more-btn' onclick='showMore=!showMore;renderHappened()'>"
                               + escapeHtml(showMore ? txt("ui.showLess") : txt("ui.showMore").replace("%1", hidden)) + "</button>";
            document.getElementById("happened").innerHTML = h;
        }

        // === Details: every field saves as it changes, as the app's page does ===
        var META = { enjoyment: "enjoyment", tasteBalance: "tasteBalance", tasteBody: "tasteBody", espressoNotes: "espressoNotes",
                     doseWeightG: "doseWeight", finalWeightG: "finalWeight", grinderSetting: "grinderSetting", rpm: "rpm",
                     drinkTds: "drinkTds", drinkEy: "drinkEy", barista: "barista" };
        var undoStack = [];
        function field(label, inner) { return "<div class='edit-row'><span class='label'>" + escapeHtml(label) + "</span><div class='edit-field'>" + inner + "</div></div>"; }
        function numInput(key, step, min, max) {
            var v = shot[key];
            return "<input class='edit-input' id='f_" + key + "' type='number' step='" + step + "' min='" + min + "'" + (max ? " max='" + max + "'" : "")
                 + " value='" + (v > 0 ? v : "") + "' onchange='fieldChanged(\"" + key + "\")'>";
        }
        function chips(key, values) {
            return "<div class='taste'>" + values.map(function(v) {
                var on = shot[key] === v;
                return "<button type='button' class='chip" + (on ? " on" : "") + "' aria-pressed='" + on + "' onclick='pickChip(\"" + key + "\",\"" + v + "\")'>"
                     + escapeHtml(txt("taste." + v, v)) + "</button>";
            }).join("") + "</div>";
        }
        function ratingHtml() {
            var r = shot.enjoyment || 0;
            return "<div class='rating-row'>" + [25, 50, 75, 100].map(function(p) {
                return "<button type='button' class='chip preset" + (r === p ? " on" : "") + "' onclick='setRating(" + p + ")'>" + p + "</button>";
            }).join("") + "<input type='range' id='f_enjoyment' min='0' max='100' step='1' value='" + r
                 + "' oninput='document.getElementById(\"ratingPct\").textContent=this.value+\"%\"' onchange='setRating(parseInt(this.value))'>"
                 + "<span id='ratingPct'>" + r + "%</span></div>";
        }
        function phaseSummaryHtml() {
            var rows = page.phaseSummaries || [];
            if (!rows.length) return "";
            var h = "<div class='card'><h3>Phase summary</h3><table class='phases'><tr><th>Phase</th><th>Duration</th><th>Avg press</th><th>Avg flow</th><th>Weight</th></tr>";
            rows.forEach(function(p) {
                h += "<tr><td>" + escapeHtml(p.name) + "</td><td>" + (p.duration || 0).toFixed(1) + "s</td><td>" + (p.avgPressure || 0).toFixed(1) + " bar</td><td>"
                   + (p.avgFlow || 0).toFixed(1) + " mL/s</td><td>" + (p.weightGained || 0).toFixed(1) + "g</td></tr>";
            });
            return h + "</table></div>";
        }
        function uploadsHtml() {
            var d = page.decent || {}, items = [];
            if (shot.visualizerId) items.push("<span class='up ok'>&#9729; Uploaded to Visualizer</span>"
                + (shot.visualizerUrl ? " <a href='" + escapeHtml(shot.visualizerUrl) + "' target='_blank' rel='noopener'>View on Visualizer</a>" : ""));
            if (d.uploaded) items.push("<span class='up ok'>&#9729; Uploaded to Decent</span>" + (d.viewUrl ? " <a href='" + escapeHtml(d.viewUrl) + "' target='_blank' rel='noopener'>View on decentespresso.com</a>" : ""));
            else if (d.rejectedStatus > 0) items.push("<span class='up bad'>Not accepted by Decent (HTTP " + d.rejectedStatus + ")</span>");
            if (!items.length) return "";
            return "<div class='card uploads'>" + items.map(function(i) { return "<div>" + i + "</div>"; }).join("") + "</div>";
        }
        function renderDetails() {
            var h = "<div class='card'><h3>How was this shot?</h3>" + ratingHtml()
                  + field("Taste", chips("tasteBalance", ["sour", "balanced", "bitter"]))
                  + field("Body", chips("tasteBody", ["thin", "medium", "heavy"]))
                  + badgesHtml() + "</div>";
            h += "<div class='card'><h3>Notes</h3><textarea class='edit-textarea' id='f_espressoNotes' placeholder='Tasting notes' onchange='fieldChanged(\"espressoNotes\")'>"
               + escapeHtml(shot.espressoNotes) + "</textarea></div>";
            h += "<div class='card'><h3>Measurements</h3><div class='measure'>"
               + "<label>Dose (g)" + numInput("doseWeightG", 0.1, 0, 40) + "</label>"
               + "<label>Out (g)" + numInput("finalWeightG", 0.1, 0, 500) + "</label>"
               + "<label>Grind<input class='edit-input' id='f_grinderSetting' value='" + escapeHtml(shot.grinderSetting) + "' onchange='fieldChanged(\"grinderSetting\")'></label>"
               + "<label" + (shot.rpmCapable ? "" : " style='display:none'") + ">RPM" + numInput("rpm", 1, 0) + "</label>"
               + "<label>TDS (%)" + numInput("drinkTds", 0.01, 0, 35) + "</label>"
               + "<label>EY (%)" + numInput("drinkEy", 0.1, 0, 40) + "</label>"
               + "</div></div>";
            h += "<div class='card' id='happened'></div>";   // same order as the app's page
            h += "<div class='cards'>";
            h += "<div class='card'><h3>Beans</h3>" + rowHtml("Roaster", shot.beanBrand) + rowHtml("Coffee", shot.beanType)
               + rowHtml("Roast date", shot.roastDate) + rowHtml("Roast level", shot.roastLevel)
               + "<button class='action-btn small' onclick='pickBag()'>" + (shot.beanBrand || shot.beanType ? "Change beans" : "Select beans") + "</button></div>";
            var grinder = (shot.grinderBrand + " " + shot.grinderModel).trim();
            h += "<div class='card'><h3>Equipment</h3>" + rowHtml("Grinder", shot.equipmentName || grinder) + rowHtml("Burrs", shot.grinderBurrs)
               + rowHtml("Basket", (shot.basketBrand + " " + shot.basketModel).trim()) + rowHtml("Puck prep", puckLabels(shot.puckPrep).join(" · "))
               + "<button class='action-btn small' onclick='pickEquipment()'>" + (grinder || shot.equipmentName ? "Change equipment" : "Add equipment") + "</button></div>";
            h += "<div class='card'><h3>Additional</h3>"
               + field("Barista", "<input class='edit-input' id='f_barista' value='" + escapeHtml(shot.barista) + "' onchange='fieldChanged(\"barista\")'>")
               + rowHtml("Beverage", shot.beverageType) + "</div></div>";
            h += phaseSummaryHtml() + uploadsHtml();
            h += "<div class='actions'>"
               + (page.uploads && (page.uploads.visualizerActive || page.uploads.decentActive) ? "<button class='action-btn' onclick='uploadNow()'>&#9729; Upload</button>" : "")
               + (shot.recipe ? "" : "<button class='action-btn' onclick='saveAsRecipe()'>&#128204; Save as recipe</button>")
               + "<button class='action-btn' onclick='location.href=location.pathname+\"/profile.json\"'>&#128196; Profile JSON</button>"
               + "<button class='action-btn' onclick='location.href=location.pathname+\"/shot.json\"'>&#11015; Shot JSON</button>"
               + "<button class='action-btn' onclick='toggleDebugLog()'>&#128203; Debug Log</button>"
               + "<button class='action-btn danger' onclick='openModal(\"deleteModal\")'>&#128465; Delete Shot</button></div>";
            h += "<div class='card' id='debugLog' style='display:none'><div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:0.75rem;'>"
               + "<h3 style='margin:0'>Debug Log</h3><button class='action-btn small' onclick='copyDebugLog()'>Copy</button></div>"
               + "<pre id='debugLogContent'>" + escapeHtml(shot.debugLog || "No debug log available") + "</pre></div>";
            document.getElementById("details").innerHTML = h;
            renderHappened();
            // Stepped candidates for the SHOT's own grinder, not the active one
            // (grind-value-entry). Free text stays accepted either way.
            attachGrindDatalist(document.getElementById("f_grinderSetting"), document.getElementById("f_rpm"), shot.grinderBrand, shot.grinderModel,
                                function(capable) { if (shot.rpmCapable !== capable) { shot.rpmCapable = capable; setHeader(); } });
            renderUndo();
        }
        function rowHtml(label, value) {
            return "<div class='row'><div class='label'>" + escapeHtml(label) + "</div><div class='value'>" + escapeHtml(value || DASH) + "</div></div>";
        }
        function badgesHtml() {
            var b = cmpShots[me] ? cmpShots[me].badges || [] : [];
            var kinds = { channeling: "danger", pourTruncated: "danger", skipFirstFrame: "danger", grindIssue: "warning" };
            var h = "<div class='shot-quality'>";
            b.forEach(function(k) { h += "<span class='badge " + (kinds[k] || "warning") + "'><span class='dot'></span>" + escapeHtml(txt("badge." + k, k)) + "</span>"; });
            if (b.length === 0) h += "<span class='badge success'><span class='dot'></span>Clean extraction</span>";
            if (shot.summaryLines.length) h += "<button class='summary-btn' onclick='openModal(\"summaryModal\")'>&#128202; Shot Summary</button>";
            return h + "</div>";
        }

        // --- Saving ---
        function num(id) { var v = parseFloat(document.getElementById(id).value); return isNaN(v) ? 0 : v; }
        function fieldChanged(key) {
            var el = document.getElementById("f_" + key), changes = {};
            changes[key] = el.type === "number" ? (key === "rpm" ? Math.round(num("f_" + key)) : num("f_" + key)) : el.value;
            // EY = out × TDS / dose, as the app computes it.
            if (key === "doseWeightG" || key === "finalWeightG" || key === "drinkTds") {
                var dose = key === "doseWeightG" ? changes[key] : shot.doseWeightG, out = key === "finalWeightG" ? changes[key] : shot.finalWeightG,
                    tds = key === "drinkTds" ? changes[key] : shot.drinkTds;
                if (dose > 0 && out > 0 && tds > 0) changes.drinkEy = Math.round(out * tds / dose * 10) / 10;
            }
            save(changes);
        }
        function setRating(v) { save({ enjoyment: Math.max(0, Math.min(100, v)) }); }
        function pickChip(key, v) { var c = {}; c[key] = shot[key] === v ? "" : v; save(c); }
        // The server's own words on failure: JSON {"error"} or the guard's plain text.
        function readJson(r) {
            return r.text().then(function(t) {
                var d; try { d = JSON.parse(t); } catch (e) { d = { error: t }; }
                if (!r.ok || (d && d.error)) throw new Error((d && d.error) || ("Server error (" + r.status + ")"));
                return d;
            });
        }
        // Saves go out one after another, in the order they were made, and the page
        // shows each edit at once: an Undo pressed while a save is still in flight
        // then reverts that edit rather than reading the value it replaced as current.
        var saveChain = Promise.resolve();
        function save(changes, opts) {
            var data = {}, before = {}, any = false;
            Object.keys(changes).forEach(function(k) {
                if (changes[k] === shot[k]) return;
                any = true;
                data[META[k] || k] = changes[k];
                before[k] = shot[k];
            });
            if (!any) return;
            var undoable = !(opts && opts.noUndo);
            if (undoable) { undoStack.push(before); renderUndo(); }
            Object.keys(before).forEach(function(k) { shot[k] = changes[k]; });
            syncInputs();
            setHeader();
            // A bag or package pick is resolved server-side: the bean or grinder fields
            // come from the bag or package on re-read.
            var linksChanged = "bagId" in before || "equipmentId" in before;
            saveChain = saveChain.then(function() {
                return fetch("/api/shot/" + shot.id + "/metadata", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(data) })
                    .then(readJson)
                    .then(function(result) {
                        if (!result.success) throw new Error(result.error || "Unknown error");
                        showToast(opts && opts.toast ? opts.toast : "Saved");
                        if (linksChanged) refreshShot(); else refreshOutcome();
                    })
                    .catch(function(err) {
                        // Only what a later edit has not replaced goes back.
                        Object.keys(before).forEach(function(k) { if (shot[k] === changes[k]) shot[k] = before[k]; });
                        var i = undoStack.lastIndexOf(before);
                        if (undoable && i >= 0) undoStack.splice(i, 1);
                        if (opts && opts.onFail) opts.onFail();
                        renderUndo();
                        syncInputs();
                        setHeader();
                        showToast("Save failed: " + err.message, 4000);
                    });
            });
        }
        // Inputs show the saved values: EY after an auto-calculation, every field after Undo.
        function syncInputs() {
            ["doseWeightG", "finalWeightG", "grinderSetting", "rpm", "drinkTds", "drinkEy", "espressoNotes", "barista"].forEach(function(k) {
                var el = document.getElementById("f_" + k);
                if (!el || document.activeElement === el) return;
                el.value = el.type === "number" ? (shot[k] > 0 ? shot[k] : "") : (shot[k] || "");
            });
            var r = document.getElementById("f_enjoyment"); if (r) { r.value = shot.enjoyment || 0; document.getElementById("ratingPct").textContent = (shot.enjoyment || 0) + "%"; }
            document.querySelectorAll(".rating-row .preset").forEach(function(b) { b.classList.toggle("on", parseInt(b.textContent) === shot.enjoyment); });
            ["tasteBalance", "tasteBody"].forEach(function(key) {
                document.querySelectorAll(".taste button").forEach(function(b) {
                    var m = b.getAttribute("onclick").match(/pickChip\("(\w+)","(\w+)"\)/);
                    if (m && m[1] === key) { var on = shot[key] === m[2]; b.classList.toggle("on", on); b.setAttribute("aria-pressed", on); }
                });
            });
        }
        function undoLast() {
            if (!undoStack.length) return;
            var before = undoStack.pop();
            // A failed undo keeps its frame, so the step can be tried again.
            save(before, { noUndo: true, onFail: function() { undoStack.push(before); } });
            renderUndo();
        }
        function renderUndo() {
            var b = document.getElementById("undoBtn");
            if (b) b.style.display = undoStack.length ? "" : "none";
        }

        // --- Results follow a save: the Δs and summary are against the previous shot ---
        function setOutcome(o) {
            page.outcome = o || {};
            cmp = (page.outcome && page.outcome.comparison) || {};
            cmpShots = cmp.shots || [];
            me = cmpShots.length - 1;
            since = (cmp.comparisons || [])[0] || null;
        }
        function refreshOutcome() {
            fetch("/api/shot/" + shot.id + "/outcome").then(readJson).then(function(o) {
                setOutcome(o);
                renderHappened();
            }).catch(function(e) { showToast("Could not reload the results: " + e.message, 4000); });
        }
        // After a bag or equipment pick the shot's own fields changed server-side.
        function refreshShot() {
            fetch("/api/shot/" + shot.id).then(readJson).then(function(s) {
                ["beanBrand", "beanType", "roastDate", "roastLevel", "bagId", "beanBaseJson", "beanBaseId", "frozenDate", "defrostDate", "storageHint", "openedDate",
                 "grinderBrand", "grinderModel", "grinderBurrs", "basketBrand", "basketModel",
                 "puckPrep", "equipmentName", "equipmentId", "grinderSetting", "rpm"].forEach(function(k) { if (s[k] !== undefined) shot[k] = s[k]; });
                renderDetails();
                setHeader();
                refreshOutcome();
            }).catch(function(e) { showToast("Could not reload the shot: " + e.message, 4000); });
        }

        // --- Pickers: the bag and equipment lists the app's dialogs show ---
        function pickerModal(title, items, onPick) {
            var m = document.getElementById("pickerModal");
            document.getElementById("pickerTitle").textContent = title;
            var list = document.getElementById("pickerList");
            list.innerHTML = items.length ? "" : "<div class='note'>Nothing to choose from.</div>";
            items.forEach(function(it) {
                var b = document.createElement("button");
                b.className = "picker-item" + (it.active ? " active" : "");
                b.innerHTML = "<span>" + escapeHtml(it.label) + "</span>" + (it.sub ? "<span class='note'>" + escapeHtml(it.sub) + "</span>" : "");
                b.onclick = function() { closeModal("pickerModal"); onPick(it); };
                list.appendChild(b);
            });
            m.classList.add("open");
        }
        function pickBag() {
            fetch("/api/bags").then(readJson).then(function(d) {
                var items = (d.bags || []).map(function(b) {
                    return { bag: b, active: b.isActive, label: [b.roasterName, b.coffeeName].filter(Boolean).join(" · ") || "Bag " + b.id,
                             sub: [b.roastDate, b.roastLevel].filter(Boolean).join(" · ") };
                });
                // The bag's snapshot is written by the server (CoffeeBag::shotSnapshot), as
                // for the app's Change Beans.
                pickerModal("Beans", items, function(it) { save({ bagId: it.bag.id }, { toast: "Beans changed" }); });
            }).catch(function(e) { showToast("Could not load bags: " + e.message, 4000); });
        }
        function pickEquipment() {
            fetch("/api/equipment").then(readJson).then(function(d) {
                var items = (d.equipment || []).map(function(p) {
                    return { pkg: p, active: p.isActive, label: p.name || [p.grinderBrand, p.grinderModel].filter(Boolean).join(" ") || "Package " + p.id,
                             sub: [p.grinderBurrs, [p.basketBrand, p.basketModel].filter(Boolean).join(" ")].filter(Boolean).join(" · ") };
                });
                pickerModal("Equipment", items, function(it) { save({ equipmentId: it.pkg.id }, { toast: "Equipment changed" }); });
            }).catch(function(e) { showToast("Could not load equipment: " + e.message, 4000); });
        }
        // --- Actions ---
        function uploadNow() {
            fetch("/api/shot/" + shot.id + "/upload", { method: "POST" })
                .then(readJson)
                .then(function() { showToast("Upload started. Reload the page for the result.", 4000); })
                .catch(function(e) { showToast("Could not start the upload: " + e.message, 4000); });
        }
        function saveAsRecipe() { createRecipeFromShot(shot.id, function(m) { showToast(m, 4000); }); }
        function toggleDebugLog() {
            var c = document.getElementById("debugLog");
            c.style.display = c.style.display === "none" ? "block" : "none";
            if (c.style.display === "block") c.scrollIntoView({ behavior: "smooth" });
        }
        function copyDebugLog() {
            // execCommand rather than the clipboard API, which needs a secure context.
            var ta = document.createElement("textarea");
            ta.value = document.getElementById("debugLogContent").textContent;
            ta.style.position = "fixed"; ta.style.opacity = "0";
            document.body.appendChild(ta); ta.select();
            try { document.execCommand("copy"); } catch (err) { alert("Failed to copy: " + err); }
            document.body.removeChild(ta);
        }
        function openModal(id) { document.getElementById(id).classList.add("open"); }
        function closeModal(id) { document.getElementById(id).classList.remove("open"); }
        document.getElementById("summaryLines").innerHTML = shot.summaryLines.length
            ? shot.summaryLines.map(function(l) {
                  return "<div class='summary-line " + escapeHtml(l.type) + "'><span class='line-dot'></span><span>" + escapeHtml(l.text) + "</span></div>";
              }).join("")
            : "<div class='summary-line'><span>No summary available.</span></div>";

        function deleteShot() {
            var btn = document.getElementById("deleteConfirm");
            btn.disabled = true; btn.textContent = "Deleting...";
            fetch("/api/shots/delete", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ ids: [shot.id] }) })
                .then(readJson)
                .then(function(d) {
                    // 200 with deleted:0 is a row that was not removed.
                    if (!(d.deleted > 0)) throw new Error("The shot was not deleted");
                    location.href = page.olderId > 0 ? "/shot/" + page.olderId : (page.newerId > 0 ? "/shot/" + page.newerId : "/");
                })
                .catch(function(err) { alert("Delete failed: " + err.message); btn.disabled = false; btn.textContent = "Delete"; });
        }

        function graphTraces() { return [{ curves: page.graph.curves, phases: page.graph.phases, offset: 0, hidden: false }]; }
        var graphExtraChip = null;

        setHeader();
        graphInit("shotChart");
        renderDetails();
    </script>
</body>
</html>
)HTML");
    return html;
}

// One shot's curves and phase markers in the shape WEB_JS_SHOT_GRAPH draws:
// [t, v] pairs at 0.01 precision. A goal curve breaks (null) across a gap of
// more than half a second, where the profile had no goal, rather than drawing a
// line through it.
QJsonObject ShotServer::graphTraceJson(const ShotRecord& r, bool withGoals)
{
    auto round2 = [](double v) { return std::round(v * 100) / 100; };
    auto curve = [&](const QVector<QPointF>& points) {
        QJsonArray out;
        for (const QPointF& p : points) out.append(QJsonArray{ round2(p.x()), round2(p.y()) });
        return out;
    };
    auto goal = [&](const QVector<QPointF>& points) {
        QJsonArray out;
        double lastX = -1;
        for (const QPointF& p : points) {
            if (lastX >= 0 && p.x() - lastX > 0.5) out.append(QJsonArray{ round2((lastX + p.x()) / 2), QJsonValue() });
            out.append(QJsonArray{ round2(p.x()), round2(p.y()) });
            lastX = p.x();
        }
        return out;
    };
    QJsonObject curves{
        { "pressure", curve(r.pressure) }, { "flow", curve(r.flow) },
        { "temp", curve(r.temperature) }, { "weight", curve(r.weight) },
        { "weightFlow", curve(r.weightFlowRate) }, { "resistance", curve(r.resistance) },
        { "darcyR", curve(r.darcyResistance) }, { "conductance", curve(r.conductance) },
        { "dCdt", curve(r.conductanceDerivative) }, { "mixTemp", curve(r.temperatureMix) },
        { "mixTempGoal", curve(r.temperatureMixGoal) },
    };
    if (withGoals) {
        curves[QStringLiteral("pressureGoal")] = goal(r.pressureGoal);
        curves[QStringLiteral("flowGoal")] = goal(r.flowGoal);
    }
    QJsonArray phases;
    for (const auto& ph : r.phases) {
        if (ph.label == QLatin1String("Start")) continue;
        phases.append(QJsonObject{ { "time", ph.time }, { "label", ph.label }, { "reason", ph.transitionReason } });
    }
    return QJsonObject{ { QStringLiteral("curves"), curves }, { QStringLiteral("phases"), phases } };
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

    QJsonArray shotData;
    QList<ShotProjection> projections;
    for (const ShotRecord& r : std::as_const(shots)) {
        projections.append(ShotHistoryStorage::convertShotRecord(r));
        QJsonObject shot = graphTraceJson(r, false);
        shot[QStringLiteral("id")] = r.summary.id;
        shot[QStringLiteral("date")] = ShotHistoryStorage::shortDateTime(r.summary.timestamp);
        shot[QStringLiteral("pourStartSec")] = r.cachedAnalysis ? r.cachedAnalysis->detectors.pourStartSec : 0.0;
        shotData.append(shot);
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

    // The app's own icons, drawn in the text colour, so the two pages cannot drift.
    auto icon = [&](const QString& path) {
        QFile f(path);
        QString svg = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        svg.replace(QStringLiteral("\"white\""), QStringLiteral("\"currentColor\""));
        return embedJson(QJsonArray{ svg }).mid(1).chopped(1);   // the JSON string literal alone
    };

    QString html = QStringLiteral(R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Compare Shots - Decenza</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
    <style>)HTML");
    html += QString::fromLatin1(WEB_CSS_VARIABLES) + WEB_CSS_HEADER + WEB_CSS_MENU
          + QString::fromUtf8(WEB_CSS_SHOT_GRAPH) + QString::fromUtf8(WEB_CSS_COMPARISON_TEXT);
    html += QStringLiteral(R"HTML(
        :root { --surface2: #1c2129; --down: #4e85f4; --warn: #ffaa00; }
        .header { padding: 0.75rem 1.5rem; }
        .header-content { max-width: 1600px; gap: 1rem; }
        .menu-wrapper { margin-left: auto; }

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
        .row { align-items: baseline; padding: 0.35rem 0; border-top: 1px solid rgba(48,54,61,0.6); font-variant-numeric: tabular-nums; }
        .row .label { color: var(--text-secondary); font-size: 0.875rem; }
        .row .label .unit { font-size: 0.75rem; opacity: 0.7; margin-left: 0.2rem; }
        .row .muted { color: var(--text-secondary); }
        .row .warn { color: var(--warn); }
        /* Phone: each label takes its own line, so the value columns get the width. */
        @media (max-width: 600px) {
            .header { padding: 0.75rem 1rem; }
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
    html += QStringLiteral("        var shots = ") + embedJson(shots)
          + QStringLiteral(";\n        var comparisons = ") + embedJson(data.value(QStringLiteral("comparisons")))
          + QStringLiteral(";\n        var texts = ") + embedJson(ShotComparisonText::toJson())
          + QStringLiteral(";\n        var dialInLabels = ") + embedJson(QJsonObject::fromVariantMap(ProfileDialInText::labelMap()))
          + QStringLiteral(";\n        var puckFlags = ") + embedJson(QJsonArray::fromVariantList(EquipmentStorage::puckPrepFlags()))
          + QStringLiteral(";\n        var icons = { eye: ") + icon(QStringLiteral(":/icons/eye.svg"))
          + QStringLiteral(", eyeOff: ") + icon(QStringLiteral(":/icons/eye-off.svg")) + QStringLiteral(" };\n");
    html += QString::fromLatin1(WEB_JS_ESCAPE_HTML);
    html += QString::fromLatin1(WEB_JS_MENU);
    html += QString::fromLatin1(WEB_JS_POWER_CONTROL);
    html += QString::fromUtf8(WEB_JS_COMPARISON_TEXT);
    html += QString::fromUtf8(WEB_JS_SHOT_GRAPH);
    html += QStringLiteral(R"HTML(
        // === State ===
        var base = 0;                 // index into shots (oldest first)
        var hiddenShots = {};          // by shot id
        var alignPours = false;
        var showMore = false;

        var indexById = {};
        shots.forEach(function(s, i) { indexById[s.id] = i; });

        function cmp() { return comparisons[base]; }
        // Shot indices in column order: the base, then the rest oldest first.
        function columns() { return cmp().shots.map(function(s) { return indexById[s.shotId]; }); }
        function offsetFor(si) {
            if (!alignPours) return 0;
            var b = shots[base].pourStartSec, o = shots[si].pourStartSec;
            return b > 0 && o > 0 ? b - o : 0;
        }
        function graphTraces() {
            return columns().map(function(si) {
                var s = shots[si];
                return { curves: s.curves, phases: s.phases, offset: offsetFor(si), hidden: !!hiddenShots[s.id] };
            });
        }
        var graphExtraChip = { label: txt("ui.alignPours"), tip: txt("tip.alignPours"),
                               on: function() { return alignPours; }, toggle: function() { alignPours = !alignPours; } };
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
                   + graphSwatch(col) + "<span class='date'>" + escapeHtml(s.date) + "</span>"
                   + (col === 0 ? "<span class='tag'>" + escapeHtml(txt("ui.base")) + "</span>" : "")
                   + "<button class='eye" + (hidden ? " off" : "") + "' title='" + escapeHtml(txt(hidden ? "ui.showOnGraph" : "ui.hideOnGraph"))
                   + "' aria-pressed='" + !hidden + "' onclick='event.stopPropagation();toggleShot(" + s.id + ")'>"
                   + (hidden ? icons.eyeOff : icons.eye) + "</button></div></div>";
            });
            h += "</div>";

            c.comparisons.forEach(function(cc, i) {
                h += "<div class='summary'>" + graphSwatch(i + 1) + "<span>" + escapeHtml(summaryFor(cc)) + "</span></div>";
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

        // The last two phases on: the pour, where shots differ, without a wall of markers.
        var allPhases = [];
        shots.forEach(function(s) { s.phases.forEach(function(p) { if (allPhases.indexOf(p.label) < 0) allPhases.push(p.label); }); });
        allPhases.slice(0, -2).forEach(function(p) { hiddenPhases[p] = true; });
        graphInit("compareChart");
        renderComparison();
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

