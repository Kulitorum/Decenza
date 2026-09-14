// The new-profile type tiles' delegate reads `newTypeTile`'s own modelData
// only (no outer-file id lookups needed there), but the dialogs below use
// nested Components (layer.effect-free here) — Bound is kept for consistency
// with every other page in this app and because the picker it hosts requires it.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

// rebuild-profile-picker: this page is now a thin host around the shared
// ProfilePicker component (profile-picker spec). It supplies the current DYE
// bean for tiers, opens with the Favorites chip on, and a card tap LOADS the
// profile — the choice that makes this host the "selector" rather than the
// wizard's "chooser". Everything the old six-way combo / favorites panel /
// checkbox column did now lives in ProfilePicker or its dialogs.
T.Page {
    id: profileSelectorPage
    readonly property string pageTitle: TranslationManager.translate("profileselector.title", "Profiles")

    objectName: "profileSelectorPage"
    background: ThemedPageBackground {}

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin
        spacing: Theme.scaled(10)

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.cardBackgroundColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)

                ProfilePicker {
                    id: picker
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // Bean tiers from the current DYE bean + its roast level
                    // (task 3.3) — vanish automatically with no bean set.
                    beanBrand: Settings.dye.dyeBeanBrand
                    beanType: Settings.dye.dyeBeanType
                    roastLevel: Settings.dye.dyeRoastLevel

                    // Selector opens on Favorites, nothing else (profile-picker spec).
                    initialChips: ({ favorites: true })
                    showAutoLoadStrip: true
                    showAddButton: true
                    onAddRequested: addMenuDialog.open()

                    onProfileChosen: function(filename) {
                        ProfileManager.loadProfile(filename)
                    }
                }
            }
        }
    }

    // ===== "+" menu dialog (design D9) =====
    DecenzaDialog {
        id: addMenuDialog
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        padding: 20
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Accessible.role: Accessible.Dialog
            Accessible.name: TranslationManager.translate("profilepicker.addMenu.title", "Add a profile")

            AccessibleButton {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(40)
                text: TranslationManager.translate("profileselector.button.import_visualizer_short", "Visualizer")
                accessibleName: TranslationManager.translate("profileSelector.importFromVisualizer", "Import profiles from Visualizer website")
                onClicked: {
                    addMenuDialog.close()
                    AppShell.visualizerBrowserRequested()
                }
            }

            AccessibleButton {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(40)
                text: Qt.platform.os === "ios"
                      ? TranslationManager.translate("profileselector.button.import_file_short", "File")
                      : TranslationManager.translate("profileselector.button.import_tablet_short", "Tablet")
                accessibleName: Qt.platform.os === "ios"
                      ? TranslationManager.translate("profileSelector.importFromFiles", "Import a profile file from Files app")
                      : TranslationManager.translate("profileSelector.importFromTablet", "Import profiles from Decent tablet")
                onClicked: {
                    addMenuDialog.close()
                    AppShell.profileImportRequested()
                }
            }

            AccessibleButton {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(40)
                text: TranslationManager.translate("profileselector.newProfile.title", "New Profile")
                accessibleName: TranslationManager.translate("profileSelector.createNewProfile", "Create new profile")
                onClicked: {
                    addMenuDialog.close()
                    newProfileDialog.open()
                }
            }
        }
    }

    // ===== New profile type picker dialog =====
    DecenzaDialog {
        id: newProfileDialog
        anchors.centerIn: parent
        width: Theme.scaled(350)
        padding: 0
        modal: true

        header: Item {
            implicitHeight: Theme.scaled(50)

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                text: TranslationManager.translate("profileselector.newProfile.title", "New Profile")
                font: Theme.titleFont
                color: Theme.textColor
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(8)

            Repeater {
                model: [
                    { label: TranslationManager.translate("profileselector.newProfile.pressure", "Pressure Profile"), type: "pressure" },
                    { label: TranslationManager.translate("profileselector.newProfile.flow", "Flow Profile"), type: "flow" },
                    { label: TranslationManager.translate("profileselector.newProfile.dflow", "D-Flow"), type: "dflow" },
                    { label: TranslationManager.translate("profileselector.newProfile.aflow", "A-Flow"), type: "aflow" },
                    { label: TranslationManager.translate("profileselector.newProfile.advanced", "Advanced"), type: "advanced" }
                ]

                delegate: Rectangle {
                    id: newTypeTile
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.scaled(12)
                    Layout.rightMargin: Theme.scaled(12)
                    Layout.preferredHeight: Theme.scaled(48)
                    radius: Theme.scaled(6)
                    color: typeMouseArea.containsMouse ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : Theme.insetBackgroundColor
                    Accessible.role: Accessible.Button
                    Accessible.name: newTypeTile.modelData.label
                    Accessible.focusable: true
                    Accessible.onPressAction: typeMouseArea.clicked(null)

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.scaled(16)
                        text: newTypeTile.modelData.label
                        color: Theme.textColor
                        font: Theme.bodyFont
                        verticalAlignment: Text.AlignVCenter
                        Accessible.ignored: true
                    }

                    MouseArea {
                        id: typeMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        Accessible.ignored: true
                        onClicked: {
                            newProfileDialog.close()
                            var profileType = newTypeTile.modelData.type
                            if (profileType === "pressure") {
                                ProfileManager.createNewPressureProfile("New Pressure Profile")
                                AppShell.profileEditorRequested()
                            } else if (profileType === "flow") {
                                ProfileManager.createNewFlowProfile("New Flow Profile")
                                AppShell.profileEditorRequested()
                            } else if (profileType === "dflow") {
                                ProfileManager.createNewRecipe("D-Flow / New Recipe")
                                AppShell.profileEditorRequested()
                            } else if (profileType === "aflow") {
                                ProfileManager.createNewAFlowRecipe("A-Flow / New Recipe")
                                AppShell.profileEditorRequested()
                            } else {
                                ProfileManager.createNewProfile("New Profile")
                                AppShell.profileEditorRequested()
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.scaled(4) }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(8)
            border.color: Theme.borderColor
        }
    }

    // Bottom bar
    BottomBar {
        title: TranslationManager.translate("profileselector.title", "Profiles")
        rightText: TranslationManager.translate("profileselector.current_prefix", "Current:") + " " + ProfileManager.currentProfileName
        onBackClicked: AppShell.backRequested()
    }
}
