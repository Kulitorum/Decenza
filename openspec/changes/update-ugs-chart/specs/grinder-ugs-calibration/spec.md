# Spec Delta

## MODIFIED Requirements

### Requirement: The calibration section SHALL link the original UGS guide

The calibration section SHALL offer a button that opens the original Universal
Grind Setting guide (`https://videoblurb.com/UGS/`) in an in-app
dialog, loaded live from that URL with Qt WebView so it looks exactly like the
original. The dialog SHALL show the page's current URL above the page and SHALL
offer to open it in the system browser. Nothing SHALL overlap the web view.

#### Scenario: Opening the UGS guide

- **WHEN** the user taps the UGS guide button on the calibration section
- **THEN** a dialog shows the original guide page live from its own site
- **AND** the page's URL is visible above it
