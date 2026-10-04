## MODIFIED Requirements

### Requirement: Decenza Ships Stock Qt Runtime Binaries
The application SHALL be packaged against the Qt binaries the upstream installer provides. The
project SHALL NOT ship a patched Qt runtime artifact — platform plugin, jar, framework or library —
in place of a stock one.

A patched runtime artifact carries costs that outlive the bug it fixes: it is ABI-locked to one Qt
version, it must be rebuilt or deleted at every bump, and it is a binary in the tree that only its
author can reproduce. Where an upstream bug is worth fixing, the fix belongs upstream, and the
project's position is to wait for the release that carries it rather than to fork.

This SHALL NOT be read as a claim that no upstream bug affects Decenza. It is a decision about where
the fix lives.

A Qt module that the open-source installer does not provide (Qt MQTT is one: Qt publishes its
binaries only to commercial licensees) MAY be compiled into the application from its unmodified
upstream source. The source SHALL be fetched at the release tag matching the Qt version found at
configure time, never from a version written into the repository, so a Qt bump moves it with no
edit. The build SHALL fail at configure time if the fetched source declares a different Qt version
than the one found. The compiled module SHALL NOT be committed to the repository or placed in the
installed Qt tree.

#### Scenario: An upstream Qt bug affects the app
- **WHEN** a Qt defect is identified that degrades Decenza on some platform
- **THEN** the remedy SHALL be an upstream patch, a workaround in Decenza's own code, or an accepted
  known issue — not a patched Qt binary committed to this repository

#### Scenario: The decision is revisited
- **WHEN** field crash or defect reports show an unpatched upstream bug occurring at a rate that
  justifies packaging a binary again
- **THEN** the change that reintroduces one SHALL state the observed rate it is responding to, and
  SHALL restore the version-lock guard that fails the build on a Qt/artifact mismatch before the
  artifact is packaged

#### Scenario: A Qt upgrade lands
- **WHEN** the pinned Qt version changes
- **THEN** no step in any workflow SHALL replace a file in the installed Qt tree with one built
  elsewhere, so a bump cannot produce a package assembled from a mixture of Qt versions
- **AND** *removing* a stock Qt file for packaging reasons is explicitly permitted — the Linux and
  Linux-arm64 workflows delete unused SQL, image-format and position plugins before `linuxdeploy`
  runs, because those plugins pull external dependencies an AppImage cannot satisfy. Deleting a
  file cannot introduce a foreign version; substituting one can. The rule is about provenance, not
  about the Qt tree being read-only
- **AND** a Qt module compiled from source SHALL follow the new version on the next configure,
  with no change to any file in the repository

#### Scenario: A stale module source survives a Qt bump
- **WHEN** a build directory or a local source override still holds Qt module source from a
  different Qt version than the one CMake found
- **THEN** configure SHALL fail with an error naming both versions, rather than building against
  the mismatched source

#### Scenario: Qt has no matching module release
- **WHEN** the Qt version found has no matching release tag for a module compiled from source
- **THEN** configure SHALL fail with an error naming the Qt version and the module
