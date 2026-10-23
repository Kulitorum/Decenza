import QtQuick

SharedAccessibleButton {
    required property EquipmentDialogStyle equipmentStyle
    buttonStyle: equipmentStyle.buttonStyle
    interaction: equipmentStyle.buttonInteraction
}
