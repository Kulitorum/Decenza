## 1. Implementation

- [x] 1.1 Shared `AddLabel` component; Beans, Recipes and Equipment headers use it, with a dark kind button and icon. Web Recipes and Equipment pages match.
- [x] 1.2 Focus ring on keyboard focus only (`visualFocus`), or any focus with a screen reader on.
- [x] 1.3 Key handlers that move focus pass `Qt.TabFocusReason` (Steam, Hot Water, Flush); ACCESSIBILITY.md rule 5.

## 2. Docs and verification

- [x] 2.1 Wiki manual: **Add: Recipe** / **Add: Equipment** wording in FAQ, Manual and Quick-Start (wiki 75e7ad1).
- [x] 2.2 Build and full suite through Qt Creator: 119/119; QML lint gate clean (254/254; 256/256 after merging main).
- [x] 2.3 Check on the Mac: the three headers match; no ring when a page opens (Beans, Recipes, Equipment, Steam) or on a mouse click. HELD, not verified: Tab showing the ring — Full Keyboard Access is off on the test Mac, so Tab skips buttons; merged on the user's call (2026-10-05). Verify with Full Keyboard Access on, or with a screen reader on.
