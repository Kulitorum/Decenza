## MODIFIED Requirements

### Requirement: Two bundled firmware channels
The system SHALL bundle two channels: Stable (the default, DE1 build 1358 from `decentespresso/de1app` commit `7e94ef3c7c68a9b9ae7d188e08e3ee3776780489` `de1plus/fw/bootfwupdate.dat`) and Early access (opt-in, build 1359, a Decenza-specific build supplied by Decent and not published in de1app or decaid). Each channel SHALL resolve to the single bundled manifest entry whose `channel` field is `stable` or `early-access` respectively. The selected image SHALL expose its version, channel label, release notes, expected header fields, byte length, digest and provenance.

#### Scenario: Stable is the default channel
- **WHEN** the user has never opted into Early access
- **THEN** availability is computed against the bundled Stable build 1358

#### Scenario: Early access offers the Decenza build
- **WHEN** the user has opted into Early access
- **THEN** availability is computed against the bundled build 1359
- **AND** the displayed release notes describe its post-espresso hot water window
