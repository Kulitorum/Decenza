# auto_load

The auto-load pin decides what is reloaded on app start, on DE1 wake-from-sleep, and after
`revertMinutes` of inactivity on the Idle page.

`target` is `profile` or `recipe`, and both are required arguments alongside `action`. The two
pins are **mutually exclusive**: setting one clears the other. That is why they are one tool.

## set

- `target=profile` takes `filename` — a profile filename without `.json` that exists AND is in
  the Selected list. Errors, distinctly: `filename is required`, `Profile not found: <name>`,
  `Profile is not in the Selected list`.
- `target=recipe` takes `recipeId` from `recipe_list`. Errors, distinctly: `recipeId is
  required`, `recipeId must be a positive integer`, `recipeId is out of range`, `Recipe not
  found: <id>`, `Recipe is archived`.
- `revertMinutes` is optional and shared by both targets, clamped to 0-60.

## get

Reports the current pin for that target. An unconfigured pin is not an error: the profile form
returns `filename: ""`, the recipe form returns `recipeId: null`, and both still report
`revertMinutes`.

A pin naming something that no longer resolves is reported the same way as unconfigured — a
deliberate snapshot read, because the next auto-load trigger discovers and clears a genuinely
stale pin. A storage that cannot be opened IS an error, and is distinct from both.

## clear

Clears that target's pin and leaves `revertMinutes` untouched.
