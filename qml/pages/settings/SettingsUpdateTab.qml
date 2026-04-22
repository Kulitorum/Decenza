import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../../components"

Item {
    id: updateTab

    // Easter egg: tap version card 7 times to enable translation upload
    property int versionTapCount: 0
    property var lastTapTime: 0

    readonly property var fw: typeof MainController !== "undefined" && MainController
                              ? MainController.firmwareUpdater : null

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Current version info
        Rectangle {
            objectName: "checkUpdates"
            Layout.preferredWidth: Theme.scaled(280)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(10)

                Tr {
                    key: "settings.update.currentversion"
                    fallback: "Current Version"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(65)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(1)

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "Decenza"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "v" + AppVersion
                            color: Theme.accentColor
                            font.pixelSize: Theme.scaled(18)
                            font.bold: true
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: TranslationManager.translate("update.build", "Build %1").arg(AppVersionCode)
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: DE1Device.simulationMode ? "SIMULATION MODE" : MainController.updateChecker.platformName
                            color: DE1Device.simulationMode ? Theme.primaryColor : Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            font.bold: DE1Device.simulationMode
                        }
                    }

                    // Easter egg: tap 7 times to enable translation upload
                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleName: TranslationManager.translate("update.versionBuild", "Version %1, Build %2").arg(AppVersion).arg(AppVersionCode)
                        onAccessibleClicked: {
                            var now = Date.now()
                            // Reset counter if more than 2 seconds since last tap
                            if (now - updateTab.lastTapTime > 2000) {
                                updateTab.versionTapCount = 0
                            }
                            updateTab.lastTapTime = now
                            updateTab.versionTapCount++

                            if (updateTab.versionTapCount >= 7) {
                                updateTab.versionTapCount = 0
                                Settings.developerTranslationUpload = !Settings.developerTranslationUpload
                                var message
                                if (Settings.developerTranslationUpload) {
                                    message = "Translation upload enabled! Go to Settings, Language to upload."
                                } else {
                                    message = "Translation upload disabled."
                                }
                                translationUploadToast.show(message)
                                AccessibilityManager.announce(message)
                            } else if (updateTab.versionTapCount >= 4) {
                                var remaining = (7 - updateTab.versionTapCount) + " more taps"
                                translationUploadToast.show(remaining + "...")
                                AccessibilityManager.announce(remaining)
                            }
                        }
                    }
                }

                // Auto-check toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(1)

                        Tr {
                            key: "settings.update.autocheck"
                            fallback: "Auto-check for updates"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.update.checkeveryhour"
                            fallback: "Check every hour"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.autoCheckUpdates
                        accessibleName: TranslationManager.translate("settings.update.autocheck", "Auto-check for updates")
                        onToggled: Settings.autoCheckUpdates = checked
                    }
                }

                // Beta updates toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(1)

                        Tr {
                            key: "settings.update.betaupdates"
                            fallback: "Include beta versions"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.update.betadesc"
                            fallback: "Get early access to new features"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.betaUpdatesEnabled
                        accessibleName: TranslationManager.translate("settings.update.betaupdates", "Include beta versions")
                        onToggled: Settings.betaUpdatesEnabled = checked
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.borderColor
                    Layout.topMargin: Theme.spacingSmall
                    Layout.bottomMargin: Theme.spacingSmall
                }

                // About section
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("about.credits", "Built by Michael Holm (Kulitorum) during Christmas 2025. Three weeks, lots of coffee, one app.")
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(13)
                    color: Theme.textColor
                    wrapMode: Text.Wrap
                }

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("about.communityThanks", "Thanks to the Decent community and the de1app developers for inspiration.")
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(11)
                    color: Theme.textSecondaryColor
                    wrapMode: Text.Wrap
                }

                Item { Layout.fillHeight: true }

                // Support button
                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(44)
                    text: TranslationManager.translate("about.supportProject", "Support This Project")
                    accessibleName: TranslationManager.translate("about.supportProject", "Support This Project")
                    primary: true
                    onClicked: donateDialog.open()
                }
            }
        }

        // Right column: Firmware card + Software Updates
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(8)

            // Firmware card (DE1 firmware update entry point)
            Rectangle {
                objectName: "firmwareUpdate"
                Layout.fillWidth: true
                Layout.preferredHeight: firmwareCardContent.implicitHeight + Theme.scaled(16)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: firmwareCardContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(10)
                    spacing: Theme.scaled(4)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "firmware.card.title"
                            fallback: "DE1 Firmware"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                        }

                        Rectangle {
                            width: Theme.scaled(10)
                            height: Theme.scaled(10)
                            radius: Theme.scaled(5)
                            Layout.leftMargin: Theme.scaled(4)
                            color: {
                                if (!updateTab.fw) return Theme.textSecondaryColor
                                if (updateTab.fw.updateAvailable)
                                    return updateTab.fw.isDowngrade ? Theme.warningColor : Theme.primaryColor
                                if (updateTab.fw.installedVersion > 0) return Theme.successColor
                                return Theme.textSecondaryColor
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: {
                                if (!updateTab.fw) return ""
                                if (updateTab.fw.updateAvailable) {
                                    if (updateTab.fw.isDowngrade) {
                                        return TranslationManager.translate(
                                            "firmware.card.downgradeAvailable",
                                            "Downgrade available: v%1 (installed v%2)")
                                            .arg(updateTab.fw.availableVersion)
                                            .arg(updateTab.fw.installedVersion > 0 ? updateTab.fw.installedVersion : "—")
                                    }
                                    return TranslationManager.translate(
                                        "firmware.card.updateAvailable",
                                        "Update available: v%1 (installed v%2)")
                                        .arg(updateTab.fw.availableVersion)
                                        .arg(updateTab.fw.installedVersion > 0 ? updateTab.fw.installedVersion : "—")
                                }
                                if (updateTab.fw.installedVersion > 0) {
                                    return TranslationManager.translate(
                                        "firmware.card.upToDate",
                                        "Up to date — v%1")
                                        .arg(updateTab.fw.installedVersion)
                                }
                                return TranslationManager.translate(
                                    "firmware.card.unknown",
                                    "Firmware version unknown — connect DE1 and check")
                            }
                            color: updateTab.fw && updateTab.fw.updateAvailable
                                   ? (updateTab.fw.isDowngrade ? Theme.warningColor : Theme.textColor)
                                   : Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            font.bold: updateTab.fw && updateTab.fw.updateAvailable
                            elide: Text.ElideRight
                        }

                        Text {
                            id: firmwareManageText
                            text: TranslationManager.translate("firmware.card.manage", "Manage...")
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(13)
                            Accessible.ignored: true
                            AccessibleMouseArea {
                                anchors.fill: parent
                                anchors.margins: -Theme.scaled(6)
                                accessibleName: TranslationManager.translate("firmware.card.manageAccessible", "Open DE1 firmware update")
                                accessibleItem: firmwareManageText
                                onAccessibleClicked: firmwareDialog.open()
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate(
                            "firmware.card.description",
                            "Check, update, or downgrade the DE1 firmware.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Software Updates card (app updates + release notes)
            Rectangle {
            objectName: "releaseNotes"
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(10)
                spacing: Theme.scaled(6)

                // Title + inline action buttons
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.update.softwareupdates"
                        fallback: "Software Updates"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    // Hidden while the status area is showing its own progress UI
                    readonly property bool idleState: MainController.updateChecker.canCheckForUpdates
                                                      && !MainController.updateChecker.checking
                                                      && !MainController.updateChecker.downloading
                                                      && !MainController.updateChecker.installing

                    AccessibleButton {
                        text: TranslationManager.translate("settings.update.checknow", "Check Now")
                        accessibleName: TranslationManager.translate("settings.update.checkNowAccessible", "Check for app updates")
                        visible: parent.idleState
                        enabled: !MainController.updateChecker.checking
                        onClicked: MainController.updateChecker.checkForUpdates()
                    }

                    AccessibleButton {
                        primary: true
                        text: MainController.updateChecker.downloadReady
                              ? TranslationManager.translate("settings.update.install", "Install")
                              : TranslationManager.translate("settings.update.downloadinstall", "Download & Install")
                        accessibleName: MainController.updateChecker.downloadReady
                              ? TranslationManager.translate("settings.update.installAccessible", "Install the downloaded update")
                              : TranslationManager.translate("settings.update.downloadInstallAccessible", "Download and install the available update")
                        visible: parent.idleState && MainController.updateChecker.updateAvailable && MainController.updateChecker.canDownloadUpdate
                        onClicked: MainController.updateChecker.downloadAndInstall()
                    }

                    AccessibleButton {
                        primary: true
                        text: TranslationManager.translate("settings.update.viewongithub", "View on GitHub")
                        accessibleName: TranslationManager.translate("settings.update.viewOnGithubAccessible", "Open the release page on GitHub")
                        visible: parent.idleState && MainController.updateChecker.updateAvailable && !MainController.updateChecker.canDownloadUpdate
                        onClicked: MainController.updateChecker.openReleasePage()
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.update.whatsnew", "What's New?")
                        accessibleName: TranslationManager.translate("settings.update.whatsNewAccessible", "View release notes for this update")
                        visible: parent.idleState && MainController.updateChecker.releaseNotes !== ""
                        onClicked: releaseNotesPopup.open()
                    }
                }

                // Status area
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: statusColumn.height + Theme.scaled(12)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)

                    ColumnLayout {
                        id: statusColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(8)
                        spacing: Theme.scaled(4)

                        // Status row
                        RowLayout {
                            spacing: Theme.scaled(8)
                            visible: !MainController.updateChecker.checking && !MainController.updateChecker.downloading && !MainController.updateChecker.installing

                            Rectangle {
                                width: Theme.scaled(10)
                                height: Theme.scaled(10)
                                radius: Theme.scaled(5)
                                color: MainController.updateChecker.updateAvailable ? Theme.primaryColor : Theme.successColor
                            }

                            Text {
                                text: {
                                    if (MainController.updateChecker.updateAvailable) {
                                        var betaTag = MainController.updateChecker.latestIsBeta ? " (Beta)" : ""
                                        var msg = TranslationManager.translate("settings.update.updateavailable", "Update available:") +
                                               " v" + MainController.updateChecker.latestVersion + betaTag +
                                               " (Build " + MainController.updateChecker.latestVersionCode + ")"
                                        // Add platform-specific note for iOS
                                        if (Qt.platform.os === "ios") {
                                            msg += "\n" + TranslationManager.translate("settings.update.appstoreupdate", "Update via App Store")
                                        }
                                        return msg
                                    } else if (MainController.updateChecker.latestVersion) {
                                        return TranslationManager.translate("settings.update.uptodate", "You're up to date")
                                    } else {
                                        return TranslationManager.translate("settings.update.checktostart", "Check for updates to get started")
                                    }
                                }
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(13)
                            }
                        }

                        // Checking indicator
                        RowLayout {
                            spacing: Theme.scaled(8)
                            visible: MainController.updateChecker.checking

                            BusyIndicator {
                                running: true
                                Layout.preferredWidth: Theme.scaled(20)
                                Layout.preferredHeight: Theme.scaled(20)
                            }

                            Tr {
                                key: "settings.update.checking"
                                fallback: "Checking for updates..."
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(13)
                            }
                        }

                        // Installing (PackageInstaller session write in progress)
                        RowLayout {
                            spacing: Theme.scaled(8)
                            visible: MainController.updateChecker.installing
                            Accessible.role: Accessible.StaticText
                            Accessible.name: trInstalling.text
                            Accessible.focusable: true

                            BusyIndicator {
                                running: true
                                Layout.preferredWidth: Theme.scaled(20)
                                Layout.preferredHeight: Theme.scaled(20)
                            }

                            Tr {
                                id: trInstalling
                                key: "settings.update.installing"
                                fallback: "Installing update..."
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(13)
                            }
                        }

                        // Download progress
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(6)
                            visible: MainController.updateChecker.downloading

                            RowLayout {
                                spacing: Theme.scaled(8)

                                Tr {
                                    key: "settings.update.downloading"
                                    fallback: "Downloading update..."
                                    color: Theme.textColor
                                    font.pixelSize: Theme.scaled(13)
                                }

                                Text {
                                    text: MainController.updateChecker.downloadProgress + "%"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.scaled(12)
                                }
                            }

                            ProgressBar {
                                Layout.fillWidth: true
                                value: MainController.updateChecker.downloadProgress / 100
                            }
                        }

                        // Error message
                        Text {
                            Layout.fillWidth: true
                            visible: MainController.updateChecker.errorMessage !== ""
                            text: MainController.updateChecker.errorMessage
                            color: Theme.errorColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // iOS App Store message
                Text {
                    Layout.fillWidth: true
                    visible: !MainController.updateChecker.canCheckForUpdates
                    text: TranslationManager.translate("settings.update.appstoreonly", "Updates are handled by the App Store. Check for updates in the App Store app.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }

                // Inline release notes preview
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)
                    visible: MainController.updateChecker.releaseNotes !== ""

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(6)

                        Text {
                            text: MainController.updateChecker.updateAvailable
                                  ? TranslationManager.translate("settings.update.pendingNotes", "What's New in v%1").arg(MainController.updateChecker.latestVersion)
                                  : TranslationManager.translate("settings.update.currentNotes", "Release Notes — v%1").arg(AppVersion)
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                            font.bold: true
                        }

                        ScrollView {
                            id: inlineNotesScrollView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                            TextArea {
                                width: inlineNotesScrollView.width
                                readOnly: true
                                text: MainController.updateChecker.releaseNotes
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                                wrapMode: Text.WordWrap
                                background: null
                                selectByMouse: true

                                Accessible.role: Accessible.StaticText
                                Accessible.name: TranslationManager.translate("settings.update.releaseNotesContent", "Release notes")
                                Accessible.description: Theme.stripMarkdown(text)
                                Accessible.focusable: true
                                activeFocusOnTab: true
                            }
                        }
                    }
                }

                // Spacer when no release notes
                Item {
                    Layout.fillHeight: true
                    visible: MainController.updateChecker.releaseNotes === ""
                }
            }
            }
        }
    }

    // Firmware update dialog (full-screen) — hosts the SettingsFirmwareTab panel
    Dialog {
        id: firmwareDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: parent ? parent.width - Theme.scaled(40) : Theme.scaled(800)
        height: parent ? parent.height - Theme.scaled(40) : Theme.scaled(600)
        modal: true
        dim: true
        padding: 0
        closePolicy: Dialog.CloseOnEscape

        // Latch: keep the panel loaded after the first open so state/progress
        // isn't lost when the user closes and reopens the dialog.
        property bool panelLoaded: false
        onOpened: panelLoaded = true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header bar with title + close button
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(48)
                color: Theme.backgroundColor
                radius: Theme.cardRadius

                // Square off bottom corners so header sits flush with content
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Theme.cardRadius
                    color: Theme.backgroundColor
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.scaled(16)
                    anchors.rightMargin: Theme.scaled(10)
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "firmware.dialog.title"
                        fallback: "DE1 Firmware Update"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(15)
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    StyledIconButton {
                        text: "×"
                        accessibleName: TranslationManager.translate("firmware.dialog.close", "Close firmware dialog")
                        enabled: !firmwarePanelLoader.item || !firmwarePanelLoader.item.isFlashing
                        onClicked: firmwareDialog.close()
                    }
                }
            }

            // Firmware panel content (lazy-loaded on first open, stays loaded)
            Loader {
                id: firmwarePanelLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Theme.scaled(8)
                asynchronous: true
                active: firmwareDialog.panelLoaded
                source: "SettingsFirmwareTab.qml"
            }
        }
    }

    // Release notes popup
    Dialog {
        id: releaseNotesPopup
        modal: true
        dim: true
        anchors.centerIn: parent
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        width: Math.min(600, updateTab.width - 40)
        height: Math.min(400, updateTab.height - 40)
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(0)

            // Header
            Rectangle {
                Layout.fillWidth: true
                height: Theme.scaled(44)
                color: Theme.backgroundColor
                radius: Theme.cardRadius

                // Square off bottom corners
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Theme.cardRadius
                    color: Theme.backgroundColor
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.scaled(15)
                    anchors.rightMargin: Theme.scaled(10)

                    Text {
                        text: {
                            var version = MainController.updateChecker.updateAvailable
                                ? MainController.updateChecker.latestVersion
                                : AppVersion
                            return TranslationManager.translate("settings.update.whatsnew", "What's New?") +
                                  (version ? " - v" + version : "")
                        }
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    StyledIconButton {
                        text: "\u00D7"
                        accessibleName: TranslationManager.translate("settings.update.closeReleaseNotes", "Close release notes")
                        onClicked: releaseNotesPopup.close()
                    }
                }
            }

            // Scrollable content
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Theme.scaled(15)

                ScrollView {
                    id: notesScrollView
                    anchors.fill: parent
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    TextArea {
                        id: notesText
                        width: notesScrollView.width
                        readOnly: true
                        text: MainController.updateChecker.releaseNotes
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(13)
                        wrapMode: Text.WordWrap
                        background: null
                        selectByMouse: true

                        Accessible.role: Accessible.StaticText
                        Accessible.name: TranslationManager.translate("settings.update.releaseNotesContent", "Release notes")
                        Accessible.description: Theme.stripMarkdown(text)
                        Accessible.focusable: true
                        activeFocusOnTab: true
                    }
                }

                // Scroll indicator - shows when more content below
                Rectangle {
                    id: scrollIndicator
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.scaled(5)
                    width: Theme.scaled(28)
                    height: Theme.scaled(28)
                    radius: Theme.scaled(14)
                    color: Theme.primaryColor
                    opacity: 0.9
                    visible: {
                        var scrollBar = notesScrollView.ScrollBar.vertical
                        return scrollBar && scrollBar.size < 1.0 && scrollBar.position + scrollBar.size < 0.95
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("accessibility.scrolldown", "Scroll down")
                    Accessible.focusable: true
                    Accessible.onPressAction: scrollDownArea.clicked(null)

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/ArrowLeft.svg"
                        sourceSize.width: Theme.scaled(16)
                        sourceSize.height: Theme.scaled(16)
                        rotation: 90
                        Accessible.ignored: true
                        layer.enabled: true
                        layer.smooth: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: Theme.primaryContrastColor
                        }
                    }

                    MouseArea {
                        id: scrollDownArea
                        anchors.fill: parent
                        onClicked: {
                            // Scroll down a bit
                            var scrollBar = notesScrollView.ScrollBar.vertical
                            if (scrollBar) {
                                scrollBar.position = Math.min(1.0 - scrollBar.size, scrollBar.position + 0.2)
                            }
                        }
                    }
                }
            }
        }
    }

    // Toast for Easter egg feedback
    Rectangle {
        id: translationUploadToast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(20)
        width: toastText.width + Theme.scaled(24)
        height: toastText.height + Theme.scaled(16)
        color: Theme.surfaceColor
        radius: Theme.scaled(8)
        opacity: 0
        visible: opacity > 0
        z: 1000

        border.width: 1
        border.color: Theme.borderColor

        Text {
            id: toastText
            anchors.centerIn: parent
            color: Theme.textColor
            font.pixelSize: Theme.scaled(13)
        }

        function show(message) {
            toastText.text = message
            toastAnimation.restart()
        }

        SequentialAnimation {
            id: toastAnimation
            NumberAnimation { target: translationUploadToast; property: "opacity"; to: 1; duration: 150 }
            PauseAnimation { duration: 2000 }
            NumberAnimation { target: translationUploadToast; property: "opacity"; to: 0; duration: 300 }
        }
    }

    // Donate dialog
    Dialog {
        id: donateDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(360))
        modal: true
        dim: true
        padding: Theme.spacingMedium
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("about.supportProject", "Support This Project")
                font.pixelSize: Theme.scaled(16)
                font.bold: true
                color: Theme.textColor
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("about.donationMessage", "If you find this app useful, donations are welcome but never expected.")
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(13)
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            // QR code
            Image {
                Layout.alignment: Qt.AlignHCenter
                source: "qrc:/qrcode.png"
                width: Theme.scaled(150)
                height: Theme.scaled(150)
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 150
                sourceSize.height: 150
            }

            Text {
                Layout.fillWidth: true
                text: "paypal@kulitorum.com"
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
            }

            // Donate via PayPal button
            Rectangle {
                Layout.fillWidth: true
                height: Theme.scaled(48)
                radius: Theme.buttonRadius
                color: Theme.primaryColor

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("about.donateViaPaypal", "Donate via PayPal")
                Accessible.focusable: true
                Accessible.onPressAction: donateArea.clicked(null)

                MouseArea {
                    id: donateArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Qt.openUrlExternally("https://www.paypal.com/donate?business=paypal@kulitorum.com")
                }

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("about.donateViaPaypal", "Donate via PayPal")
                    font.pixelSize: Theme.scaled(15)
                    font.bold: true
                    color: Theme.primaryContrastColor
                    Accessible.ignored: true
                }
            }

            // Close button
            AccessibleButton {
                Layout.fillWidth: true
                text: TranslationManager.translate("common.button.close", "Close")
                accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Close dialog")
                onClicked: donateDialog.close()
            }
        }
    }
}
