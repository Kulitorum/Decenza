# Shipped change reconciliation — October 8, 2026

The implementation shipped as `58f0265f4` in [PR #1067](https://github.com/Kulitorum/Decenza/pull/1067). The user accepted archival after reviewing current main (`3456f8e77`). The old 0/35 checklist did not reflect shipped code; this record replaces it with verified outcomes and explicit follow-ups.

## 1. Implementation and contract

- [x] 1.1 Confirm the sparse roast/brand/date parser and context-gated background write in src/ai/aimanager.{h,cpp}, with anchored invocation in src/ai/aiconversation.cpp; checked against current source.
- [x] 1.2 Confirm prompt teaching and family catalog rendering in src/ai/shotsummarizer.cpp and shotsummarizer_kb.cpp; reconcile the delta to schema-valid JSON families and exclude skipCatalog reference entries. The old boolean persistence return was superseded by a void queued-write helper with tracked outcomes.
- [x] 1.3 Confirm parser/gating/catalog/prompt tests exist in tests/tst_aimanager.cpp and tst_shotsummarizer.cpp. PR #1067 records a clean Qt Creator build and 2025 passing cases with zero failures/warnings; this is historical evidence, not a new run.

## 2. Archive readiness

- [x] 2.1 Preserve unrecorded advisor runtime checks and unproven positive-persistence, next-envelope integration, and prompt-prefix assertions in verify-shipped-advisor-and-settling/tasks.md. None is reported as passed here.
- [x] 2.2 Strictly validate the reconciled shipped delta before CLI archival; the new capability Purpose describes current behavior rather than historical Markdown storage.

## Workflow follow-up

- Archive the shipped implementation; outstanding verification belongs to verify-shipped-advisor-and-settling. That follow-up must distinguish corrected shot metadata from today's separately owned bag context.
