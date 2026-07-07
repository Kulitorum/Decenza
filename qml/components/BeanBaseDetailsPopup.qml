import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Bean Base details viewer — renders every cached attribute from a
// beanBaseJson blob. Shared by BeanInfoPage (live DYE state), the post-shot
// review page, and the shot detail page (per-shot snapshots), so a historical
// shot always shows the bean it was actually pulled with.
Popup {
    id: root

    // Compact-JSON blob; parse failures render as an empty popup body, never throw.
    property string beanBaseJson: ""
    readonly property var bean: {
        if (!beanBaseJson || beanBaseJson.length === 0) return ({})
        try { return JSON.parse(beanBaseJson) } catch (e) { return ({}) }
    }

    function fieldOrEmpty(key) { return bean[key] !== undefined && bean[key] !== null ? String(bean[key]) : "" }

    // Bag photo from the on-disk image cache (resolved from the product page's
    // og:image; canonical entries carry no image field). Legacy blobs may still
    // carry a CDN `image` URL, used as fallback. Resolution is requested when
    // the popup opens so the photo appears on the spot when it can be fetched.
    property string cachedImagePath: ""
    // Image-cache key: the canonical id for linked blobs; a host showing a
    // manual bag with a product URL passes its "bag-<rowid>" key instead
    // (add-bag-detail-editing). Defaults to the blob's own id so per-shot
    // snapshot hosts need no change.
    property string imageKey: fieldOrEmpty("id")

    onAboutToShow: {
        if (imageKey.length === 0) {
            cachedImagePath = ""
            return
        }
        cachedImagePath = MainController.beanbase.bagImagePath(imageKey)
        if (cachedImagePath.length === 0)
            MainController.beanbase.ensureBagImage(imageKey, fieldOrEmpty("roastName"), fieldOrEmpty("link"))
    }

    Connections {
        target: MainController.beanbase
        function onBagImageReady(id, path) {
            if (root.visible && id === root.imageKey)
                root.cachedImagePath = path
        }
    }

    readonly property string elevationText: {
        // Canonical/user-edited blobs carry a display string; legacy Bean Base
        // blobs carry the numeric min/max pair instead.
        if (bean.elevation) return String(bean.elevation)
        const lo = Number(bean.minElevationM || 0)
        const hi = Number(bean.maxElevationM || 0)
        if (lo > 0 && hi > 0 && hi !== lo) return lo + "–" + hi + " m"
        if (lo > 0) return lo + " m"
        if (hi > 0) return hi + " m"
        return ""
    }

    anchors.centerIn: Overlay.overlay
    width: Math.min(Theme.scaled(520), Overlay.overlay ? Overlay.overlay.width - Theme.scaled(40) : Theme.scaled(520))
    height: Math.min(contentColumn.implicitHeight + Theme.scaled(60), Overlay.overlay ? Overlay.overlay.height - Theme.scaled(80) : Theme.scaled(600))
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.scaled(20)

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: Flickable {
        // Accessible.* must attach to an Item (Popup is not one), so the dialog
        // role/name live on the content Flickable, not the Popup root.
        Accessible.role: Accessible.Dialog
        // Announce WHICH bean this is, not just a generic title — the identity
        // Texts below are Accessible.ignored so they aren't read twice.
        Accessible.name: {
            var title = TranslationManager.translate("beanbase.details.title", "Bean details")
            var parts = [root.fieldOrEmpty("roastName"), root.fieldOrEmpty("roasterName")]
                .filter(function(p) { return p.length > 0 })
            return parts.length > 0 ? title + ": " + parts.join(", ") : title
        }
        contentHeight: contentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentColumn
            width: parent.width
            spacing: Theme.scaled(10)

            // Title row: name + roaster, close button
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)

                    Text {
                        Layout.fillWidth: true
                        text: root.fieldOrEmpty("roastName")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(18)
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true  // in the dialog name
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.fieldOrEmpty("roasterName")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(14)
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true  // in the dialog name
                    }
                }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.close", "Close")
                    accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                    onClicked: root.close()
                }
            }

            // Sold-out advisory
            Text {
                visible: root.bean.soldout === true
                text: TranslationManager.translate("beanbase.details.soldout", "Marked sold out at the roaster")
                color: Theme.errorColor
                font.pixelSize: Theme.scaled(12)
            }

            // Bag photo — collapses silently when absent or failing to load
            Image {
                id: bagImage
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(Theme.scaled(220), contentColumn.width)
                Layout.preferredHeight: status === Image.Ready
                    ? Layout.preferredWidth * (implicitHeight / Math.max(1, implicitWidth))
                    : 0
                visible: status === Image.Ready
                source: root.cachedImagePath.length > 0
                    ? "file:///" + root.cachedImagePath
                    : root.fieldOrEmpty("image")
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                Accessible.ignored: true
            }

            // Attribute grid — rows render only when the field has a value
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.scaled(16)
                rowSpacing: Theme.scaled(6)

                component AttrLabel: Text {
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(13)
                }
                component AttrValue: Text {
                    Layout.fillWidth: true
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    wrapMode: Text.WordWrap
                }

                AttrLabel { text: TranslationManager.translate("beanbase.details.origin", "Origin"); visible: root.fieldOrEmpty("origin").length > 0 }
                AttrValue { text: root.fieldOrEmpty("origin"); visible: root.fieldOrEmpty("origin").length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.region", "Region"); visible: root.fieldOrEmpty("region").length > 0 }
                AttrValue { text: root.fieldOrEmpty("region"); visible: root.fieldOrEmpty("region").length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.farm", "Farm"); visible: root.fieldOrEmpty("farm").length > 0 }
                AttrValue { text: root.fieldOrEmpty("farm"); visible: root.fieldOrEmpty("farm").length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.producer", "Producer"); visible: root.fieldOrEmpty("producer").length > 0 }
                AttrValue { text: root.fieldOrEmpty("producer"); visible: root.fieldOrEmpty("producer").length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.variety", "Variety"); visible: root.fieldOrEmpty("variety").length > 0 }
                AttrValue { text: root.fieldOrEmpty("variety"); visible: root.fieldOrEmpty("variety").length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.process", "Process"); visible: root.fieldOrEmpty("process").length > 0 }
                AttrValue { text: root.fieldOrEmpty("process"); visible: root.fieldOrEmpty("process").length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.elevation", "Elevation"); visible: root.elevationText.length > 0 }
                AttrValue { text: root.elevationText; visible: root.elevationText.length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.roastLevel", "Roast level"); visible: root.fieldOrEmpty("degree").length > 0 }
                AttrValue { text: root.fieldOrEmpty("degree"); visible: root.fieldOrEmpty("degree").length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.beanType", "Roasted for"); visible: root.fieldOrEmpty("beanType").length > 0 }
                AttrValue { text: root.fieldOrEmpty("beanType"); visible: root.fieldOrEmpty("beanType").length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.harvest", "Harvest"); visible: root.fieldOrEmpty("harvest").length > 0 }
                AttrValue { text: root.fieldOrEmpty("harvest"); visible: root.fieldOrEmpty("harvest").length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.qualityScore", "Quality score"); visible: root.fieldOrEmpty("qualityScore").length > 0 }
                AttrValue { text: root.fieldOrEmpty("qualityScore"); visible: root.fieldOrEmpty("qualityScore").length > 0 }

                AttrLabel { text: TranslationManager.translate("beanbase.details.placeOfPurchase", "Purchased at"); visible: root.fieldOrEmpty("placeOfPurchase").length > 0 }
                AttrValue { text: root.fieldOrEmpty("placeOfPurchase"); visible: root.fieldOrEmpty("placeOfPurchase").length > 0 }
            }

            // Tasting tag chips
            Flow {
                Layout.fillWidth: true
                spacing: Theme.scaled(6)
                visible: Array.isArray(root.bean.tastingTags) && root.bean.tastingTags.length > 0

                Repeater {
                    model: Array.isArray(root.bean.tastingTags) ? root.bean.tastingTags : []
                    delegate: Rectangle {
                        width: chipText.implicitWidth + Theme.scaled(16)
                        height: chipText.implicitHeight + Theme.scaled(8)
                        radius: height / 2
                        color: Theme.backgroundColor
                        border.color: Theme.borderColor
                        border.width: 1

                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }
                }
            }

            // Roaster's tasting notes / description
            Text {
                Layout.fillWidth: true
                visible: root.fieldOrEmpty("tastingNotes").length > 0
                text: root.fieldOrEmpty("tastingNotes")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(13)
                font.italic: true
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                visible: root.fieldOrEmpty("description").length > 0
                      && root.fieldOrEmpty("description") !== root.fieldOrEmpty("tastingNotes")
                text: root.fieldOrEmpty("description")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(12)
                wrapMode: Text.WordWrap
            }

            // Product link — the action line plus the visible URL itself, so
            // the destination is recognizable at a glance (reordering aid).
            ColumnLayout {
                id: productLink
                Layout.fillWidth: true
                visible: root.fieldOrEmpty("link").length > 0
                spacing: Theme.scaled(1)

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("beanbase.details.viewAtRoaster", "View at roaster")
                    color: Theme.primaryColor
                    font.pixelSize: Theme.scaled(13)
                    Accessible.ignored: true  // accessibleItem; node carried by AccessibleMouseArea
                }
                Text {
                    Layout.fillWidth: true
                    text: root.fieldOrEmpty("link")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    elide: Text.ElideMiddle
                    Accessible.ignored: true
                }

                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleName: TranslationManager.translate("beaninfo.beanbase.openUrlAccessible", "View bean at roaster website. Opens web browser")
                    accessibleItem: productLink
                    onAccessibleClicked: Qt.openUrlExternally(root.fieldOrEmpty("link"))
                }
            }
        }
    }
}
