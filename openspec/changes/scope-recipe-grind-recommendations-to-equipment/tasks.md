## 1. Equipment-aware grind-history lookup

- [ ] 1.1 Extend the recipe-wizard grind-history query to accept an optional selected equipment package ID, use it for both exact-bean and same-roast lanes first, then retry only against packages with the same grinder (brand/model/burrs) and basket (brand/model) while ignoring puck prep; verify a different grinder or basket never qualifies.
- [ ] 1.2 Preserve the existing equipment-agnostic lookup when the recipe explicitly has no selected package, and verify the zero-ID case returns the current latest bean/same-roast result.

## 2. Wizard recommendation lifecycle

- [ ] 2.1 Pass the selected recipe package to the wizard's grind-hint request, refresh the request after an equipment-tile selection, and verify full-package history wins while a puck-prep-only difference can provide the fallback hint.
- [ ] 2.2 Correlate asynchronous hint replies with their queried package and verify a reply for a prior selection cannot replace the current hint.

## 3. Regression coverage and verification

- [ ] 3.1 Extend the existing shot-history test target with multiple real equipment packages and verify full-package priority, same-grinder-and-basket fallback across puck-prep differences, and rejection of different grinders or baskets.
- [ ] 3.2 Run the affected Qt test target through Qt Creator MCP and verify it passes; manually create a coffee recipe with matching grinder/basket packages that differ only in puck prep, then with a different grinder or basket, to confirm the hint follows the two-tier rule.
