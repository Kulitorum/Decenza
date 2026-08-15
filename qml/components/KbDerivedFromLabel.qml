import QtQuick
import Decenza

// "Based on X" — the one-line note shown where a profile reached its knowledge
// by SHAPE rather than by name, so the user can tell the relationship was
// inferred from the profile's structure and not read off its title.
//
// ONE definition for all three surfaces that show it (the profile selector
// delegate, ShotDetailPage, PostShotReviewPage). Each of them carried its own
// copy of the translation key, its English fallback, the caption styling and
// the accessibility wiring — three chances for the key to drift from the
// string it renders, with nothing failing when one did.
//
// An empty `derivedFrom` hides the label, and covers three distinct cases that
// all mean "show nothing": the profile resolved by title (its own name already
// says where it came from), it resolved to nothing, and its shape matched
// several entries at once (no single entry to name).
//
// Layout sizing belongs to the caller: each surface bounds this differently
// against its own width.
Text {
    id: kbDerivedFromLabel

    // Canonical display name of the KB entry the knowledge came from, or "".
    property string derivedFrom: ""

    visible: kbDerivedFromLabel.text.length > 0
    text: kbDerivedFromLabel.derivedFrom
              ? TranslationManager.translate("profileselector.based_on", "Based on %1")
                    .arg(kbDerivedFromLabel.derivedFrom)
              : ""
    color: Theme.textSecondaryColor
    font: Theme.captionFont
    elide: Text.ElideRight
    Accessible.role: Accessible.StaticText
    Accessible.name: kbDerivedFromLabel.text
    Accessible.ignored: !kbDerivedFromLabel.visible
}
