## 1. Bare multi-word values

- [x] 1.1 Add a free-text key set — `profile_title`, `author`, `profile_notes` ONLY — to `de1apptclfields.cpp` and take the rest of the line for a bare value of those keys, leaving every other key on the first-token rule
- [x] 1.2 Strip a trailing `;#` or ` #` Tcl comment when applying the rule, so a hand-edited `profile_title Espresso ;# note` does not fold the comment into the title
- [x] 1.3 Record at the call site why `beverage_type` and `profile_language` are excluded — both are bare-written enums/codes where rest-of-line silently drops a classification, the one case where this rule is worse than truncation
- [x] 1.4 Document why this diverges from Tcl's own `array set`, citing the Visualizer export and the `[string range $title 0 7]` dispatch that costs de1app users the editor
- [x] 1.5 Confirm the rule is unreachable for braced and quoted values, so no existing de1app profile changes path

## 2. de1app per-profile dose

- [x] 2.1 Add `profile_grinder_dose_weight` to the scalar field table, mapped to `recommended_dose`, with the absent-value substitute left UNSET
- [x] 2.2 Comment that row to say the substitute must stay unset because `compareScalars` walks the same table as the built-in drift gate, and a `0` fallback would fail all 8 built-ins carrying `recommended_dose: 18.0`
- [x] 2.3 Set `has_recommended_dose` in `Profile::loadFromTclString`, only when the imported value is greater than zero — `readScalar` returns a bare double and cannot set a companion flag
- [x] 2.4 Add `profile_grinder_setting` to the known-ignored list with its evidence line, alongside the existing grinder entries
- [x] 2.5 Check whether a realistic de1app-saved profile also carries `original_profile_title`, `profile_filename` and `profile_to_save` (all written by `profile.tcl:658`), and list any that would otherwise land in `uncoveredTclKeys()`

## 3. Tests

- [x] 3.1 Import the real Visualizer `D-Flow / Q` export as a fixture and assert the title, editor type and slash-prefix category all resolve
- [x] 3.2 Assert a bare `beverage_type` followed by trailing text still yields the first token and keeps its classification
- [x] 3.3 Assert a bare numeric value still reads as the first token
- [x] 3.4 Assert braced and quoted values are byte-identical to before
- [x] 3.5 Assert a trailing Tcl comment is stripped rather than absorbed
- [x] 3.6 Assert every de1app corpus profile re-serializes identically to before the rule change
- [x] 3.7 Cover the dose: non-zero promoted with the flag set, zero and absent leaving it off
- [x] 3.8 Assert the built-in drift gate still passes across all 88 corpus profiles with the new table row present
- [x] 3.9 Assert `uncoveredTclKeys` is empty for a fixture carrying the de1app save-only key set, adjusted for whatever 2.5 finds

## 4. Withdraw the roundtrip change

- [x] 4.1 Remove `openspec/changes/preserve-recipe-visualizer-roundtrip/`, recording in the commit message that its Non-Goal is refuted by `proc prep`, the three-line toggle derivation, the title-prefix editor variant, and the edit matrix at 0/99
- [x] 4.2 Confirm no live change or promoted spec references it; archived changes that mention it by name are historical and stay as they are

## 5. Documentation

- [x] 5.1 Update `docs/CLAUDE_MD/RECIPE_PROFILES.md` for the bare-value rule and the new de1app dose mapping
- [x] 5.2 ~~Wiki manual note~~ — **deliberately skipped.** The manual documents features, not
  every bug fix, and this one restores the behaviour a user already expects (a downloaded profile
  keeps its name). Nothing about the documented feature set changes.

## 6. Upstream

- [ ] 6.1 Open an issue on `miharekar/decent-visualizer` for the unbraced `profile_title` in the `.tcl` export, with the `tclsh` reproduction, the de1app editor-dispatch consequence, and the note that the JSON export is unaffected

## 7. Verify

- [x] 7.1 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) and clear every warning
- [x] 7.2 Re-import the downloaded file end to end and confirm it lands as `D-Flow / Q` in the D-Flow group
- [ ] 7.3 Archive the change with `openspec archive` as the last commit on the branch
