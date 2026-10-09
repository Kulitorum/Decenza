## Why

Holding the Sleep widget quit the app, because a missing `allowQuit` meant "quit enabled". So wiping the home screen, or a wet finger resting on Sleep, could close Decenza. The explicit Quit widget already exists for quitting on purpose.

## What Changes

- **BREAKING (behavioural):** when `allowQuit` is not set on a Sleep instance, long-press does not quit. Layouts that stored `allowQuit: true` keep quit-on-long-press.
- The default is declared once, in `SettingsNetwork::sleepOptionDefaults()`, and read by:
  - the Sleep item;
  - the compiled centre-zone tile;
  - the in-app editor and the layout tab that opens it;
  - the web editor (through an injected `SLEEP_DEFAULTS`).
- The 5-second fake-shot corner on the idle page exists only in debug builds or while simulation is live.

## Capabilities

### Modified Capabilities
- `layout-widget-instance-config`: the Sleep quit option now defaults to off, and every rendering path follows the stored option or the default.

## Impact

- **Code:**
  - `src/core/settings_network.{h,cpp}`
  - `src/network/shotserver_layout.cpp`
  - `qml/components/layout/items/SleepItem.qml`
  - `qml/components/layout/LayoutItemDelegate.qml`
  - `qml/components/layout/SleepEditorPopup.qml`
  - `qml/pages/settings/SettingsLayoutTab.qml`
  - `qml/pages/IdlePage.qml`
- **Tests:** `tst_settings`, `tst_customwidgethtml`.
- **Docs:** the wiki manual's Sleep entry, one sentence.
