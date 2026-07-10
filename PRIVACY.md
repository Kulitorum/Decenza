# Privacy Policy for Decenza

**Last updated: July 2026**

## Overview

Decenza is an open-source application for controlling Decent Espresso DE1 machines. We are committed to protecting your privacy and to being transparent about every network connection the app can make.

## Data Collection

The app runs on your device and stores your settings, shot history, and preferences locally. It does not send data to us automatically for the core espresso-machine functionality.

However, Decenza has a number of **optional network features**, several of which reach the internet. Some of these send an approximate location or a random per-install identifier. They are documented in full below. Except where noted, these features only transmit data when you enable them or explicitly trigger the action.

## Network Connections

The following is a complete list of the servers Decenza can contact and why:

- **Your DE1 espresso machine** and **Bluetooth scale** — local Bluetooth only; no internet.

- **Shot Map** (`api.decenza.coffee`) — **optional, off by default.** If you turn on the Shot Map, then on every shot the app POSTs your coarse location (rounded to ~11 km / ~7 miles), the profile name, and the app version to decenza.coffee to plot anonymous shots on a community map. No account is used. Turning this feature on is what enables location acquisition (see *Location* below).

- **Location & reverse geocoding** (`nominatim.openstreetmap.org`) — the app only acquires a location fix and looks up your city/country name (via OpenStreetMap's Nominatim service) when a location-consuming feature is enabled — currently the Shot Map. If no such feature is on, no location fix is taken and nothing is sent to Nominatim. The location OS permission is also required for Bluetooth scanning on mobile, but scanning does **not** cause a location fix or any network request.

- **Weather** (`open-meteo.com`, `api.weather.gov` / NWS, `api.met.no` / MET Norway) — if you add the Weather widget to a layout, the app sends your approximate coordinates to a weather provider (chosen by region) to fetch a local forecast. Weather uses whatever location another feature has already obtained; it does not itself acquire a new fix.

- **Automatic update check** (`api.github.com`) — **optional, off by default.** When enabled, the app periodically GETs the public GitHub releases list for this repository to see whether a newer version exists. This is a plain read of public release metadata; no personal data is sent.

- **Crash reporter** (`api.decenza.coffee/v1/crash-report`) — **user-submitted only.** If the app crashes, you may choose to submit a crash report (log tail plus any notes you type). It is sent to decenza.coffee, which files a GitHub issue. Nothing is sent unless you press submit.

- **Widget / layout Library sharing** (`api.decenza.coffee/v1/library`) — **user-initiated.** When you upload a widget/layout, browse, or download from the community library, requests are sent to decenza.coffee. These requests include an `X-Device-Id` header containing a random UUID generated once per installation (used for anonymous authentication and to let you manage your own uploads). This UUID is not tied to your identity.

- **Visualizer.coffee** (optional) — if you enable shot uploads, your shot data, metadata, and approximate location (if location is enabled) are sent to visualizer.coffee. See their privacy policy at https://visualizer.coffee.

- **AI analysis** (optional) — if you enable AI shot analysis, shot data is sent to the AI provider you configure (OpenAI, Anthropic, Google, OpenRouter, or a local/Ollama endpoint) using your own API key. Data handling is governed by your chosen provider's policy.

- **Translations** (`api.decenza.coffee`) — if you download an additional UI language, the language list and language file are fetched from decenza.coffee.

- **Remote control relay** (`wss://ws.decenza.coffee`) — if you enable remote access, the app opens a websocket to a relay server so you can control the machine from another device. Only active when enabled.

- **DE1 firmware & screensaver downloads** — firmware images and optional screensaver videos are downloaded from their respective sources when you use those features.

## Local Web Server (LAN)

Decenza can run an optional on-device web server so you can view/edit settings from a browser on your local network. The settings page never returns your stored secret values (API keys, Visualizer/MQTT credentials): configured secrets are shown redacted, and a saved value is only overwritten when you type a new one. Settings backups taken over this web server exclude those secret values as well (API keys plus Visualizer/MQTT usernames and passwords).

## Permissions

- **Bluetooth** — communicate with your DE1 machine and Bluetooth scales.
- **Location** — required by the mobile OS for Bluetooth scanning; also used, only when you enable a location feature such as the Shot Map, to tag shots with your approximate location (~11 km / ~7 miles).
- **Internet** — used for the optional network features listed above.

## Data Storage

All settings, shot history, and preferences are stored locally on your device. No data is transmitted to us or any third party except through the optional features described above.

## Open Source

Decenza is open source. You can review the complete source code at:
https://github.com/Kulitorum/Decenza

## Contact

For questions about this privacy policy, please open an issue on GitHub:
https://github.com/Kulitorum/Decenza/issues

## Changes

Any changes to this privacy policy will be posted in this file and in the app's repository.
