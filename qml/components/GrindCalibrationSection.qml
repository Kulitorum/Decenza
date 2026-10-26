// The anchor Repeater delegate reads this file's `root`; Bound makes it resolvable. The
// delegate declares its one injected role, `modelData`, required (QML_GOTCHAS.md).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

// An equipment package's grind calibration (add-grinder-ugs-calibration): where its
// conversion key comes from, the profile range it covers, and the optional fine and
// coarse anchors. The equipment card shows it in full; the post-shot review page
// shows the status line plus the mark actions (`shotId` > 0, `compact`).
ColumnLayout {
    id: root

    property int packageId: -1
    // Review page: the shot the mark actions apply to, and the compact layout.
    property int shotId: -1
    property bool compact: false
    property var calib: ({})
    property bool calibrationLoaded: false
    readonly property bool serviceReady: EquipmentService.ready
    onServiceReadyChanged: {
        calib = ({})
        calibrationLoaded = false
        if (serviceReady) refresh()
    }

    readonly property bool _hasAnchors: !!calib.fine && !!calib.coarse
                                        && (calib.fine.shotId > 0 || calib.coarse.shotId > 0)

    spacing: Theme.scaled(4)
    visible: packageId > 0

    function refresh() {
        if (packageId > 0 && EquipmentService.ready)
            EquipmentService.requestGrindCalibration(packageId)
    }

    // One wording for every refusal and every reason an anchor does not count.
    function reasonText(code) {
        switch (code) {
        case "notEspresso": return TranslationManager.translate("grindcalibration.reason.notEspresso", "only espresso shots can be anchors")
        case "tooLight": return TranslationManager.translate("grindcalibration.reason.tooLight", "the shot weighs under 15 g")
        case "badged": return TranslationManager.translate("grindcalibration.reason.badged", "the shot has a quality warning")
        case "noSetting": return TranslationManager.translate("grindcalibration.reason.noSetting", "the shot has no numeric grind setting")
        case "noUgs": return TranslationManager.translate("grindcalibration.reason.noUgs", "the profile is not on the UGS chart")
        case "noBean": return TranslationManager.translate("grindcalibration.reason.noBean", "the shot has no bean recorded")
        case "notInPackage": return TranslationManager.translate("grindcalibration.reason.notInPackage", "the shot is no longer on this equipment")
        case "noPackage": return TranslationManager.translate("grindcalibration.reason.noPackage", "the shot has no equipment")
        case "noGrinder": return TranslationManager.translate("grindcalibration.reason.noGrinder", "the equipment has no grinder")
        case "sameShot": return TranslationManager.translate("grindcalibration.reason.sameShot", "this shot is already the other anchor")
        case "differentCoffee": return TranslationManager.translate("grindcalibration.reason.differentCoffee", "the two anchors must be on the same bag")
        case "differentRpm": return TranslationManager.translate("grindcalibration.reason.differentRpm", "the two anchors must use the same RPM")
        case "tooClose": return TranslationManager.translate("grindcalibration.reason.tooClose", "the two anchors must be at least 2 UGS apart")
        default: return TranslationManager.translate("grindcalibration.reason.unknown", "the anchor could not be saved")
        }
    }

    // Linux and Wasm use their external-browser boundary. Other platforms load
    // the native guide dialog on demand, so WebView is never required by Wasm.
    function openUgsGuide() {
        var url = calib.ugsGuideUrl || ""
        if (url === "") return
        if (Qt.platform.os === "linux" || Qt.platform.os === "wasm") {
            const reservation = guideUrlOpener.reserve()
            if (reservation) guideUrlOpener.openReserved(reservation, url)
            return
        }
        // A fresh dialog each time; it opens itself with the URL it is given, and is
        // unloaded once closed so its native web view does not stay resident.
        // Deferred: the dialog must not be destroyed inside its own closed handler.
        ugsGuideLoader.active = false
        ugsGuideLoader.setSource(Qt.resolvedUrl("UgsGuideDialog.qml"), {
            "initialUrl": url,
            "closeCallback": function() { Qt.callLater(function() { ugsGuideLoader.active = false }) }
        })
        ugsGuideLoader.active = true
    }

    function _anchorLine(a) {
        if (!a || !(a.shotId > 0))
            return TranslationManager.translate("grindcalibration.anchor.notSet", "not set")
        var when = a.timestamp > 0
            ? new Date(a.timestamp * 1000).toLocaleString(Qt.locale(), Settings.app.use12HourTime ? "MMM d, h:mm AP" : "MMM d, HH:mm")
            : ""
        return [when, a.profileName || "", a.setting || ""].filter(function(s) { return s.length > 0 }).join(" · ")
    }

    readonly property string statusText: {
        if (!serviceReady || (calib.error || "") !== "")
            return TranslationManager.translate("grindcalibration.status.unavailable", "Calibration is unavailable. Please try again.")
        if (!calibrationLoaded)
            return TranslationManager.translate("common.loading", "Loading…")
        if (!calib.hasShots)
            return TranslationManager.translate("grindcalibration.status.noShots", "No espresso shots with this equipment yet")
        if (!calib.keyPublished)
            return TranslationManager.translate("grindcalibration.status.notEnough", "Not enough data yet")
        if (calib.basis === "anchors")
            return TranslationManager.translate("grindcalibration.status.anchors", "From your anchors")
        if (calib.basis === "both")
            return calib.agreesWithHistory
                ? TranslationManager.translate("grindcalibration.status.bothAgree", "From your history and anchors, which agree")
                : TranslationManager.translate("grindcalibration.status.bothDisagree", "From your history and anchors, which disagree")
        return TranslationManager.translate("grindcalibration.status.history", "From your shot history (%1 pairs)").arg(calib.historyPairs)
    }

    readonly property string optionalText: {
        if (root._hasAnchors || !calib.hasShots) return ""
        return calib.historyKeyValid
            ? TranslationManager.translate("grindcalibration.optional.notNeeded", "Anchors are optional and not needed: your history is enough.")
            : TranslationManager.translate("grindcalibration.optional.offer", "Anchors are optional: mark a fine and a coarse shot on the same bag to calibrate.")
    }

    readonly property string pairProblemText: {
        var p = calib.pairStatus || ""
        if (p === "differentCoffee" || p === "differentRpm" || p === "tooClose" || p === "sameShot")
            return TranslationManager.translate("grindcalibration.pair.notCounted", "Anchors not used: %1").arg(reasonText(p))
        return ""
    }

    // The page owns saving pending edits before a mark, so it handles this.
    signal markRequested(string side)

    ExternalUrlOpener { id: guideUrlOpener }

    Loader {
        id: ugsGuideLoader
        active: false
    }

    onPackageIdChanged: {
        calib = ({})
        calibrationLoaded = false
        refresh()
    }
    Component.onCompleted: refresh()

    Connections {
        target: EquipmentService
        function onGrindCalibrationReady(pid, calibration) {
            if (pid !== root.packageId) return
            root.calib = calibration
            root.calibrationLoaded = true
        }
        function onGrindCalibrationChanged(pid) {
            if (pid === root.packageId) root.refresh()
        }
    }

    // Anchors are re-checked on every read, so an edit or delete of an anchor shot
    // re-reads the calibration.
    function _isAnchorShot(id) {
        return (calib.fine && calib.fine.shotId === id) || (calib.coarse && calib.coarse.shotId === id)
    }
    Connections {
        target: HistoryService
        function onShotMetadataUpdated(shotId, success) {
            if (success && root._isAnchorShot(shotId)) root.refresh()
        }
        function onShotDeleted(shotId) {
            if (root._isAnchorShot(shotId)) root.refresh()
        }
        function onHistoryDataChanged() {
            if (root._hasAnchors) root.refresh()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(6)

        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: TranslationManager.translate("grindcalibration.title", "Grind calibration") + ": " + root.statusText
            font: Theme.captionFont
            color: !root.serviceReady || (root.calib.error || "") !== ""
                   ? Theme.warningColor : Theme.textSecondaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        StyledIconButton {
            Layout.preferredWidth: Theme.scaled(32)
            Layout.preferredHeight: Theme.scaled(32)
            enabled: root.serviceReady && (root.calib.ugsGuideUrl || "") !== ""
            icon.source: "qrc:/icons/info.svg"
            accessibleName: TranslationManager.translate("grindcalibration.ugsGuide", "Open the Universal Grind Setting guide")
            onClicked: root.openUgsGuide()
        }
    }

    Text {
        Layout.fillWidth: true
        visible: !root.compact && !!root.calib.keyPublished && (root.calib.rangeFromProfile || "") !== ""
        wrapMode: Text.Wrap
        text: TranslationManager.translate("grindcalibration.range", "Grind suggestions from %1 to %2")
                  .arg(root.calib.rangeFromProfile || "").arg(root.calib.rangeToProfile || "")
        font: Theme.captionFont
        color: Theme.textSecondaryColor
    }

    Repeater {
        model: root.compact || !root.calib.hasShots ? [] : ["fine", "coarse"]

        RowLayout {
            id: anchorRow
            required property string modelData
            readonly property var anchor: root.calib[modelData] || ({})
            readonly property bool isSet: anchorRow.anchor.shotId > 0
            readonly property string label: anchorRow.modelData === "fine"
                ? TranslationManager.translate("grindcalibration.anchor.fine", "Fine")
                : TranslationManager.translate("grindcalibration.anchor.coarse", "Coarse")
            Layout.fillWidth: true
            spacing: Theme.scaled(6)

            Text {
                id: anchorText
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: anchorRow.label + ": " + root._anchorLine(anchorRow.anchor)
                      + ((anchorRow.anchor.rejection || "") !== ""
                         ? " (" + TranslationManager.translate("grindcalibration.anchor.notCounted", "not counted: %1")
                                      .arg(root.reasonText(anchorRow.anchor.rejection)) + ")"
                         : "")
                font: Theme.captionFont
                color: (anchorRow.anchor.rejection || "") !== "" ? Theme.warningColor : Theme.textColor

                AccessibleMouseArea {
                    anchors.fill: parent
                    enabled: anchorRow.isSet
                    accessibleName: anchorText.text
                    onAccessibleClicked: AppShell.shotDetailRequested(anchorRow.anchor.shotId, [anchorRow.anchor.shotId])
                }
            }
            StyledIconButton {
                visible: anchorRow.isSet
                enabled: root.serviceReady && (root.calib.error || "") === ""
                Layout.preferredWidth: Theme.scaled(32)
                Layout.preferredHeight: Theme.scaled(32)
                icon.source: "qrc:/icons/cross.svg"
                accessibleName: TranslationManager.translate("grindcalibration.anchor.clear", "Clear %1 anchor").arg(anchorRow.label)
                onClicked: EquipmentService.requestClearGrindAnchor(root.packageId, anchorRow.modelData)
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: !root.compact && root.pairProblemText !== ""
        wrapMode: Text.Wrap
        text: root.pairProblemText
        font: Theme.captionFont
        color: Theme.warningColor
    }

    Text {
        Layout.fillWidth: true
        visible: !root.compact && root.optionalText !== ""
        wrapMode: Text.Wrap
        text: root.optionalText
        font: Theme.captionFont
        color: Theme.textSecondaryColor
    }

    RowLayout {
        visible: root.shotId > 0
        Layout.fillWidth: true
        spacing: Theme.scaled(6)

        AccessibleButton {
            Layout.preferredHeight: Theme.scaled(32)
            _customFontSize: Theme.captionFont.pixelSize
            enabled: root.serviceReady && (root.calib.error || "") === ""
            text: TranslationManager.translate("grindcalibration.markFine", "Mark as fine anchor")
            accessibleName: text
            onClicked: root.markRequested("fine")
        }
        AccessibleButton {
            Layout.preferredHeight: Theme.scaled(32)
            _customFontSize: Theme.captionFont.pixelSize
            enabled: root.serviceReady && (root.calib.error || "") === ""
            text: TranslationManager.translate("grindcalibration.markCoarse", "Mark as coarse anchor")
            accessibleName: text
            onClicked: root.markRequested("coarse")
        }
        Item { Layout.fillWidth: true }
        AccessibleButton {
            Layout.preferredHeight: Theme.scaled(32)
            _customFontSize: Theme.captionFont.pixelSize
            text: TranslationManager.translate("grindcalibration.openEquipment", "Equipment")
            accessibleName: TranslationManager.translate("grindcalibration.openEquipmentAccessible", "Open the Equipment page to see the full grind calibration")
            onClicked: AppShell.equipmentRequested()
        }
    }
}
