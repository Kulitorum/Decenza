.pragma library

// The sentence naming the shot a grind suggestion rests on
// (DialingBlocks::grindSuggestionFor), shared by the Genius Grind popup and the recipe
// wizard. `when` is the shot's local date and time; `tr(key, fallback)` translates.
function sourceLine(s, targetTitle, when, tr) {
    var profile = String(s.startProfileName || "")
    var setting = String(s.startShotSetting || "")
    var rating = Number(s.startRating || 0)
    switch (String(s.source || "")) {
    case "rated":
        return tr("geniusGrind.source.rated", "Your %1 shot of %2, rated %3, at %4.")
                   .arg(profile).arg(when).arg(rating).arg(setting)
    case "history":
        return tr("geniusGrind.source.history", "Your latest %1 shot of this coffee, %2, at %3.")
                   .arg(profile).arg(when).arg(setting)
    case "sameUgs":
        return tr("geniusGrind.source.sameUgs",
                  "Your %1 shot of %2, rated %3, at %4. It sits at the same UGS as %5.")
                   .arg(profile).arg(when).arg(rating).arg(setting).arg(targetTitle)
    default:
        return rating > 0
            ? tr("geniusGrind.source.derivedRated",
                 "Estimated from your %1 shot of %2, rated %3, at %4, using your grinder calibration.")
                  .arg(profile).arg(when).arg(rating).arg(setting)
            : tr("geniusGrind.source.derived",
                 "Estimated from your %1 shot of %2, at %3, using your grinder calibration.")
                  .arg(profile).arg(when).arg(setting)
    }
}
