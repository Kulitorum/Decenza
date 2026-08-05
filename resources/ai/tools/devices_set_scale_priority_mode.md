# devices_set_scale_priority_mode

Sets the persistent scale connection-priority backoff policy.

| Mode | Behaviour |
|---|---|
| `enforce` (default) | The normal dual-HIGH backoff: on a detected stall/fault cluster the scale link latches to BALANCED and reconnects |
| `observe` | Detect-and-log only. Detection still runs but takes no action: the link is forced to HIGH (overriding, but not erasing, an existing BALANCED latch), and would-back-off and recovery events are logged and surfaced in `devices_connection_status` |

`observe` exists so the backoff's aggressiveness can be evaluated on a production build.

## Two things that surprise callers

**The mode is not scoped at all.** It persists across app restarts AND build upgrades until
explicitly changed — unlike the latch itself, which is epoch-scoped.

**The write is eventually consistent.** The change is queued onto the BLE-manager thread, so the
response does not assert that the persist has executed yet. The HIGH-forcing additionally applies
only on the next scale (re)connect; the current connection is not torn down.
