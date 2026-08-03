## Context

Decenza's MCP tools are thin wrappers over app operations that were written for
a UI, where the user sees the outcome on screen and no return value is needed.
That is why `ProfileManager::loadProfile`, `SettingsTheme::applyPresetTheme` and
`ScaleDevice::startTimer` are all `void`: a human watching the machine learns
whether the profile switched. A model calling the same operation over MCP learns
only what the tool chooses to say, and today several of them say "success"
because nothing told them otherwise.

The defects group into four mechanisms, and the grouping matters because the fix
differs per mechanism:

1. **A `void` operation with a real failure path.** `loadProfile` refuses an
   unreadable profile and returns early keeping the current one;
   `applyPresetTheme` falls off the end of its loop on an unknown name. The tool
   cannot detect either.
2. **A database call that reports "the statement ran", not "a row changed".**
   `updateShotMetadataStatic` returns `query.exec()`; a `WHERE id = 99999`
   matching nothing is a successful statement.
3. **A signal-driven async tool wired to the success signal only.**
   `shots_delete` responds from `shotDeleted` and the failure path emits
   `errorOccurred`, so the failure produces no response at all.
4. **A guard that returns emptiness instead of a reason.** Four profile reads
   `return result;` on a null `ProfileManager`; `steam_get_health` returns a
   default-constructed struct whose `status` is `""`.

