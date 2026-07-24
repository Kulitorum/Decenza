## ADDED Requirements

### Requirement: Upload embeds the recipe parameters

The Visualizer upload SHALL embed a `recipe` object, containing the high-level editor parameters, inside the uploaded profile JSON for every recipe-based profile (D-Flow, A-Flow, and the simple pressure/flow editors). The `recipe` object SHALL carry a schema `version` marker. The editor type SHALL continue to be conveyed by the profile name convention ("D-Flow /…", "A-Flow /…") and SHALL NOT be added as a field to the profile JSON.

#### Scenario: D-Flow profile upload includes recipe

- **WHEN** a D-Flow profile is uploaded to visualizer.coffee
- **THEN** the uploaded profile JSON contains a `recipe` object with the profile's fill, infuse, and pour parameters and a schema `version`
- **AND** the profile JSON contains no editor-type field

#### Scenario: A-Flow structural toggles are included

- **WHEN** an A-Flow profile with structural options (e.g. split pressure ramp, second fill, flow-up extraction) is uploaded
- **THEN** the `recipe` object records those toggle values so the exact frame structure can be reproduced

#### Scenario: Advanced profile upload omits recipe

- **WHEN** a hand-built advanced profile (not created by a recipe editor) is uploaded
- **THEN** no `recipe` object is added and the upload is unchanged

### Requirement: Import reconstructs the editor from the recipe

When an imported profile carries a `recipe` object with a recognized schema `version`, the app SHALL reconstruct the D-Flow/A-Flow (or simple pressure/flow) editor exactly from those parameters, for arbitrary user-created profiles, without deriving parameters from the frames.

#### Scenario: Faithful round-trip of a user D-Flow profile

- **WHEN** a user-created D-Flow profile is uploaded and then imported back
- **THEN** the reconstructed profile's recipe parameters equal the original's (fill/infuse/pour pressures, flows, temperatures, times, weights, and target weight/volume)
- **AND** opening it in the D-Flow editor shows the original values, not defaults

#### Scenario: Faithful round-trip of a user A-Flow profile

- **WHEN** a user-created A-Flow profile with non-default structural toggles is uploaded and then imported back
- **THEN** the reconstructed profile is A-Flow with the same toggles and parameters, and its generated frames match the original

### Requirement: Missing recipe imports as an advanced profile

When an imported profile has a D-Flow/A-Flow name but no `recipe` object (a foreign upload, or one predating this change), the app SHALL import it as an advanced profile using the profile's frames, and SHALL NOT present a recipe editor populated with default parameters. The app SHALL NOT attempt to reconstruct recipe parameters from the frames.

#### Scenario: Foreign D-Flow-named profile without recipe

- **WHEN** a profile titled "D-Flow / …" is imported and carries no `recipe` object
- **THEN** it is imported as an advanced profile whose frames match the download
- **AND** no default-filled D-Flow editor is shown for it

#### Scenario: Unrecognized recipe version

- **WHEN** an imported profile carries a `recipe` object whose schema `version` the app does not recognize
- **THEN** the app imports it as an advanced profile rather than misreading the recipe

### Requirement: Recipe survives the standard file-import path

The recipe reconstruction SHALL apply to profiles imported from a downloaded profile file (the Visualizer website "Download this profile" file, imported via the standard File/Tablet import), not only to profiles fetched through the in-app share-code importer. A downloaded profile file SHALL be a valid, working profile importable into any DE1 app.

#### Scenario: File import reconstructs the recipe

- **WHEN** a profile file downloaded from visualizer.coffee that contains a `recipe` object is imported via File/Tablet import
- **THEN** the app reconstructs the same D-Flow/A-Flow editor as the in-app share-code importer would

#### Scenario: Recipe key is backward-compatible

- **WHEN** the downloaded profile file is imported by an app that does not understand the `recipe` key
- **THEN** the extra key is ignored and a fully working profile is imported from the frames

### Requirement: Preinfuse frame count is preserved on import

On import, the app SHALL derive the preinfuse frame count from the profile's `target_volume_count_start` field when present, rather than resetting it to zero.

#### Scenario: Preinfuse count round-trips

- **WHEN** a profile whose preinfuse frame count is 2 is uploaded and imported back
- **THEN** the imported profile's preinfuse frame count is 2, not 0
