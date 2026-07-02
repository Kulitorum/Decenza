import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText  // display-only summary, not interactive
    Accessible.name: {
        var plan = compactSteamPlan.text || fullSteamPlan.text || ""
        return plan ? TranslationManager.translate("plan.a11y.steamPlan", "Steam plan: %1").arg(plan)
                    : TranslationManager.translate("plan.a11y.steamPlanEmpty", "Steam plan")
    }
    Accessible.focusable: true

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactSteamPlan.implicitWidth
        implicitHeight: compactSteamPlan.implicitHeight

        SteamPlanText {
            id: compactSteamPlan
            anchors.centerIn: parent
            visible: text !== ""
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: fullSteamPlan.implicitWidth
        implicitHeight: fullSteamPlan.implicitHeight

        SteamPlanText {
            id: fullSteamPlan
            anchors.centerIn: parent
            visible: text !== ""
        }
    }
}
