## ADDED Requirements

### Requirement: The Build Enables `-Wall -Wextra -Werror` By Default
The project SHALL build with `-Wall -Wextra -Werror` (and the MSVC equivalent, `/W4 /WX`) enabled by default on all six supported platforms, from the first commit of this change rather than at the end of it.

The flags go on immediately. Diagnostic classes with existing occurrences are carried as explicit `-Wno-<name>` exemptions, so the enforcement is real from day one for everything else, and the change completes when the exemption block is empty.

#### Scenario: Default build rejects a new warning
- **WHEN** a contributor introduces code producing any `-Wall` or `-Wextra` diagnostic outside an exempt class, and builds with no special flags
- **THEN** the build fails, on every supported platform

#### Scenario: End state reached
- **WHEN** the last exemption is removed
- **THEN** the exemption block is deleted from `CMakeLists.txt` and `-Wall -Wextra -Werror` stands alone with nothing carved out of it

### Requirement: Diagnostics Add No Significant Build Time
Enabling the diagnostics SHALL NOT measurably slow a build, and sanitizer instrumentation SHALL NOT be applied to Release builds.

`-Wall -Wextra` costs effectively nothing — the analyses run during ordinary semantic analysis whether or not diagnostics are emitted. The cost risk is sanitizer instrumentation, which is scoped by configuration rather than assumed harmless.

Note the deliberate asymmetry, which an earlier draft of this spec had backwards by requiring instrumentation to be off by default everywhere: desktop **Debug** builds are instrumented automatically, because a sanitizer nobody remembers to enable catches nothing. Debug builds are already unoptimised and not performance-sensitive; Release builds are, and are untouched.

#### Scenario: Release build
- **WHEN** a Release build is configured
- **THEN** no sanitizer instrumentation is applied and build time is materially unchanged from before this change

#### Scenario: Debug build
- **WHEN** a developer configures a desktop Debug build without naming any sanitizer option
- **THEN** ASan and UBSan are applied automatically, and the resulting slowdown is accepted as the cost of a detector that is actually running

### Requirement: The Backlog Is An Explicit Exemption List That Only Shrinks
The diagnostic classes not yet cleared SHALL be recorded as explicit `-Wno-<name>` entries in a single labelled block in `CMakeLists.txt`, and that block SHALL only ever have entries removed.

The list is the backlog made visible in the build file rather than tracked in a CI artifact. Its length is the remaining work, and the change is complete when the block is empty and deleted. Because everything absent from the list is already `-Werror`, no separate count baseline or ratchet is needed — a new warning in a cleared class fails the build outright, which is strictly stronger than failing when a total rises.

#### Scenario: Warning in a cleared class
- **WHEN** a contributor introduces a diagnostic whose class is not on the exemption list
- **THEN** the build fails immediately, with no CI count comparison involved

#### Scenario: Clearing a class
- **WHEN** the last occurrence of an exempt diagnostic is fixed across the tree
- **THEN** the same pull request deletes that `-Wno-<name>` entry, and demonstrates the build green on all six platforms

#### Scenario: An entry is never added back
- **WHEN** a change would require re-adding a previously removed `-Wno-<name>`
- **THEN** it is treated as a regression to fix in that change, not as a reason to reopen the exemption

### Requirement: No Advisory Warning Reporting
The project SHALL NOT rely on a non-blocking CI job that reports warning counts.

A count in a CI log has no forcing function; the exemption list in the build file does. This requirement exists to prevent reintroducing a dashboard that reports a number nobody acts on, which is the failure mode an earlier draft of this design would have shipped.

#### Scenario: Proposal to add warning reporting
- **WHEN** someone proposes a job that compiles with diagnostics enabled and reports the resulting count without failing
- **THEN** it is rejected in favour of removing an entry from the exemption list

### Requirement: The Backlog Is Cleared By Fixing, Not By Re-Suppressing
An exemption SHALL be retired by correcting every occurrence of that diagnostic. It SHALL NOT be retired by moving the suppression somewhere less visible — a `#pragma` wall, a file-level disable, or a per-target `-Wno-<name>` outside the tracked block.

The tracked exemption block is honest: it says "these classes are outstanding" in one visible place, and shrinking it is the work. Moving a suppression elsewhere to shorten that block produces the same green build while hiding the debt, which is the defect shape this project has already shipped in several other forms.

