import QtQuick
import Decenza

// The one wording for a Decent upload's progress and problems; success shows
// nothing, as for Visualizer (the Upload button's fill says it is in sync). With
// shotId set, problems show only for that shot; progress shows for any upload.
Text {
    id: root

    property var shotId: 0

    readonly property var uploader: MainController.decentUploader
    readonly property bool forThisShot: !root.shotId || (root.uploader && root.uploader.lastShotId === root.shotId)
    readonly property int result: root.uploader ? root.uploader.lastResult : DecentShotUploader.Result.None

    visible: text.length > 0
    wrapMode: Text.WordWrap
    font: Theme.captionFont
    color: root.uploader && root.uploader.uploading ? Theme.textSecondaryColor : Theme.errorColor
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    text: {
        if (!root.uploader) return ""
        if (root.uploader.uploading)
            return TranslationManager.translate("decent.upload.uploading", "Uploading to your Decent account…")
        if (!root.forThisShot) return ""
        switch (root.result) {
        case DecentShotUploader.Result.NotReplaced:
            return TranslationManager.translate("decent.upload.notReplaced",
                "Decent kept its earlier copy — the edit was not saved")
        case DecentShotUploader.Result.Maintenance:
            return TranslationManager.translate("decent.upload.maintenance", "Cleaning and descaling cycles are not uploaded")
        case DecentShotUploader.Result.TooShort:
            return TranslationManager.translate("decent.upload.tooShort", "Shorter than the minimum shot length — not uploaded")
        case DecentShotUploader.Result.NotLinked:
            return TranslationManager.translate("decent.upload.notLinked", "Connect your Decent account in Settings first")
        case DecentShotUploader.Result.NoMachine:
            return TranslationManager.translate("decent.upload.noMachine",
                "Connect your DE1 first — shots are filed under its serial number")
        case DecentShotUploader.Result.NotFound:
            return TranslationManager.translate("decent.upload.notFound", "This shot could not be loaded")
        case DecentShotUploader.Result.Rejected:
            return TranslationManager.translate("decent.upload.rejected", "Decent did not accept this shot (HTTP %1)")
                .arg(root.uploader.lastHttpStatus)
        case DecentShotUploader.Result.NeedsSignIn:
            return TranslationManager.translate("decent.upload.signIn", "Sign in to your Decent account again")
        case DecentShotUploader.Result.NotRegistered:
            return TranslationManager.translate("decent.upload.notRegistered",
                "Serial number %1 is not registered to your Decent account").arg(root.uploader.lastSerial)
        case DecentShotUploader.Result.Failed:
            if (root.uploader.lastHttpStatus > 0)
                return TranslationManager.translate("decent.upload.serverProblem",
                    "decentespresso.com had a problem (HTTP %1) — try again later").arg(root.uploader.lastHttpStatus)
            return TranslationManager.translate("decent.upload.failed",
                "Could not reach decentespresso.com — try again later")
        }
        return ""
    }
}
