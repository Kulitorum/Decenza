# Design

## Context

`maybeAutoConnectBrowsedScale()` is the single auto-connect path shared by three
callers: the user scan's DNS-SD browse handler, the user scan's A-record probe
handler, and the reconnect browse's handler. Sharing it is deliberate — an
earlier duplicate of the saved-scale match drifted from the row dedupe and let a
browse hit rewrite a row out from under the matcher.

Its already-connected branch exists because `main.cpp`'s single-scale invariant
silently drops a `scaleDiscovered` emission while a scale is connected. Routing
through `wifiPrimaryReachable` → `switchToWifiPrimary()` is what gets past that,
because the switch-back drops the current scale *before* emitting.

The branch's condition was `m_scaleDevice && m_scaleDevice->isConnected()`,
which does not distinguish a backup from the primary itself.

## Goals / Non-Goals

- **Goal:** request a switch-back only when there is something to switch back
  *from*.
- **Goal:** keep one implementation shared by all three callers.
- **Non-Goal:** teach `BLEManager` the connected scale's identity. `ScaleDevice`
  exposes no address or hostname accessor, and adding one for this would mean a
  downcast to `DecentScaleWifi` or a new tracked member — more machinery than
  the question needs.

## Decisions

### Use `m_wifiDirectAttemptFailed` as the backup discriminator

The flag's lifecycle already means exactly "the direct attempt to the saved WiFi
primary failed and nothing has since proved otherwise":

- Set in `onScaleConnectionTimeout()`, only for a saved `wifi:` primary and only
  on a non-manual attempt (`blemanager.cpp:1861`).
- Cleared by any connect that is **not** the WiFi→BLE fallback
  (`onScaleConnectedChanged`, `:1722`).
- Cleared when the saved address changes (`setSavedScaleAddress`, `:2069`).

So while a scale is connected, `true` can only mean the connect was the fallback
— i.e. a backup — and `false` means the connected scale is the primary. No new
state is introduced and no existing state changes meaning.

**Alternative considered:** compare the connected device's hostname to the saved
address. Rejected — it needs an accessor `ScaleDevice` does not have, and a
downcast at a layer that otherwise treats scales polymorphically. It would also
be *less* correct in one case: a manually connected third WiFi scale is a backup
by any reasonable reading, and the flag classifies it correctly while a hostname
comparison would too, at strictly more cost.

**Alternative considered:** suppress duplicate switch-back requests (the log
shows two). Rejected as treating the symptom — one needless disconnect is the
bug, not two.

### Express it as a static predicate

`shouldRequestSwitchBack(bool scaleConnected, bool directAttemptFailed)` joins
`shouldBrowseOnReconnect()` and `browsedScaleIsSavedPrimary()`. Same reasoning
as those: the rule is otherwise reachable only by faking a connection timeout,
and the project treats needing fault injection to reach a branch as a stop sign.
As a pure function the four-case truth table is asserted directly, in an
existing test file, at the cost of one test slot.

## Risks / Trade-offs

- **A backup connected by some path that does not set the flag would not trigger
  a switch-back.** The only such path is a scale connected without the primary's
  direct attempt ever having timed out — in which case the app has no evidence
  the primary was unreachable, and overriding the user's connection would be the
  wrong move anyway.
- **Behaviour depends on a flag maintained elsewhere.** Mitigated by the
  predicate's comment naming the three sites that own its lifecycle, and by the
  comment at the clearing site now pointing back at this rule.