#### Scenario: Retiring an exemption
- **WHEN** a `-Wno-<name>` entry is deleted
- **THEN** the occurrences it covered were fixed, not relocated behind a pragma or a narrower suppression

#### Scenario: Diagnostic is a genuine false positive
- **WHEN** a specific occurrence is provably not a defect
- **THEN** it is suppressed at that one call site with a comment giving the reason, and this is the only acceptable form of surviving suppression

### Requirement: The Initial Exemption List Is Derived From A One-Off Measurement
The exemption list SHALL be built by compiling once per platform with `-Wall -Wextra` and recording the diagnostic classes that actually occur. It SHALL name only classes with real occurrences.

The composition of the backlog is currently unknown — `-Werror=unused-result` is the only compile option in `CMakeLists.txt`, everything else is generator defaults, and the iOS Xcode defaults suppress roughly fifteen classes today (`-Wno-shadow`, `-Wno-conversion`, `-Wno-non-virtual-dtor`, `-Wno-unused-parameter`, and others). Measuring is a one-time task, not a standing job.

#### Scenario: Building the initial list
- **WHEN** the measurement is run across all six platforms
- **THEN** each diagnostic class with at least one occurrence gets a `-Wno-<name>` entry, and classes with no occurrences get none

#### Scenario: Speculative exemption
- **WHEN** a diagnostic class has no occurrences on any platform
- **THEN** it is not added to the list, because an exemption for a problem that does not exist silently permits it later

### Requirement: A Diagnostic Is Promoted to Error Only With Six-Platform Evidence
Before any diagnostic is added as `-Werror=<name>`, it SHALL be demonstrated to compile clean on all six supported platforms (Windows, macOS, iOS, Android, Linux x64, Linux arm64).

This requirement exists because of a specific failure. `-Werror=unused-result` was verified on macOS and Android, then pushed to a release tag; it broke the iOS release build on code inside `#ifdef Q_OS_IOS`, which neither verification platform compiles. Platform-guarded code is invisible to every platform that does not build it, so evidence from a subset is not evidence.

#### Scenario: Promoting a diagnostic
- **WHEN** a maintainer proposes adding `-Werror=<name>`
- **THEN** a build on each of the six platforms is run and shown green before the flag is merged

#### Scenario: Diagnostic fails on one platform
- **WHEN** a candidate diagnostic compiles clean on five platforms and fails on the sixth
- **THEN** the flag is not merged until the underlying findings on that platform are fixed

### Requirement: Exemptions Are Retired One Class At A Time
Each change that shrinks the exemption list SHALL remove exactly one `-Wno-<name>` entry, so that a regression is attributable to a single diagnostic class.

Removing several at once makes a six-platform failure ambiguous and encourages reverting the whole batch rather than fixing the one class that broke.

#### Scenario: Clearing a class
- **WHEN** a pull request retires an exemption
- **THEN** it removes one entry, fixes that class across the tree, and shows the build green on all six platforms

### Requirement: The Limits of Compiler Enforcement Are Documented
The project documentation SHALL state that a clean build under the enforced diagnostics is not evidence of a defect-free codebase, and SHALL record the worked example of why.

`-Werror=unused-result` caught a discarded `SecRandomCopyBytes` result because Apple annotates that function `warn_unused_result`. The identical defect on the OpenSSL path — a discarded `RAND_bytes` result, on the branch that five of the six platforms build — produced no diagnostic at all, because OpenSSL does not carry the annotation. A diagnostic's reach is bounded by third-party annotations, so silence from the compiler carries little information about unannotated APIs.

#### Scenario: A contributor relies on a clean build
- **WHEN** a contributor reads the project's build or contribution guidance
- **THEN** it states that enforced diagnostics cover only annotated APIs, and cites the `SecRandomCopyBytes` / `RAND_bytes` pair as the concrete case

### Requirement: A Deliberately Discarded Result Is Explicit
Where a `[[nodiscard]]` or `warn_unused_result` value is intentionally ignored, the call SHALL be written as an explicit discard with a comment giving the reason the failure is tolerable.

#### Scenario: Intentional discard
- **WHEN** a caller genuinely does not need a checked result
- **THEN** the call is written `(void)f();` with a comment stating why losing that failure is acceptable, rather than being left bare or the diagnostic suppressed file-wide
