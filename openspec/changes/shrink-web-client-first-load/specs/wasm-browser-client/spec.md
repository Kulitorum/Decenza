# Spec Delta

## ADDED Requirements

### Requirement: Compressed static asset delivery
The host SHALL serve each large static client asset Brotli-compressed to browsers that accept Brotli, and uncompressed otherwise; Brotli SHALL be the only packaged compressed encoding. The Brotli copy SHALL be verified against its packaged size and hash before it is served, and a copy that fails verification SHALL be reported as an integrity failure, never substituted. Encoding SHALL NOT change an asset's URL, caching or package-identity checks.

#### Scenario: Browser accepts Brotli
- **WHEN** a browser requests a client asset and accepts Brotli
- **THEN** the host responds with the verified Brotli body, declares the Brotli content encoding, and varies the response on accepted encodings

#### Scenario: Browser does not accept Brotli
- **WHEN** a browser requests a client asset without accepting Brotli
- **THEN** the host responds with the verified uncompressed body

#### Scenario: Brotli copy fails verification
- **WHEN** the packaged Brotli copy does not match its recorded size or hash
- **THEN** the host does not serve it
- **AND** the request fails with an integrity error instead of silently substituting the uncompressed body

#### Scenario: Release package lacks Brotli copies
- **WHEN** a release bundle is packaged without a Brotli copy of the Wasm runtime
- **THEN** packaging fails rather than shipping a release that only offers the larger encoding

### Requirement: On-demand image resources
Emoji and screensaver map images SHALL NOT be part of the client's initial download. The browser SHALL fetch each image from the serving host the first time a screen displays it, at a URL derived from the content of its image set, with private immutable caching. A host update that leaves a set unchanged SHALL keep its URLs, so cached images are reused. These requests SHALL follow the same access rules as other client assets.

#### Scenario: First screen with emoji
- **WHEN** the client shows a screen containing emoji for the first time
- **THEN** only the emoji images that screen displays are requested
- **AND** the screen is usable before those images arrive

#### Scenario: Host update without emoji changes
- **WHEN** the host is updated and the emoji set is unchanged
- **THEN** the updated client requests emoji at the same URLs and the browser reuses its cached images

#### Scenario: Image request is refused or fails
- **WHEN** an emoji image cannot be fetched
- **THEN** that image is left blank and the rest of the screen keeps working
- **AND** the emoji is not handed to the browser or platform text renderer as a colour glyph

#### Scenario: Remote access session
- **WHEN** an admitted remote browser displays emoji
- **THEN** the images load through the remote route without additional admission
- **AND** a browser that has not been admitted cannot fetch them

### Requirement: Emoji availability decided without a download
The client SHALL know which emoji have a bundled image without fetching any image. An emoji with no bundled image SHALL be removed from displayed text immediately, exactly as in the native app, and an emoji with a bundled image SHALL be shown once its image arrives.

#### Scenario: Emoji newer than the bundled set
- **WHEN** text contains an emoji for which the bundled set has no image
- **THEN** the client removes it from the displayed text without making a request

#### Scenario: Native app is unchanged
- **WHEN** the native app displays the same text
- **THEN** it resolves emoji from its own bundled images, with the same emoji shown or removed as in the browser

### Requirement: Client-retained runtime
The browser client SHALL keep the downloaded Wasm runtime in script-managed browser storage, keyed by its content-versioned URL, and SHALL start from that copy on later visits without downloading it again. When storage is unavailable, full, or holds a copy that fails to compile, the client SHALL download the runtime as it would without storage, and SHALL NOT fail to start because of storage. Copies for other runtime versions SHALL be removed once the current one is stored.

#### Scenario: Repeat visit on iOS Safari
- **WHEN** a user opens the interface again on a browser that already stored the current runtime
- **THEN** the runtime is not requested from the host
- **AND** the interface starts from the stored copy

#### Scenario: Host update
- **WHEN** the host has been updated since the runtime was stored
- **THEN** the client downloads the new runtime from its new URL, stores it, and removes the old copy

#### Scenario: Storage cannot be used
- **WHEN** browser storage is unavailable, refuses the write, or returns a copy that does not compile
- **THEN** the client starts from a normal download
- **AND** a stored copy that did not compile is discarded
