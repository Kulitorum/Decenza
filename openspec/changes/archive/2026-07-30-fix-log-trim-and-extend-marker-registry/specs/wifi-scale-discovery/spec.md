## ADDED Requirements

### Requirement: The retry narrative names the address dialled and why

Each WiFi scale connection attempt SHALL record, at INFO, which address it dialled and
which of the driver's address sources supplied it: a freshly-resolved address, a
successful re-resolution, or the persisted cache after a resolution failure.

Announcing an intent to re-resolve SHALL NOT stand alone as the account of an attempt.
When resolution fails and the driver falls back to the cached address — which it does
deliberately, because a remembered address beats opening no socket at all — that fallback
SHALL be recorded at INFO. Recording it only at DEBUG leaves the INFO narrative asserting
a re-resolve that did not happen, and a reader diagnosing a scale that moved sees repeated
failures against an address the log implies was freshly obtained.

#### Scenario: A cache fallback after failed resolution is visible at INFO

- **WHEN** mDNS resolution for the scale's hostname fails and the driver dials the cached
  address instead
- **THEN** the log records at INFO that resolution failed and names the cached address it
  dialled

#### Scenario: A successful re-resolution is distinguishable from a cache dial

- **WHEN** re-resolution succeeds and the driver dials the freshly-resolved address
- **THEN** the log at INFO distinguishes that attempt from one that fell back to the cache

#### Scenario: A stale cached address is diagnosable from the log alone

- **WHEN** a scale has moved to a new address and the cached address is repeatedly
  unreachable
- **THEN** an INFO-level reading of the session shows the same cached address being dialled
  each cycle and resolution failing each cycle, without requiring the DEBUG tier

#### Scenario: A user-supplied address is still identified as such

- **WHEN** the caller supplies an address from a scan selection or the dialog's "Use"
  action and the driver dials it ahead of both the cache and a re-resolve
- **THEN** the log identifies that address as the caller-supplied one
