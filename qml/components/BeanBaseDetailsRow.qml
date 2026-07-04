import QtQuick
import QtQuick.Layouts
import Decenza

// Compact Bean Base summary: bag thumbnail + "origin · variety · process"
// one-liner + tap-to-open-details. Zero footprint when the blob is empty —
// unlinked beans and legacy shots render nothing. Mounted on BeanInfoPage,
// PostShotReviewPage, and ShotDetailPage, each feeding its own blob source.
Item {
    id: root

    property string beanBaseJson: ""
    readonly property var bean: {
        if (!beanBaseJson || beanBaseJson.length === 0) return ({})
        try { return JSON.parse(beanBaseJson) } catch (e) { return ({}) }
    }
    readonly property bool hasData: bean.id !== undefined && bean.id !== ""

    readonly property var _summaryParts: {
        var parts = []
        if (bean.origin) parts.push(bean.origin)
        if (bean.variety) parts.push(bean.variety)
        if (bean.process) parts.push(bean.process)
        return parts
    }
    readonly property string summaryLine: _summaryParts.join("  ·  ")
    // Display form: styled bold-dot separator, user data HTML-escaped by the helper.
    readonly property string summaryLineRich: Theme.joinWithBullet(_summaryParts)

    visible: hasData
    implicitHeight: hasData ? Math.max(thumb.height, summaryColumn.implicitHeight) : 0
    implicitWidth: rowLayout.implicitWidth

    RowLayout {
        id: rowLayout
        anchors.fill: parent
        spacing: Theme.scaled(10)

        // Bag thumbnail — collapses when missing or failing to load
        Image {
            id: thumb
            Layout.preferredWidth: status === Image.Ready ? Theme.scaled(44) : 0
            Layout.preferredHeight: Theme.scaled(44)
            visible: status === Image.Ready
            source: root.bean.image !== undefined ? root.bean.image : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            Accessible.ignored: true
        }

        ColumnLayout {
            id: summaryColumn
            Layout.fillWidth: true
            spacing: Theme.scaled(1)

            Text {
                Layout.fillWidth: true
                text: root.summaryLine.length > 0
                    ? root.summaryLineRich
                    : TranslationManager.translate("beanbase.row.linked", "Linked to Bean Base")
                textFormat: Text.StyledText
                color: Theme.textColor
                font.pixelSize: Theme.scaled(12)
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("beanbase.row.tapForDetails", "Tap for bean details")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(10)
                elide: Text.ElideRight
            }
        }
    }

    AccessibleMouseArea {
        anchors.fill: parent
        accessibleName: TranslationManager.translate("beanbase.row.accessible", "Bean details from Bean Base. Opens details dialog")
        accessibleItem: root
        onAccessibleClicked: detailsPopup.open()
    }

    BeanBaseDetailsPopup {
        id: detailsPopup
        beanBaseJson: root.beanBaseJson
    }
}