The audit also found ~six *deliberate* partial-outcome payloads
(`set_flow_calibration`'s `warning`, `profiles_edit_params`' `ignoredFields`,
`dialing_get_grinder_calibration`'s `available: false`, and others). Those are
reviewed decisions with comments explaining them, and they are not in scope —
distinguishing them from the defects above is the first thing this design has to
get right, because a rule that swept them up would be wrong.

## Goals / Non-Goals

**Goals:**

- A tool's `success` means the operation happened. Where the underlying call
  cannot currently say, give it a way to say it.
- No tool waits on a signal that only fires on success.
- "Dependency unavailable" and "your input did not resolve" are answerable
  states, not silences.

**Non-Goals:**

- **`settings_set`'s ~90 `void` setters.** It builds `{"success": true, "updated": […]}`
  *before* the setter closures run, so no clamp or rejection can reach the
  response. Real, but no failing key has been identified, and the fix is a
  90-setter refactor to report applied-vs-clamped. Out of scope deliberately —
  see Open Questions, and note the file already handles the one case someone
  noticed (`simulationMode`) by refusing up front rather than reporting an
  ineffective write.
- **A generic watchdog on the `_deferred` path.** A handler that never calls
  `respond()` hangs the client with no timeout anywhere in `McpServer`. That is a
  real structural gap, but the fix here is to close the one concrete instance
  (`shots_delete`); a timeout mechanism for a hazard with one known instance is
  machinery ahead of evidence.
- **Changing any deliberate partial-outcome payload.** Listed above.
- **Re-litigating which conditions are failures.** Same non-goal as #1754.

## Decisions

### Outcome from the operation, never from a pre-check

The tempting fix for `shots_update` and `shots_delete` is a `SELECT` first to
confirm the row exists. That is wrong twice over: it races (the row can go
between check and write, and both tools run on a background thread), and it adds
a query per call to a path that already has one.

Instead the operation reports what it did: `numRowsAffected()` for the update and
delete, a `bool` return for `loadProfile` and `applyPresetTheme`. This is both
cheaper and correct under concurrency, and it puts the knowledge where the
operation is rather than where the caller guesses.

*Consequence:* three currently-`void` app-layer functions change signature. Their
existing UI callers ignore the new return, which is fine — `[[nodiscard]]` is
deliberately NOT added, because a UI caller that shows the outcome on screen has
no use for it and the annotation would force `(void)` noise at every call site.

### `loadProfile` reports refusal by return value, not by listening for the signal

`loadProfile` already emits `profileRefusedUnreadable` on the path in question,
so a tool *could* connect to it. It should not: the tool would then be racing a
signal it did not cause (any other loader's refusal would resolve its call), and
`profiles_set_active` is currently synchronous-ish via `invokeMethod` with no
connection lifetime to manage.

A `bool` return keeps the causality direct — this call refused — and leaves the
signal for the UI, which is what it was added for.

### A signal-driven tool connects to every terminal signal, or none

`shots_delete` connects to `shotDeleted` alone. The fix is to also connect
`errorOccurred` and disconnect both from whichever fires first.

The sharper question is whether `errorOccurred` is specific enough: it is a
general-purpose storage error signal carrying a user-facing string with no shot
ID, so a *different* failing operation could resolve this tool's call with a
misleading message. Given the alternative — leaving the client to hang forever —
resolving on a possibly-unrelated storage error is the better failure, but the
right fix is for `requestDeleteShot` to report its own outcome. Prefer a
delete-specific completion (id + success) and use `errorOccurred` only if that
proves invasive.

### Unavailability is an error payload, and it matches the file it lives in

The four profile reads get `result["error"] = "Profile manager not available";`
— the exact shape every other guard in `mcptools_profiles.cpp` already uses.
This is the centralization rule applied to a convention rather than to code: the
outliers are outliers, not a different policy.

`steam_get_health` is the same shape with an extra consideration —
`hasData: false` already exists and is meaningful (no steam sessions yet), which
is *not* the same as "the tracker is unavailable". Those two must stay
distinguishable, so unavailability becomes an `error` and `hasData: false` keeps
meaning what it means.

### Dropped inputs are named, and the count invariant is stated

`shots_compare` returns the shots it could resolve. Rather than failing the whole
call when one ID is bad — which would make a mostly-good comparison useless — it
gains an `unresolvedShotIds` array naming what it dropped. The caller then does
not have to infer loss by comparing counts, and a call where *every* ID failed
becomes an `error` because there is nothing to compare.

### Timer support becomes a device capability, not a hope

`ScaleDevice` gains a virtual `supportsTimer()` defaulting to `false`, overridden
by the drivers that implement the three timer methods. The tools consult it and
report an error rather than a fictional success.

*Alternative considered:* reword the messages to "Timer start requested". Rejected
— it is honest about the tool and dishonest about the outcome, and a model
reading "requested" still has no way to know it did nothing.

*Trade-off:* this touches every scale driver, which is the widest blast radius in
this change. The default of `false` means a driver that is missed reports an
error instead of a false success, so the failure mode of an incomplete
implementation is the safe direction.

## Risks / Trade-offs

- **Calls that "worked" start failing** → A client whose flow depends on
  `shots_delete` reporting success for an already-deleted shot will now see an
  error. That client was being lied to; the standing position on MCP surfaces
  covers it. Mitigation: name each behaviour change in the PR body.
- **Signal-based resolution can resolve on the wrong event** → Covered above:
  prefer a delete-specific completion over the general `errorOccurred`.
- **Changing three `void` signatures touches UI callers** → They ignore the
  return; no `[[nodiscard]]`, so no forced churn. The compiler proves the call
  sites still build.
- **The scale-timer capability flag spans drivers** → Default `false` makes an
  incomplete rollout report an error rather than a false success.
- **Over-correction into the deliberate partial-outcome payloads** → Enumerated
  in Context and listed as a non-goal; the tasks name them explicitly so a future
  reader does not "finish the job".

## Migration Plan

None. No schema, no persisted state, no settings. Behaviour changes are confined
to what tools report.

## Open Questions

- **`settings_set`.** Deferred here for lack of a failing key, but the mechanism
  is real: `void` setters, response assembled before they run. Worth its own
  change if any key is found to clamp or reject silently — and worth a targeted
  read-back check if one is. Recorded so it is not rediscovered from scratch.
- **Is `requestDeleteShot` worth a delete-specific completion signal?** Preferred
  above, but it touches storage rather than MCP. If it proves invasive, the
  `errorOccurred` fallback ships with its imprecision documented at the call
  site.
