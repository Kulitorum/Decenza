# shot-uploads Specification

## Purpose
One path for every shot upload, whatever the destination (Visualizer, the Decent account): when a shot is sent, under the shared upload settings, one shot at a time per destination.

## Requirements

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

#### Scenario: Shot deleted on Visualizer
- **WHEN** a shot's Visualizer copy was deleted and the user uploads it
- **THEN** the PATCH's 404 clears the dead link and the shot is uploaded again

### Requirement: One shot at a time per destination

Each destination SHALL receive one shot at a time; shots arriving while a request is out SHALL wait in that destination's queue and be sent in order.

#### Scenario: Shot saved during an upload
- **WHEN** a shot is saved while another shot is uploading to the same destination
- **THEN** it is sent when that upload finishes

### Requirement: Review-page edits are sent once, on close

While the post-shot review page has a shot open, its own field-by-field saves SHALL NOT each be sent; they SHALL be sent once when the page closes, if any were made since it opened or since the user last tapped Upload, and only to destinations already holding the shot. Edits to that shot from anywhere else SHALL still be sent at once.

#### Scenario: Several edits, then close
- **WHEN** the user changes the rating, notes and dose on the review page and closes it, with automatic update on
- **THEN** each destination holding the shot receives one update

#### Scenario: Upload, then close without editing
- **WHEN** the user taps Upload (with an unsaved edit or not) and then closes the page without further edits
- **THEN** nothing more is sent

#### Scenario: Edit from elsewhere while the page is open
- **WHEN** an MCP tool edits the shot while its review page is open, with automatic update on
- **THEN** each destination holding the shot is updated at once

### Requirement: Decent and Visualizer behave the same

Every send of a saved shot, first upload or update, to either destination, SHALL get 3 attempts, 2 s then 4 s apart, made by the shared upload path. Each destination SHALL map its server's response to one shared set of results — sent, nothing to send, transient, sign-in needed, account refused, rejected — and the shared path SHALL record them the same way for both: transient on the third attempt records the shot as failed for that destination, a rejection records it as rejected with its status, and success clears both. A sign-in or account problem SHALL record nothing on the shot. Where the two servers agree, the result for a response SHALL be the same: a transport failure or timeout, 408, 429 and 5xx are transient, 401 needs sign-in, and any other 4xx is a rejection, except 404, 405 or 410 from the upload endpoint, which are transient.

#### Scenario: Server error on either destination
- **WHEN** a Visualizer upload and a Decent upload of the same shot each get HTTP 503 on every attempt
- **THEN** each destination makes 3 attempts, 2 s then 4 s apart, and records the shot as failed

#### Scenario: Rejected by Visualizer
- **WHEN** Visualizer answers a first upload with HTTP 422
- **THEN** the shot is recorded as rejected for Visualizer with status 422 and is not offered by Upload missing shots

#### Scenario: Edit that fails
- **WHEN** a PATCH to Visualizer for an edited shot times out on all 3 attempts
- **THEN** the edit stays unsent and is offered by Upload missing shots, as a Decent replace that fails is
