# Qt WebAssembly accessibility findings

Two defects reproduced with stock Qt 6.12.0 Wasm and filed on 2026-10-20 after the maintainer authorized upstream reports:

- [QTBUG-151710](https://bugreports.qt.io/browse/QTBUG-151710): missing accessible name.
- [QTBUG-151711](https://bugreports.qt.io/browse/QTBUG-151711): stale programmatic text value.

`AccessibilityRepro.qml` uses only QtQuick and QtQuick.Controls. Build the probe through Qt Creator and open `/wasm/?accessibility-repro` to load it without instantiating application controls or HTTP backends. Activate Enable Screen Reader, inspect the unnamed input, then activate Set programmatically: the QML label updates while the accessibility input retains `initial value`. This was observed in the Mac Chromium 155 embedded browser. Reports include standalone QML and source references; neither claims actual screen-reader verification.

## Finding A: QML editable-field accessible name is missing from the browser shadow input

**Minimal QML reproduction** (run in a Qt 6.12.0 WebAssembly Quick Controls application with Wasm accessibility enabled):

```qml
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    width: 420
    height: 160
    visible: true

    TextField {
        id: field
        anchors.centerIn: parent
        text: "initial value"
        Accessible.role: Accessible.EditableText
        Accessible.name: "Example field"
    }
}
```

**Expected:** the browser shadow element is an editable text input named “Example field” with value “initial value”.

**Observed on Qt 6.12.0 Wasm:** the visible QML TextField renders; its shadow `<input>` has no accessible name/`aria-label`. The Qt Quick accessibility interface does contain the name. A name change reaches the Wasm EditableText handler, but that handler writes `QAccessible::Value` to the DOM `value` property instead of writing the `QAccessible::Name` to an accessible-name attribute.

**Source:** `qtdeclarative/src/quick/accessible/qaccessiblequickitem.cpp:648-660` reads `Accessible.name`; `qtdeclarative/src/quick/items/qquickaccessibleattached_p.h:103-110` emits `NameChanged`; `qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:814-816` handles EditableText `NameChanged` by setting `value`. The same Wasm file's button handler does map `NameChanged` to `aria-label` at `:894-900`.

## Finding B: programmatic TextField text changes do not update the browser shadow value

**Minimal QML reproduction:** use the same imports/window and TextField as above, plus this button:

```qml
Button {
    anchors.top: field.bottom
    anchors.horizontalCenter: parent.horizontalCenter
    text: "Set programmatically"
    onClicked: field.text = "updated value"
}
```

**Expected:** after the button is activated, both the visible QML field and its browser shadow input expose “updated value”.

**Observed on Qt 6.12.0 Wasm:** the visible QML TextField updates to “updated value”; the browser accessibility shadow input retains “initial value”. The Qt 6.12 Wasm accessibility bridge does not process the `TextUpdated` event emitted for this programmatic update.

**Source:** `qtdeclarative/src/quick/items/qquicktextinput.cpp:3880-3889` emits `QAccessibleTextUpdateEvent` from `internalSetText()`; `qtbase/src/gui/accessible/qaccessible.h:546-560` assigns that event type `TextUpdated`; `qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:802-840` handles `TextInserted`/`TextRemoved` and caret movement but has no `TextUpdated` branch. `qtdeclarative/src/quick/accessible/qaccessiblequickitem.cpp:691-701` reads the current `displayText`, so the current value is available to the interface.

## Existing upstream coverage checked

The umbrella [QTBUG-144330](https://bugreports.qt.io/browse/QTBUG-144330), “Complete accessibility support for basic UI elements on WebAssembly,” is In Progress as checked on 2026-10-20. [Gerrit 707847](https://codereview.qt-project.org/c/qt/qtbase/+/707847), “wasm:a11y Add support for menus and dialogs,” was NEW/unmerged at revision 21, updated October 13, targets 6.12, and references QTBUG-144330 and QTBUG-140108. Its reviewed patch addresses dialog/menu shadows and browser dialog behavior; it does not appear to address the two editable-field paths above.

Focused public Jira REST searches on 2026-10-20 for `TextUpdated` + WebAssembly, `TextUpdated` + accessibility, accessible-text + Wasm, `QAccessible` + WebAssembly, and editable-text + WebAssembly did not find a report specifically about the missing DOM accessible name or programmatic `TextUpdated` synchronization. Results included the umbrella and other Wasm accessibility work, plus [QTBUG-138821](https://bugreports.qt.io/browse/QTBUG-138821), “Textfields properties not updates on editing at right moment on mobile browser (Wasm).” QTBUG-138821 was fixed in 2025 by [Gerrit 674612](https://codereview.qt-project.org/c/qt/qtbase/+/674612), “wasm: fix composition for mobile input.” That report concerns delayed QML text-property updates during mobile keyboard composition, and the change fixes mobile input composition; it is related to text input behavior but does not describe the browser accessibility shadow's missing `aria-label` or stale value after a programmatic QML assignment. It should not be treated as a duplicate of these findings.

Focused Gerrit searches on 2026-10-20 covered `qt/qtbase` changes touching `qwasmaccessibility.cpp`, plus 6.12 queries for `aria-label`, `NameChanged`, and `TextUpdated`. No pending change found in those searches addressed either editable-field gap. The returned 6.12 accessibility changes concern spin-box/slider role mapping; the matching `TextUpdated` code search produced only unrelated historical merges and an AT-SPI fix. Search results are not a guarantee that no differently worded or unindexed change exists. The report filer should associate either finding with QTBUG-144330 if appropriate and mention the distinct mobile-composition fix QTBUG-138821/Gerrit 674612 to avoid conflating them.

## Relationship and possible scope

The symptoms are independently reproducible: one is incorrect `NameChanged` mapping; the other is an omitted `TextUpdated` event. They can be included as separate cases in one focused Qt Wasm accessibility report if maintainers prefer a single `QWasmAccessibility` issue, or split if the Qt team tracks name mapping and value synchronization separately. A public `QAccessibleValueChangeEvent` from application C++ can exercise the existing Wasm `ValueChanged` branch for value synchronization, but it does not fix the accessible-name mapping. The QML application should not need a private Wasm DOM workaround for either standard editable-text property/event.

Both findings were reproduced in the isolated Qt-only screen as well as the application fixture; the stale accessibility value was recorded separately by DOM/tree inspection. Actual VoiceOver/Safari, VoiceOver/Chrome, and mobile screen-reader behavior has not been verified.

## Shared value-sync workaround (2026-10-21)

Recipe ratio selection reproduced QTBUG-151711 again: the visible yield changed
from 36.0 to 54.3 while the shadow input retained 36.0. `SharedTextField` now
notifies a shared `AccessibleValueChange` helper on `displayTextChanged`. On Wasm,
the helper emits the public `QAccessibleValueChangeEvent` that the existing Qt
handler supports; native is a no-op. It uses the accessible display value, so
masked fields retain Qt's masking. The original Qt-only reproduction remains
unchanged and does not use the workaround.

The signal choice matters: `QQuickTextInputPrivate::finishChange()` emits
`textChanged` before updating display text (`qquicktextinput.cpp:3818-3824`),
whereas `displayTextChanged` follows that update (`:3064-3077`). An immediate
notification from `textChanged` reproduced the old value. Remove this workaround
after a stock Qt release handles TextUpdated and passes both the raw reproduction
and the shared-field regression. This does not address QTBUG-151710's missing
accessible names or claim screen-reader acceptance.


## Finding C: ComboBox is absent from the browser accessibility tree

Stock Qt 6.12 assigns `QAccessible::ComboBox` in `qtdeclarative/src/quicktemplates/qquickcombobox.cpp:2413-2427`, but `qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:375-575` has no mapping for that role and its event dispatcher at `:1452-1516` has no handler. The Basic style's noneditable internal TextField (`qtdeclarative/src/quickcontrols/basic/ComboBox.qml:46-65`) appears as a disabled field instead. The real recipe sort picker reproduced this: the named ComboBox was missing and its visible options appeared only as static text. The isolated Qt-only reproduction now also includes an explicitly named ComboBox.

This is already covered by [QTBUG-144330](https://bugreports.qt.io/browse/QTBUG-144330), whose ComboBox work remains TODO, and pending [Gerrit 680803](https://codereview.qt-project.org/c/qt/qtbase/+/680803), “wasm: a11y - Support QComboBox” (NEW, dev branch, patchset 26, updated October 1; checked October 20, 2026). [Gerrit 771466](https://codereview.qt-project.org/c/qt/qtbase/+/771466) is another WIP implementation and [Gerrit 725692](https://codereview.qt-project.org/c/qt/qtbase/+/725692) adds tests. No duplicate was filed. The browser recipe sort selector uses an accessible Button and dialog of named Button choices; native controls are unchanged. Recheck this workaround when upstream support ships.

## Finding D: ListItem name updates remove an editable descendant

The shared String Browser page exposed a missing editor in the Wasm accessibility DOM when its row used `Accessible.ListItem`; changing the row to `Accessible.Grouping` exposed the nested editor and restored keyboard editing. The application flow was re-tested on 2026-10-22: typing then Return saved once, Back released the editor, and clearing the text refreshed the row. The isolated Qt-only source was then built and reproduced the same defect: before NameChanged the listitem div contained an editable input; afterward it contained only the new row name. Filed as [QTBUG-151770](https://qt-project.atlassian.net/browse/QTBUG-151770). Actual screen-reader behavior remains untested.

**Minimal Qt-only source:** [`ListItemEditableChildRepro.qml`](ListItemEditableChildRepro.qml). Build the existing Qt 6.12 Wasm diagnostic target through Qt Creator and open `/wasm/?accessibility-repro=list-item`; this loads the fixture without application controls or HTTP backends. Enable the screen-reader accessibility DOM, inspect the nested editable input, then activate “Change accessible row name.” The input should remain under the row. The ordinary `/wasm/` route and existing `/wasm/?accessibility-repro` fixture remain unchanged.

**Source evidence:** Qt 6.12.0 `qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:752-760` handles `QAccessible::NameChanged` for `QAccessible::ListItem` by calling `setNamedProperty(iface, "innerText", QAccessible::Name)`. `setNamedProperty` at lines 332-335 calls `setProperty`, whose overload at lines 311-315 assigns the value to the DOM element property. The row's child `EditableText` is created as a browser `<input>` in the same file at lines 528-535. This supports the mechanism but does not replace the isolated runtime reproduction.

**Upstream search, 2026-10-22:** exact official-site searches for `handleListItemUpdate`, `ListItem is a div`, `QAccessible::NameChanged` with `innerText`, and QTBUG + Wasm accessibility did not return a matching issue or change. The official Qt 6.12 Wasm documentation describes accessibility as basic and notes that complex table/tree controls may be incomplete ([Qt for WebAssembly, Accessibility and Screen Readers](https://doc.qt.io/qt-6.12/wasm.html#accessibility-and-screen-readers)). The known umbrella [QTBUG-144330](https://bugreports.qt.io/browse/QTBUG-144330) is relevant, but I could not verify from the tracker search whether it already covers this `ListItem` behavior. The isolated reproduction and direct umbrella check are recorded below.

The authenticated tracker check on 2026-10-22 confirmed QTBUG-144330 marks List support complete. Searching `ListItem` + `accessibility` returned five unrelated reports; `innerText` + Wasm/WebAssembly returned none. The isolated reproduction and report use Qt-only code.


## Finding E: checkbox accessibility state remains checked after programmatic reset

Filed [QTBUG-151799](https://qt-project.atlassian.net/browse/QTBUG-151799) on 2026-10-24 after reproducing the earlier checkbox observation in the Qt-only `AccessibilityRepro.qml`. Enable accessibility, activate Example checkbox through its browser accessibility element, then activate Reset checkbox. QML and the canvas return to false; Safari's macOS accessibility tree still exposes checked = 1. This does not claim actual VoiceOver announcement verification.

Qt 6.12 `qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:907-925` updates checkbox state through the checked content attribute; its bool helper at `:303-309` adds/removes the attribute. A live checked property can diverge after user activation. The group-box checkbox path at `:929-946` uses the same update. Focused authenticated tracker searches found no matching state-sync report; the new report references QTBUG-144330.

`SharedStyledSwitch` now exposes its matching `Accessible.Switch` role in both clients. Qt's Wasm Switch handler updates `aria-checked` (`qwasmaccessibility.cpp:955-971`), and Cocoa maps that role to the native switch subrole (`qcocoaaccessibility.mm:211-212`). Browser preference writes and native restores now expose the correct switch state. The raw CheckBox probe remains unchanged by this shared-control adjustment.

## Pending isolation: custom numeric values

On 2026-10-22 a custom numeric control visibly changed 10→15 and its accessible label changed, while the DOM input `.value` remained `10`. The preference was restored. This application/DOM observation still needs a standalone reproduction before filing another report.

Qt 6.12 Wasm creates Slider/SpinBox/Dial as `input type=number` (`qwasmaccessibility.cpp:461-476`); their handlers set the live value property (`:1084-1119`, helper `:311-315`). Absence of `aria-valuenow` is expected. `qtdeclarative/src/quick/items/qquickaccessibleattached.cpp:349-404` connects valueChanged during construction only when a value interface exists; its later role setter does not establish that connection. That is a candidate explanation, not a confirmed runtime cause. Isolate a Qt-only Item with a value property, Slider accessibility role and a button changing 10→15, then verify the exposed value and search the tracker. Keep reports free of product names and application data.
