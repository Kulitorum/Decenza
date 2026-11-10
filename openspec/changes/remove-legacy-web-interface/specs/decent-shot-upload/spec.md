# Spec Delta

## MODIFIED Requirements

### Requirement: Edited shots are re-uploaded with replace
When the metadata of an uploaded shot changes, the system SHALL re-upload it with `?replace=1` through the shared upload path, while the Decent switch and the shared "Auto-update shots" setting are on. A change that cannot be sent (upload off, sign-in needed, offline) SHALL be remembered on the shot and sent when uploading resumes. Writes the uploader makes to record its own upload state SHALL NOT trigger a re-upload.

#### Scenario: Rating added after upload
- **WHEN** the user rates a shot that was already uploaded
- **THEN** the shot is re-posted with `?replace=1`
- **AND** the account copy shows the new rating

#### Scenario: Edit while offline
- **WHEN** an uploaded shot is edited while the Decent server is unreachable
- **THEN** the shot stays marked replace-pending and is offered by Upload missing shots

#### Scenario: Recording upload state
- **WHEN** the uploader records a successful upload on a shot
- **THEN** that write does not cause another upload of the same shot

#### Scenario: Edit from any surface re-uploads

- **WHEN** a shot's metadata is changed from the post-shot review, shot detail, an MCP tool, the AI advisor or the change-beans dialog
- **THEN** it is re-uploaded with `?replace=1` if auto-update is on


## REMOVED Requirements

### Requirement: Web and MCP settings parity
**Reason**: The separate HTML interface and its unused endpoints are removed.
**Migration**: Use the shared browser interface or MCP.

