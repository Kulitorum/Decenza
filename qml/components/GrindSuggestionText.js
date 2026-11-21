.pragma library

// The sentences describing a grind suggestion (DialingBlocks::grindSuggestionFor),
// shared by the Genius Grind popup and the recipe wizard so both state the same
// facts. `tr(key, fallback)` translates; the caller supplies it, as with DateUtils.

// The shot a reading rests on. `prefix` is "start" or "alt"; `source` is that
// reading's source; `when` is the shot's local date and time.
function sourceLine(s, prefix, source, targetTitle, when, tr) {
    var profile = String(s[prefix + "ProfileName"] || "")
    var setting = String(s[prefix + "ShotSetting"] || "")
    var rating = Number(s[prefix + "Rating"] || 0)
    var earlier = s[prefix + "EarlierBag"] === true
        ? " " + tr("geniusGrind.source.earlierBag", "It is from an earlier bag.")
        : ""
    switch (source) {
    case "rated":
        return tr("geniusGrind.source.rated", "Your %1 shot of %2, rated %3, at %4.")
                   .arg(profile).arg(when).arg(rating).arg(setting) + earlier
    case "history":
        return tr("geniusGrind.source.history", "Your latest %1 shot of this coffee, %2, at %3.")
                   .arg(profile).arg(when).arg(setting) + earlier
    case "sameUgs":
        return tr("geniusGrind.source.sameUgs",
                  "Your %1 shot of %2, rated %3, at %4. It sits at the same UGS as %5.")
                   .arg(profile).arg(when).arg(rating).arg(setting).arg(targetTitle) + earlier
    default:
        return (rating > 0
            ? tr("geniusGrind.source.derivedRated",
                 "Estimated from your %1 shot of %2, rated %3, at %4, using your grinder calibration.")
                  .arg(profile).arg(when).arg(rating).arg(setting)
            : tr("geniusGrind.source.derived",
                 "Estimated from your %1 shot of %2, at %3, using your grinder calibration.")
                  .arg(profile).arg(when).arg(setting)) + earlier
    }
}

// "1 day" / "N days" between the two readings' shots.
function days(n, tr) {
    return n === 1 ? tr("geniusGrind.days.one", "1 day")
                   : tr("geniusGrind.days.many", "%1 days").arg(n)
}
