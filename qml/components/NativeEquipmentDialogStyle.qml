import QtQuick
import Decenza

EquipmentDialogStyle {
    property NativeDialogStyle nativeDialogStyle: NativeDialogStyle {}
    property NativeButtonStyle nativeButtonStyle: NativeButtonStyle {}
    property NativeButtonInteraction nativeButtonInteraction: NativeButtonInteraction {}
    property NativeKeyboardInteraction nativeKeyboardInteraction: NativeKeyboardInteraction {}
    property NativeFormControlStyle nativeFormControlStyle: NativeFormControlStyle {}
    property NativeSwitchStyle nativeSwitchStyle: NativeSwitchStyle {}

    dialogStyle: nativeDialogStyle
    buttonStyle: nativeButtonStyle
    buttonInteraction: nativeButtonInteraction
    keyboardInteraction: nativeKeyboardInteraction
    formControlStyle: nativeFormControlStyle
    switchStyle: nativeSwitchStyle
    spacingSmall: Theme.spacingSmall
    spacingMedium: Theme.spacingMedium
    cardRadius: Theme.cardRadius
    titleFont: Theme.titleFont
    labelFont: Theme.labelFont
    captionFont: Theme.captionFont
    warningColor: Theme.warningColor
    errorColor: Theme.errorColor
}
