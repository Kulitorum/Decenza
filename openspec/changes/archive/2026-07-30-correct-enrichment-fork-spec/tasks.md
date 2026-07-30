Spec-only. The code and tests these requirements describe are already on `main` as of `848e2df7` (PR #1720); every task here is transcription plus a check that the spec and the suite agree.

## 1. Correct the enrichment requirement

- [x] 1.1 Restate the requirement with the grinder-less exception and its reason, and say explicitly that the exception does NOT extend to an absent basket or puck prep
- [x] 1.2 Add scenarios for both sides: a grinder-less package gaining a grinder forks; naming a never-recorded basket is enrichment
- [x] 1.3 Verify each scenario against `tst_equipment::copyOnWriteAndMerge` — the tea-package fork and the whole-tuple enrichment case

## 2. Correct the merge requirement

- [x] 2.1 State that the target returns to inventory only when nothing else supersedes it, with the stale-duplicate reason
- [x] 2.2 Add the "superseded target is not revived" scenario
- [x] 2.3 Verify against `tst_equipment::healEnrichmentForks`, which asserts the chain's middle package keeps `inInventory == false` and its `supersededBy`

## 3. Add the log-subsystem requirement

- [x] 3.1 Require the `[Equipment]` marker, the per-branch identity decision line, the single shared merge line, and a heal that reports even at zero
- [x] 3.2 Confirmed against the running app: `[Equipment][Migration] 35 complete - merged 0 package(s) that a burr edit had split off` — the zero case reporting itself, which is the requirement's point

## 4. Land it

- [x] 4.1 `openspec validate correct-enrichment-fork-spec`
- [x] 4.2 Archive with `openspec archive correct-enrichment-fork-spec` as the last commit on the branch
