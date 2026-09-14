// Declares no delegate/injected model role itself (the picker's GridView/Row
// delegates hold `entry` as a required property and pass it straight through),
// but `layer.effect` blocks below declare inline Components, so ids from this
// file are not statically resolvable inside them without this pragma.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

// One profile card, shared by ProfilePicker's tier rows and its "All" grid, in
// both hosts (Profiles page, recipe wizard). See profile-picker spec "Card
// contents" / "Card actions". Fixed height so a grid row never staggers.
Rectangle {
    id: card

    // {name (filename), title, beverageType, espressoTemperature, targetWeight,
    // source (0=BuiltIn/1=Downloaded/2=UserCreated), hasKnowledgeBase,
    // kbDerivedFrom, readOnly} — ProfileManager::profileInfoToVariantMap's shape.
    required property var entry
    // Tier ①/② recommendation reason, rendered as a chip. "" = no chip (grid cards).
    property string reason: ""
    // Highlights the current machine profile (profile-picker "Card contents").
    property bool current: false

    signal chosen()
    signal longPressed()
    signal sparkleRequested()
    signal infoRequested()
    signal overflowRequested()

    readonly property int sourceValue: entry && entry.source !== undefined ? entry.source : 0
    readonly property bool isBuiltIn: sourceValue === 0
    readonly property bool isDownloaded: sourceValue === 1
    readonly property color sourceColor: isBuiltIn ? Theme.sourceBadgeBlueColor
                                         : isDownloaded ? Theme.sourceBadgeGreenColor
                                         : Theme.sourceBadgeOrangeColor
    readonly property string sourceLetter: isBuiltIn ? "D" : (isDownloaded ? "V" : "U")

    readonly property bool isSelected: entry ? ProfileManager.isProfileInSelectedList(entry.name) : false
    readonly property bool isFavorite: entry ? Settings.app.isFavoriteProfile(entry.name) : false
    readonly property bool isAutoLoad: entry && entry.name !== undefined
        && Settings.app.autoLoadProfileFilename !== ""
        && entry.name === Settings.app.autoLoadProfileFilename
    readonly property bool isCurrentModified: card.current && ProfileManager.profileModified

    // {lastTimestamp, count} for this title, or undefined when never used.
    readonly property var usage: entry ? (ProfileManager.profileUsage[entry.title] || null) : null

    readonly property string metaLine: {
        var parts = []
        if (entry && (entry.espressoTemperature || 0) > 0)
            parts.push(Theme.formatTemperature(entry.espressoTemperature, 0))
        if (entry && (entry.targetWeight || 0) > 0)
            parts.push("→ " + Number(entry.targetWeight).toFixed(0) + "g")
        return parts.join(" · ")
    }

    readonly property string usageLine: {
        if (!usage || !usage.lastTimestamp)
            return TranslationManager.translate("profilepicker.usage.never", "Never used")
        var count = usage.count || 0
        var shots = count === 1
            ? TranslationManager.translate("profilepicker.usage.one_shot", "1 shot")
            : TranslationManager.translate("profilepicker.usage.n_shots", "%1 shots").arg(count)
        return shots + " · " + card.ageText(usage.lastTimestamp)
    }

    function ageText(lastTimestampSec) {
        var deltaSec = Math.max(0, Date.now() / 1000 - lastTimestampSec)
        var days = deltaSec / 86400
        if (days >= 1)
            return TranslationManager.translate("profilepicker.usage.days_ago", "%1 d ago").arg(Math.floor(days))
        var hours = deltaSec / 3600
        if (hours >= 1)
            return TranslationManager.translate("profilepicker.usage.hours_ago", "%1 h ago").arg(Math.floor(hours))
        var minutes = Math.max(1, Math.floor(deltaSec / 60))
        return TranslationManager.translate("profilepicker.usage.minutes_ago", "%1 m ago").arg(minutes)
    }

    implicitWidth: Theme.scaled(230)
    implicitHeight: Theme.scaled(168)
    radius: Theme.cardRadius
    color: Theme.cardBackgroundColor
    border.color: card.current ? Theme.primaryColor : Theme.borderColor
    border.width: card.current ? 2 : 1

    // Computed once and read by both the Accessible.name below and the
    // card-wide tap area's AccessibleMouseArea — one string, not two.
    readonly property string accessibleSummary: {
        var bits = [card.sourceLetter === "D"
            ? TranslationManager.translate("profileselector.accessible.source_decent", "Decent")
            : card.sourceLetter === "V"
            ? TranslationManager.translate("profileselector.accessible.source_downloaded", "Downloaded")
            : TranslationManager.translate("profileselector.accessible.source_custom", "Custom")]
        bits.push(card.entry ? card.entry.title : "")
        if (card.isFavorite) bits.push(TranslationManager.translate("profileselector.accessible.favorite", "favorite"))
        if (card.isCurrentModified) bits.push(TranslationManager.translate("profileselector.accessible.unsaved_changes", "unsaved changes"))
        if (card.current) bits.push(TranslationManager.translate("profileselector.accessible.currently_selected", "currently selected"))
        bits.push(card.usageLine)
        if (card.reason !== "") bits.push(card.reason)
        return bits.join(", ")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.scaled(10)
        spacing: Theme.scaled(4)

        // Row 1: source letter, selected check, title(+modified), pin, sparkle.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(4)

            Text {
                text: card.sourceLetter
                font.pixelSize: Theme.scaled(14)
                font.bold: true
                color: card.sourceColor
                Accessible.ignored: true
            }

            Image {
                visible: card.isSelected
                source: "qrc:/icons/box-checked.svg"
                sourceSize.width: Theme.scaled(13)
                sourceSize.height: Theme.scaled(13)
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect { colorization: 1.0; colorizationColor: Theme.textSecondaryColor }
            }

            Text {
                Layout.fillWidth: true
                text: {
                    var name = card.entry ? card.entry.title : ""
                    if (card.isCurrentModified) {
                        return ProfileManager.isCurrentProfileReadOnly
                            ? name + " " + TranslationManager.translate("profileselector.modified_suffix", "(modified)")
                            : "*" + name
                    }
                    return name
                }
                color: Theme.textColor
                font: Theme.bodyFont
                elide: Text.ElideRight
                maximumLineCount: 1
                Accessible.ignored: true
            }

            Image {
                visible: card.isAutoLoad
                source: "qrc:/icons/pin.svg"
                sourceSize.width: Theme.scaled(13)
                sourceSize.height: Theme.scaled(13)
                Accessible.role: Accessible.Indicator
                Accessible.name: TranslationManager.translate("profileselector.accessible.auto_load_profile", "Auto-load profile")
                Accessible.ignored: !card.isAutoLoad
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect { colorization: 1.0; colorizationColor: Theme.primaryColor }
            }

            Image {
                id: sparkleIcon
                visible: card.entry ? card.entry.hasKnowledgeBase === true : false
                source: "qrc:/icons/sparkle.svg"
                sourceSize.width: Theme.scaled(13)
                sourceSize.height: Theme.scaled(13)
                opacity: sparkleArea.containsMouse ? 1.0 : 0.6
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect { colorization: 1.0; colorizationColor: Theme.textSecondaryColor }

                AccessibleMouseArea {
                    id: sparkleArea
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(-5)
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    accessibleName: TranslationManager.translate("profileselector.accessible.view_knowledge", "View AI knowledge base")
                    accessibleItem: sparkleIcon
                    onAccessibleClicked: card.sparkleRequested()
                }
            }
        }

        Text {
            visible: card.metaLine !== ""
            text: card.metaLine
            font: Theme.captionFont
            color: Theme.textSecondaryColor
            Accessible.ignored: true
        }

        // Reason chip (tier cards) OR the usage line — never both; a tier
        // card's reason is more useful than "Never used" on the same profile.
        Text {
            visible: card.reason === ""
            text: card.usageLine
            font: Theme.captionFont
            color: Theme.textSecondaryColor
            Accessible.ignored: true
        }
        Rectangle {
            visible: card.reason !== ""
            radius: height / 2
            color: Qt.alpha(Theme.primaryColor, 0.15)
            implicitHeight: reasonLabel.implicitHeight + Theme.scaled(6)
            implicitWidth: Math.min(reasonLabel.implicitWidth + Theme.scaled(14), card.width - Theme.scaled(20))
            Text {
                id: reasonLabel
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - Theme.scaled(10))
                text: card.reason
                font: Theme.captionFont
                color: Theme.primaryColor
                elide: Text.ElideRight
                Accessible.ignored: true
            }
        }

        // Derivation caption — a FIXED-height row so cards without one still
        // reserve the same space (profile-picker "Derived caption" scenario).
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.captionFont.pixelSize * 1.3
            KbDerivedFromLabel {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                derivedFrom: card.entry ? (card.entry.kbDerivedFrom || "") : ""
            }
        }

        Item { Layout.fillHeight: true }

        // Bottom action row: info, star, overflow.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(2)

            Item { Layout.fillWidth: true }

            ProfileInfoButton {
                Layout.preferredWidth: Theme.scaled(26)
                Layout.preferredHeight: Theme.scaled(26)
                buttonSize: Theme.scaled(26)
                profileFilename: card.entry ? card.entry.name : ""
                profileName: card.entry ? card.entry.title : ""
                onClicked: card.infoRequested()
            }

            StyledIconButton {
                Layout.preferredWidth: Theme.scaled(32)
                Layout.preferredHeight: Theme.scaled(32)
                enabled: card.isFavorite || Settings.app.favoriteProfiles.length < 50
                icon.source: card.isFavorite ? "qrc:/icons/star.svg" : "qrc:/icons/star-outline.svg"
                active: card.isFavorite
                accessibleName: card.isFavorite
                    ? TranslationManager.translate("profileselector.accessible.remove_from_favorites", "Remove from favorites")
                    : TranslationManager.translate("profileselector.accessible.add_to_favorites", "Add to favorites")
                onClicked: ProfileManager.toggleFavoriteProfile(card.entry ? card.entry.name : "")
            }

            StyledIconButton {
                Layout.preferredWidth: Theme.scaled(32)
                Layout.preferredHeight: Theme.scaled(32)
                icon.source: "qrc:/icons/more-vertical.svg"
                inactiveColor: Theme.textColor
                accessibleName: TranslationManager.translate("profileselector.accessible.more_options", "More options for")
                    + " " + (card.entry ? card.entry.title : "")
                onClicked: card.overflowRequested()
            }
        }
    }

    // The card-wide tap target and the card's ONE accessible node (a second
    // on the root would give screen readers two stops per card): choose on
    // tap, preview on long-press.
    CardTapArea {
        supportLongPress: true
        cursorShape: Qt.PointingHandCursor
        accessibleName: card.accessibleSummary
        accessibleItem: card
        onAccessibleClicked: card.chosen()
        onAccessibleLongPressed: card.longPressed()
    }
}
