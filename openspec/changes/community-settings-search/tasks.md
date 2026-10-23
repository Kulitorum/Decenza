## 1. Data

- [x] 1.1 `CommunityAggregator::summarize` (tst_communityaggregator)
- [x] 1.2 `CommunitySettingsClient` on `GET /api/shots?community=1&full=1`, bean and canonical requests merged, auth, caches, profile fetch (tst_communitysettingsclient)
- [x] 1.3 Settings: search on/off, minimum enjoyment with a Settings → Visualizer control and backup, API address, cross-grinder grind (off)

## 2. App

- [x] 2.1 Card: your profiles and last grind, community summary, few-shots list, Retry, Open on Visualizer, web research; opened per bag from a Suggestions button as a dialog
- [x] 2.2 Apply: confirmation sheet → new numbered recipe → activate
- [x] 2.3 Create recipe → wizard prefill, importing a missing profile
- [x] 2.4 New-bag window: Suggestions button, inline card, Apply or Create recipe as one choice run on save

## 3. Parity

- [x] 3.1 MCP `bean_search` `action=community`; surface version 1.20.0
- [x] 3.2 Web Beans page community block with the Visualizer link

## 4. Verification

- [x] 4.1 Full suite through Qt Creator MCP — 123/123, QML gate 274/274 (2026-10-23)
- [x] 4.2 In the app against live Visualizer (2026-10-23): per-bag Suggestions dialog (history, community summary, few-shots fallback, From the web); new-bag window card, Apply/Create as one choice, saving with Apply created and selected "Gran Bar · Slayer /w Fill" with the profile imported. Not exercised in the app: Create recipe on save, a numbered repeat Apply (covered by tst_recipestorage), the Minimum enjoyment control.
- [x] 4.3 Wiki manual: "Dialing Suggestions" section and a Suggestions bullet in Bean Bags (wiki 0671f43)
