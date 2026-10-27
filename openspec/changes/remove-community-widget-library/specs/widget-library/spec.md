# Spec Delta

## Purpose

The widget library stores layouts, widgets and themes on this device so a user can save, reapply and manage them. It is a device-local store with no network sharing service behind it.

## ADDED Requirements

### Requirement: Widget library is device-local
The widget library SHALL let a user save, apply and delete entries on this device. It SHALL NOT offer any way to browse, download, upload, flag or delete entries on a remote sharing service, in the app, the web interface or the remote UI.

#### Scenario: In-app library shows only local entries
- **WHEN** the user opens the library panel in the layout editor
- **THEN** the panel lists only entries stored on this device
- **AND** it offers no Community tab, share control, delete-from-server control or browse-all control

#### Scenario: Web editors show only local entries
- **WHEN** the user opens the web layout editor or the web theme editor
- **THEN** its library section lists only entries stored on this device, with no Community tab

#### Scenario: Local library operations still work
- **WHEN** the user saves the current layout to the library and later applies it
- **THEN** the entry is saved and applied exactly as before this change

#### Scenario: Entries downloaded before the removal remain usable
- **WHEN** the library holds an entry that was downloaded from the community library before this change
- **THEN** the entry is listed, applied and deleted like any other local entry

### Requirement: No widget library traffic leaves the device
The app SHALL NOT send any request to the widget-library sharing backend (`api.decenza.coffee/v1/library`) for any user action or background activity.

#### Scenario: Using the library makes no sharing request
- **WHEN** the user opens the library panel and applies an entry
- **THEN** no request is made to the widget-library sharing backend

### Requirement: The web server exposes no community library endpoints
The built-in web server SHALL NOT serve community library routes. A request to a former `/api/community/` library path (browse, download, upload, delete, thumbnail) SHALL get the same not-found response as any other unknown API path.

#### Scenario: Former community endpoint is not found
- **WHEN** a client sends `GET /api/community/browse` to the built-in web server
- **THEN** the server answers as it does for an unknown API path and makes no outbound request

### Requirement: No community library destination
The app SHALL have no Community library page. No navigation route, AppShell request or Custom-widget action SHALL open one, and the Custom-widget action picker in either editor SHALL NOT offer "Go to Community".

#### Scenario: Action pickers do not offer Go to Community
- **WHEN** the user opens the action picker for a Custom widget in the in-app or the web layout editor
- **THEN** "Go to Community" is not listed

#### Scenario: Stored Go to Community action is inert
- **WHEN** a layout saved before this change has a Custom widget whose stored action is `navigate:community`, and the user taps it
- **THEN** no page opens and the app does not crash
- **AND** the stored action value is left unchanged in the layout
