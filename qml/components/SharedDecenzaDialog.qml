pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Decenza

T.Dialog {
    id: control

    required property SharedDialogStyle dialogStyle
    property string logName: ""
    // Shared browser-shell policy. Derived dialogs bind these to their local
    // pending state and editable-baseline comparison; no host draft is copied.
    // A Dialog with NoAutoClose is waiting for an explicit outcome; browser
    // Back must not bypass the same pending-operation guard used by Escape.
    property bool browserBackDismissible:
        (control.closePolicy & T.Popup.CloseOnEscape) !== 0
    property bool browserUnloadDirty: false
    signal logShown(string name)

    function dismissForBrowserBack(): void {
        // Qt 6.12's qquickpopup.cpp:521-529 routes a Dialog's Escape through
        // reject(); qquickdialog.cpp:523-557 emits rejected() before closing.
        // Keep browser Back equivalent to Escape.
        reject()
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth, implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))
    padding: dialogStyle.padding
    topPadding: dialogStyle.topPadding
    modal: true
    focus: modal

    Connections {
        target: control
        function onAboutToShow() {
            AppShell.registerBrowserDialog(control, control.browserBackDismissible,
                                           control.browserUnloadDirty,
                                           function() { control.dismissForBrowserBack() })
        }
        function onOpened() {
            if (control.logName.length > 0)
                control.logShown(control.logName)
        }
        // Relinquish Back ownership as soon as the close begins. The shell can
        // receive the confirmed navigation request in the same turn that a
        // dialog's Discard handler calls close(); waiting for `closed` would
        // let the fading dialog consume that navigation a second time.
        function onAboutToHide() {
            AppShell.unregisterBrowserDialog(control)
        }
        function onClosed() {
            if (!control.visible)
                AppShell.unregisterBrowserDialog(control)
        }
        function onBrowserBackDismissibleChanged() {
            AppShell.updateBrowserDialog(control, control.browserBackDismissible,
                                         control.browserUnloadDirty)
        }
        function onBrowserUnloadDirtyChanged() {
            AppShell.updateBrowserDialog(control, control.browserBackDismissible,
                                         control.browserUnloadDirty)
        }
    }

    Component.onDestruction: AppShell.unregisterBrowserDialog(control)

    enter: Transition {
        NumberAnimation { property: "scale"; from: 0.9; to: 1.0; easing.type: Easing.OutQuint; duration: 220 }
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; easing.type: Easing.OutCubic; duration: 150 }
    }
    exit: Transition {
        NumberAnimation { property: "scale"; from: 1.0; to: 0.9; easing.type: Easing.OutQuint; duration: 220 }
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; easing.type: Easing.OutCubic; duration: 150 }
    }

    header: Text {
        text: control.title
        visible: parent?.parent === T.Overlay.overlay && control.title !== ""
        elide: Text.ElideRight
        color: control.dialogStyle.textColor
        font.family: control.dialogStyle.subtitleFont.family
        font.pixelSize: control.dialogStyle.scaled(20)
        font.weight: Font.Medium
        padding: control.dialogStyle.scaled(24)
        bottomPadding: 0
        Accessible.ignored: true
    }

    T.Overlay.modal: Rectangle {
        color: control.dialogStyle.dialogDimColor
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }
    T.Overlay.modeless: Rectangle {
        color: control.dialogStyle.dialogDimColor
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }
}
