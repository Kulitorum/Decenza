import QtQuick
import QtQuick.Layouts
import Decenza

// A card on a settings tab, and the only way to put one there: its title is both the header and
// its settings-search result. scripts/settings_search_index.py reads searchId, title,
// description, keywords and availability from the QML source, so they must be literals.
Rectangle {
    id: card

    required property string searchId
    required property string title
    property string description: ""
    property list<string> keywords
    // A SettingsSearchRegistry condition. Decides visibility here and in search together.
    property string availability: ""
    // Runtime visibility (e.g. "only while the refill kit is off"). Use this, never `visible`,
    // which would override availability.
    property bool shown: true
    property bool showHeader: true
    // For a card laid out to fill its column (Layout.fillHeight): the content fills the card,
    // e.g. so a Flickable inside it can scroll.
    property bool fillContent: false
    property real contentMargins: Theme.scaled(15)
    property alias spacing: column.spacing
    // Dims the content, not the card (e.g. a setting unavailable on this machine).
    property alias contentOpacity: column.opacity
    default property alias content: column.data

    objectName: searchId
    visible: shown && SettingsSearchRegistry.isAvailable(availability)
    Layout.fillWidth: true
    implicitHeight: column.implicitHeight + 2 * contentMargins
    color: Theme.cardBackgroundColor
    radius: Theme.cardRadius

    // left/right/top unless fillContent: implicitHeight comes from this column, and anchoring the
    // bottom too derives the column's height back from the card, which does not settle once a
    // wrapping Text is in it. A fill card's height comes from its layout instead.
    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: card.fillContent ? parent.bottom : undefined
        anchors.margins: card.contentMargins
        spacing: Theme.scaled(8)

        Text {
            visible: card.showHeader
            Layout.fillWidth: true
            text: card.title
            color: Theme.textColor
            font.family: Theme.bodyFont.family
            font.pixelSize: Theme.scaled(16)
            font.bold: true
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
    }
}
