## Context

Visualizer declined a dedicated community endpoint (miharekar/visualizer#266) and extended `GET /api/shots` instead: `community=1` searches public shots plus the caller's own private ones, `full=1` returns each shot's fields, `bean_brand`/`bean_type` are case-insensitive substring filters, `canonical_coffee_bag_id` matches a canonical bean, results are newest first, `items` caps at 100, and `full=1` is limited to 10 requests a minute per IP. Free and anonymous callers see a recent window; premium accounts see all history.

## Decisions

### D1. Two requests per bean, cached for the session
The bean-filter request is authoritative; when the bag is linked to a canonical bean, a second request by `canonical_coffee_bag_id` catches other spellings and is merged by shot id. A failed canonical leg degrades to the bean rows and is not cached. The user's Visualizer login is sent when connected. Results are cached per bean and dropped when the grinder, the floor or the account changes.

### D2. Ranking by a lower credible bound over liked shots
Shots are cleaned (Visualizer trending-job bounds), grouped into profile families, and ranked by mean − 1.64·sd/√n over shots rated at least the floor, n ≥ 2, with at least three people per suggestion and the last 12 months preferred.

### D3. Grind only on the same grinder
Grind is quoted only when the community grinder resolves to yours with at least three samples. Cross-grinder translation is scaffolded behind a setting that is off, with no micron data.

### D4. Apply goes through a recipe
Recipes are the single activation path, and live dose/grind edits write through into an active recipe. So Apply never edits the current setup: it creates a recipe (active bag and equipment, the profile — imported from the top shot when missing — median dose, ratio as the yield, the profile's own temperature, grind only on a matching grinder) and activates it. The name is "<coffee> · <profile>"; `RecipeStorage` numbers it ("… 2") on request when the name is taken. Activation sets the bag's dial to the recipe grind, as any recipe does; an empty grind leaves it alone.

### D5. Create recipe opens the wizard
The same values go to the wizard's `prefill`, so the user can change anything before saving.
