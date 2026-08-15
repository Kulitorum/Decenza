## 1. One traversal, two filters

- [x] 1.1 Define the comparable-field list once in `src/profile/profile.{h,cpp}`: an ordered enumeration of
      profile-level and per-frame fields, each carrying its reader, its kind, and whether it is a shape field,
      a dial-in field, or developer-only. Record at the definition why the inactive setpoint is
      developer-only (the machine never applies it) and why the non-matching exit thresholds are excluded.
- [x] 1.2 Reimplement `Profile::frameDiffReport()` as a formatter over that traversal. Its field set and its
      rendered text must not change.
- [x] 1.3 Add a test pinning `frameDiffReport()`'s rendered text for a known differing pair, so a future
      format change fails instead of passing quietly. `tst_tclimport`'s emptiness assertion only catches
      fields appearing or vanishing, not their rendering.
- [x] 1.4 Add the dial-in filter over the same traversal, returning structured deltas (kind, frame index,
      frame name, old and new values). Tests: inactive setpoint absent for a pressure frame that differs only
      in flow; only the matching exit threshold present; a frame rename present; shape fields never present.

## 2. The bundled profile behind a shape bucket

- [x] 2.1 Change `ProfileShapeIndex`'s bucket value to carry the bundled profiles' resource paths alongside
      the KB ids. Keep the id list's existing sort and semantics; sort the paths for the same reason.
- [x] 2.2 Expose a lookup returning the bundled profiles sharing a given profile's shape.
- [x] 2.3 Test: every shipped profile's own resource path appears in its own bucket; the two known collision
      buckets each carry two paths; the lookup is independent of directory enumeration order.

## 3. Base selection

- [x] 3.1 Implement candidate-list construction: title origin restricts the bucket to profiles carrying the
      resolved id; shape origin takes the whole bucket. A profile that is a bundled profile itself yields no
      candidates.
- [x] 3.2 Implement the nearest-candidate vote over the dial-in field list, abstaining when no candidate has
      strictly the most points.
- [x] 3.3 Tests, using the two real collisions: a copy of `D-Flow / La Pavoni` re-tuned slightly picks the
      La Pavoni file and not `D-Flow / default`; a constructed profile sitting equidistant between
      `Hybrid pour over espresso` and `Preinfuse then 45ml of water` produces no base. Also: a title-resolved
      in-place edit of a bundled profile selects that bundled profile; a title-resolved profile of a
      different shape selects nothing.

## 4. QML-facing API

- [x] 4.1 Add `ProfileManager` invokables returning the diff for a catalog profile by title and for a stored
      profile JSON string, both over one C++ core. Return structured rows; no user-visible text in C++.
- [x] 4.2 Distinguish the three outcomes the surface must tell apart: no base (no block), a base with no
      differences (unchanged-copy statement), and a base with differences (the list).
- [x] 4.3 Justify the inline read at the call site. Resolved as a bounded-work argument rather than a
      stopwatch figure: the work is one disk load plus at most two resource loads and a linear walk, on a
      discrete user action, assigned to a plain property rather than bound. Recorded as explicitly
      unmeasured — see the comment; a number nobody took must not be written as if it were.
- [x] 4.4 Tests for the JSON-string entry point, including unparseable JSON returning no base rather than
      warning-failing a test that does not expect a warning.

## 5. Dialog presentation

- [x] 5.1 Add the block to `qml/components/ProfileKnowledgeDialog.qml` above the KB prose: heading naming the
      bundled profile, one row per difference, old → new with units. Theme tokens only; no hardcoded colours,
      sizes or spacing.
- [x] 5.2 Render the unchanged-copy case as a single line, and render nothing at all when there is no base.
- [x] 5.3 Add `openForShot(title, profileJson)` beside `openFor(title)`, computing the diff into a plain
      property inside the function rather than a binding. Update the file's header comment, which currently
      names `openFor` as the only complete entry point.
- [x] 5.4 Point `ShotDetailPage.qml` and `PostShotReviewPage.qml` at `openForShot`, passing the shot's stored
      profile JSON.
- [x] 5.5 Accessibility: the block is a labelled group with a readable summary; every row reachable in focus
      order. Fix any pre-existing accessibility violations in the files touched.
- [x] 5.6 Declare every model role the new Repeater delegate reads as a `required property`, in the same edit
      that introduces the delegate.
- [x] 5.7 Translation keys for the heading, the unchanged-copy line, and each field label, with English
      fallbacks. Reuse existing keys where they exist.

## 6. Gates and documentation

- [x] 6.0 Five-agent review pass; fix every confirmed finding. Six defects producing wrong output were
      found and fixed: the self-check using a predicate blind to five dial-in fields, a tolerance that
      swallowed one editor step of yield, a missing authored brew-temperature row, a collapse that merged
      rows across differing units, a load failure that could turn a tie into a false win, and a
      same-entry tie that excluded every tea profile. Plus a C++/QML drift gate, four duplicate
      translation keys, an inverted accessible name, and eleven comment inaccuracies.

- [x] 6.1 Build all through the Qt Creator MCP, including `qmllint_check`, and clear every new diagnostic.
- [x] 6.2 Run the full test suite through the Qt Creator MCP; zero failures and zero warnings.
- [x] 6.3 Update `docs/PROFILE_KNOWLEDGE_BASE.md` with the block, the shape gate, and the tie rule.
- [x] 6.4 Add a short entry to the wiki manual's profile section — what the block is, where it appears, and
      the one non-obvious rule (it only appears when the frame structure matches). Three to five sentences.
- [ ] 6.6 Archive this change with `openspec archive summarize-profile-changes-from-builtin` as the last
      commit on the branch, before merge.
