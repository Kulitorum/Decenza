## ADDED Requirements

### Requirement: Barista and notes reach Visualizer as written

The upload SHALL carry the barista as `app.data.settings.my_name`, the key Visualizer's parser reads. Notes SHALL be sent Markdown-escaped on upload (Visualizer renders them as Markdown) and as HTML paragraphs and line breaks on a shot or bag PATCH (Visualizer reads them as HTML). Notes read from Visualizer, including by shot recovery, SHALL be converted from HTML to plain text. A request Visualizer refuses with a JSON `error` SHALL show that message.

#### Scenario: Multi-line note edited
- **WHEN** the user edits a note to two lines and the update is sent
- **THEN** Visualizer shows the note on two lines

#### Scenario: Barista uploaded
- **WHEN** a shot with a barista is uploaded
- **THEN** the Visualizer shot shows that barista

#### Scenario: Disabled account
- **WHEN** Visualizer refuses an upload with 403 and a JSON error
- **THEN** the upload status shows Visualizer's message rather than "HTTP 403"
