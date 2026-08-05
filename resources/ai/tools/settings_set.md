# settings_set

Updates any app setting on the device.

This is the tool for the common dial-in writes: grind size (`dyeGrinderSetting`), dose weight
(`dyeBeanWeight`), drink/yield weight (`targetWeight`), brew temperature
(`espressoTemperature`).

It covers every QML settings tab: machine, calibration, connections, screensaver, accessibility,
AI, espresso, steam, water, flush, DYE metadata, MQTT, themes, visualizer, update, data, history,
language, debug, battery, heater, auto-favorites. API keys and passwords are excluded as
sensitive.

For temperature and weight changes on the active profile, the tool handles the profile update
itself — no separate `profiles_edit_params` call is needed.

**Only call this when the user explicitly asks to change something on the machine.** For
discussion and recommendations, answer in chat instead.
