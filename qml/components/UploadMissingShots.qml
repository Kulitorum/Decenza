import QtQuick
import QtQuick.Layouts
import Decenza

// Upload missing shots on a destination card (D14): shown only while the
// destination is missing shots, with how many and how many failed; while the run
// goes, its progress in place of the button.
ColumnLayout {
    id: root

    // ShotUploadDestination::name(): "visualizer" or "decent".
    property string destination
    // A line under the button about where the shots go, if the destination needs one.
    property string note

    readonly property var entry: MainController.shotUploads ? MainController.shotUploads.missing[root.destination] : undefined
    readonly property bool running: root.entry !== undefined && root.entry.running === true
    readonly property int count: root.entry !== undefined ? (root.entry.count ?? 0) : 0
    readonly property int failed: root.entry !== undefined ? (root.entry.failed ?? 0) : 0

    visible: root.running || root.count > 0
    spacing: Theme.spacingSmall

    AccessibleButton {
        visible: !root.running
        text: TranslationManager.translate("settings.upload.missing.button", "Upload missing shots (%1)").arg(root.count)
        accessibleName: TranslationManager.translate("settings.upload.missing.accessible",
                                                     "Upload %1 missing shots").arg(root.count)
        onClicked: MainController.shotUploads.uploadMissing(root.destination)
    }

    Text {
        Layout.fillWidth: true
        visible: text.length > 0
        wrapMode: Text.WordWrap
        font: Theme.captionFont
        color: root.failed > 0 && !root.running ? Theme.errorColor : Theme.textSecondaryColor
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        text: root.running && root.entry.resumeAtMs
              ? TranslationManager.translate("settings.upload.missing.rateLimited",
                                             "%1 of %2 done. The server asked to slow down; continuing at %3")
                    .arg(root.entry.done ?? 0).arg(root.entry.total ?? 0)
                    .arg(Theme.clockTime(new Date(root.entry.resumeAtMs)))
              : root.running
              ? TranslationManager.translate("settings.upload.missing.progress", "Uploading %1 of %2")
                    .arg(root.entry.done ?? 0).arg(root.entry.total ?? 0)
              : root.failed > 0
                ? TranslationManager.translate("settings.upload.missing.failed", "%1 of them could not be uploaded before")
                      .arg(root.failed)
                : ""
    }

    Text {
        Layout.fillWidth: true
        visible: root.note.length > 0
        text: root.note
        wrapMode: Text.WordWrap
        font: Theme.captionFont
        color: Theme.textSecondaryColor
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
}
