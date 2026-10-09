# Tasks

This is evidence-only. Design is intentionally omitted because no architectural, dependency, data-model, security or migration decision applies; specs are skipped by metadata.

## 1. Physical tablet verification

- [ ] 1.1 On the current tablet build, choose personal and cached stock images and verify preview/apply and readable Beans/Recipes/Settings/shot-page content in dark/light mode; record build, device and results.
- [ ] 1.2 With TalkBack, verify picker entry, thumbnail identity, focus order and confirm/cancel actions; record the actual traversal and any failure rather than infer it from desktop accessibility labels.
- [ ] 1.3 Verify tablet restart persistence, None, and deletion fallback using an owned disposable backing image; restore original background/theme afterward and record the observed result.

## Workflow follow-up

- These checks were explicitly carried forward by the user on October 8, 2026; the implementation archive does not mean they passed.
- Ask before any further Qt Creator use: the user took it over on October 8, 2026. Ask the user to start/restart the live app.
- Archive this follow-up after recorded results or a new explicitly accepted disposition. Optional blanket nested-widget translucency sweeps are not required unless a reproducible defect is found.
