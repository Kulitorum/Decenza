## Purpose

One path for every shot upload, whatever the destination (Visualizer, the Decent account): when a shot is sent, under the shared upload settings, one shot at a time per destination.

## ADDED Requirements

### Requirement: One upload path for every destination

Every upload and update of a saved shot SHALL go through one dispatcher (`ShotUploads`), which applies the shared upload settings once and hands the shot to each destination that is switched on and connected. Destinations SHALL implement only how a shot is sent. The triggers are:

- a shot saved at the end of an extraction, when "Auto-upload shots" is on;
- a successful edit of a saved shot, from any editor (post-shot review, shot detail, ShotServer, MCP, the AI advisor, the change-beans dialog), when "Auto-update shots" is on;
- the Upload button, the "Upload last shot" layout action and MCP `shots_upload`, regardless of the automatic settings.

Each destination SHALL build its payload from the saved row, read after any write already queued.

#### Scenario: Both destinations on
- **WHEN** a shot is saved with automatic upload on and both destinations switched on and connected
- **THEN** the shot is sent to Visualizer and to the Decent account

#### Scenario: Destination switched off
- **WHEN** a destination is switched off or its account disconnected
- **THEN** no new shot is sent to it, and shots already queued for it are dropped

### Requirement: Uploads never duplicate a shot

A shot that a destination already holds SHALL be updated there, never uploaded a second time. Visualizer SHALL PATCH a shot that has a `visualizer_id`; the Decent account SHALL re-send it with `?replace=1`. A shot queued twice for a destination SHALL be sent once.

#### Scenario: Upload an uploaded shot
- **WHEN** the user uploads a shot that is already on Visualizer, from the review page, the layout action or MCP
- **THEN** Visualizer receives a PATCH and no second shot is created

#### Scenario: Second request while the first is out
- **WHEN** a shot's first upload is in flight and the same shot is uploaded again
- **THEN** the second request is sent after the first completes and updates the copy the first created

### Requirement: One shot at a time per destination

Each destination SHALL receive one shot at a time; shots arriving while a request is out SHALL wait in that destination's queue and be sent in order.

#### Scenario: Shot saved during an upload
- **WHEN** a shot is saved while another shot is uploading to the same destination
- **THEN** it is sent when that upload finishes

### Requirement: Review-page edits are sent once, on close

While the post-shot review page has a shot open, its field-by-field saves SHALL NOT each be sent; the edits SHALL be sent once when the page closes, if any were made since it opened or since the user last tapped Upload, and only to destinations already holding the shot.

#### Scenario: Several edits, then close
- **WHEN** the user changes the rating, notes and dose on the review page and closes it, with automatic update on
- **THEN** each destination holding the shot receives one update

#### Scenario: Upload, then close without editing
- **WHEN** the user taps Upload and then closes the page without further edits
- **THEN** nothing more is sent
