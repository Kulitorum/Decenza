import QtQuick
import Decenza

// The one wording for a Decent upload's progress and problems; success shows
// nothing, as for Visualizer (the Upload button's fill says it is in sync). With
// shotId set, problems show only for that shot; progress shows for any upload.
Text {
    id: root

    property var shotId: 0

    readonly property var uploader: ShotReviewActions
    readonly property bool forThisShot: !root.shotId || (root.uploader && root.uploader.decentLastShotId === root.shotId)
    readonly property string result: root.uploader ? root.uploader.decentResultCode : "none"

    visible: text.length > 0
    wrapMode: Text.WordWrap
    font: Theme.captionFont
    color: root.uploader && root.uploader.decentUploading ? Theme.textSecondaryColor : Theme.errorColor
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    text: {
        if (!root.uploader || !root.uploader.ready) return ""
        if (root.uploader.decentUploading)
            return TranslationManager.translate("decent.upload.uploading", "Uploading to your Decent account…")
        if (!root.forThisShot) return ""
        switch (root.result) {
        case "notReplaced":
            return TranslationManager.translate("decent.upload.notReplaced",
                "Decent kept its earlier copy — the edit was not saved")
        case "maintenance":
            return TranslationManager.translate("decent.upload.maintenance", "Cleaning and descaling cycles are not uploaded")
        case "tooShort":
            return TranslationManager.translate("decent.upload.tooShort", "Shorter than the minimum shot length — not uploaded")
        case "notLinked":
            return TranslationManager.translate("decent.upload.notLinked", "Connect your Decent account in Settings first")
        case "noMachine":
            return TranslationManager.translate("decent.upload.noMachine",
                "Connect your DE1 first — shots are filed under its serial number")
        case "noSerial":
            return TranslationManager.translate("decent.upload.noSerial",
                "Your DE1 does not report its serial number — sign out of your Decent account and sign in again")
        case "notFound":
            return TranslationManager.translate("decent.upload.notFound", "This shot could not be loaded")
        case "rejected":
            return TranslationManager.translate("decent.upload.rejected", "Decent did not accept this shot (HTTP %1)")
                .arg(root.uploader.decentLastHttpStatus)
        case "needsSignIn":
            return TranslationManager.translate("decent.upload.signIn", "Sign in to your Decent account again")
        case "notRegistered":
            return TranslationManager.translate("decent.upload.notRegistered",
                "Serial number %1 is not registered to your Decent account").arg(root.uploader.decentLastSerial)
        case "failed":
            if (root.uploader.decentLastHttpStatus > 0)
                return TranslationManager.translate("decent.upload.serverProblem",
                    "decentespresso.com had a problem (HTTP %1) — try again later").arg(root.uploader.decentLastHttpStatus)
            return TranslationManager.translate("decent.upload.failed",
                "Could not reach decentespresso.com — try again later")
        }
        return ""
    }
}
