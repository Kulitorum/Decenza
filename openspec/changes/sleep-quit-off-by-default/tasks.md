## 1. Default and consumers

- [x] 1.1 Add `SettingsNetwork::sleepOptionDefaults()` (`allowQuit: false`, `showIcon: true`) and `sleepOptionDefaultsJson()`.
- [x] 1.2 Make SleepItem, LayoutItemDelegate's compiled Sleep tile, SleepEditorPopup and SettingsLayoutTab read the default instead of a literal.
- [x] 1.3 Inject `SLEEP_DEFAULTS` into the web layout editor and read it there.
- [x] 1.4 Gate the idle page's fake-shot corner on `Settings.app.isDebugBuild || DE1Device.simulationMode`.

## 2. Tests

- [x] 2.1 Add `tst_settings::sleepWidgetDoesNotQuitOnLongPressByDefault`.
- [x] 2.2 Add `tst_customwidgethtml::sleepDefaultsComeFromOneTable`, covering all five consumers and the compiled tile's gesture.

## 3. Docs

- [ ] 3.1 Update the wiki manual's Sleep entry: long-press-to-quit is off by default and can be enabled in the Sleep widget's options.
