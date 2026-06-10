## ADDED Requirements

### Requirement: Bean Base API key is stored in its own settings domain

The system SHALL expose a Bean Base API key setting through a dedicated `SettingsBeanBase` domain reachable in QML as `Settings.beanbase.beanBaseApiKey`. The key SHALL NOT be added as a property on `SettingsVisualizer` or on the `Settings` façade directly.

#### Scenario: QML reads the API key
- **WHEN** QML evaluates `Settings.beanbase.beanBaseApiKey`
- **THEN** the binding resolves to the user-entered string (or empty string if unset)
- **AND** changes to the value emit `beanBaseApiKeyChanged()` so dependent bindings update

#### Scenario: Backup includes the API key
- **WHEN** the user runs a backup
- **THEN** the Bean Base API key is included in the backup payload (via `settingsserializer.cpp`)
- **AND** a restore on a different device populates `Settings.beanbase.beanBaseApiKey` correctly

#### Scenario: Factory reset clears the API key
- **WHEN** the user invokes a factory reset
- **THEN** `Settings.beanbase.beanBaseApiKey` is cleared along with other settings domains

### Requirement: Settings UI exposes API key entry, validation, and signup link

The Visualizer settings tab SHALL include a Bean Base section below the existing Visualizer.coffee account block, containing an API key field, a Test Key button, and a link to the loffeelabs.com signup page.

#### Scenario: User pastes an API key and tests it
- **WHEN** the user pastes a non-empty key into the API key field and taps Test Key
- **THEN** the app sends `GET /beans?limit=1` with the `Authorization: Bearer <key>` header
- **AND** a 200 response renders a success message in the success color
- **AND** a 401 response renders "Invalid API key" in the error color
- **AND** a network failure renders "Could not reach Bean Base" in the error color

#### Scenario: API key is masked by default
- **WHEN** the user types or pastes into the API key field
- **THEN** the field is `echoMode: Password` by default
- **AND** a show/hide affordance toggles to `echoMode: Normal` without losing focus or value

#### Scenario: Signup link opens externally
- **WHEN** the user taps the "Get a free API key from loffeelabs.com" link
- **THEN** the system opens the loffeelabs.com signup URL via the platform browser
- **AND** the app does not navigate away from the Settings page

### Requirement: API key is not exposed in logs

The system SHALL NOT log the Bean Base API key value in plain text. Log lines referring to the key SHALL mask it (e.g., last 4 characters only) or omit it entirely.

#### Scenario: Test Key result is logged
- **WHEN** Test Key runs and a result is logged for diagnostics
- **THEN** the log line records "Bean Base test: success" or similar
- **AND** the API key string does not appear anywhere in the log
