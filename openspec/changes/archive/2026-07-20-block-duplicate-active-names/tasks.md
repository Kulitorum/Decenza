## 1. Shared comparison rule

- [x] 1.1 Define the normalization once and reuse it everywhere: key = `name.trimmed().toLower()` in C++ (`QString`) and `String(name).trim().toLowerCase()` in QML. An empty entered name is never a collision.
- [x] 1.2 Decide the collision-result contract for storage: a colliding create/rename returns an explicit failure (not the equipment-dedup "return existing id"), carrying a "name already in use" reason string the callers surface. (Create: `packageCreated(-1, {error:"nameInUse"})`; rename: update fails.)

## 2. Equipment package (anchors confirmed)

- [x] 2.1 In `qml/components/SwitchEquipmentDialog.qml`, add a `nameDuplicateOfId` computed property mirroring `duplicateOfId`: scan `packages` (already `in_inventory = 1`), skip `editPackageId`, compare normalized `fName` to normalized `p.name`; return `p.id` or `-1`; return `-1` when `fName.trim()` is empty.
- [x] 2.2 Wire `&& nameDuplicateOfId < 0` into `canSave`.
- [x] 2.3 Add an inline message under the name field, `visible: nameDuplicateOfId > 0`, key `equipment.dialog.nameInUse`.
- [x] 2.4 Add storage-level guard: `findPackageByNameStatic` + reject in `requestCreatePackage` (new gear only) and `requestUpdatePackage` (rename), scoped to `in_inventory = 1`, excluding the edited id.
- [x] 2.5 Surface the rejection: ShotServer `POST /api/equipment` returns 409 on `error:"nameInUse"`; rename-collision returns failure. (No MCP equipment-create path exists; import is a data-restore path, left unguarded.)

## 3. Recipe (anchors to pin from recipe scoping)

- [x] 3.1 `RecipeWizardPage.qml`: added `nameInUse` (over the existing `_existingRecipeNames` non-archived list, excludes edited recipe) and wired `&& !nameInUse` into `canSave`.
- [x] 3.2 Added the inline "name already in use" message under the wizard's name field, key `recipe.dialog.nameInUse`.
- [x] 3.3 Storage guard: `findRecipeByNameStatic` (archived = 0) + reject in `requestCreateRecipe`, `requestCloneRecipe`, and `requestUpdateRecipe` (rename OR unarchive into a collision), excluding the edited id.
- [x] 3.4 Surfaced `nameInUse` in MCP `recipe_create`/`recipe_clone`/`recipe_create_from_shot` and ShotServer create/create-from-shot/clone (409). Update/rename collisions return generic failure (backstop).

## 4. Bean bags — DROPPED

Bean bags are out of scope: a bag's identity is roaster + coffee rather than a
user-typed name, and the existing behaviour is correct. All bag changes were reverted;
`coffeebagstorage.*`, `ChangeBeansDialog.qml`, `shotserver_bags.cpp` and the bag branch
of `mcptools_write.cpp` are untouched by this change.

- [x] 4.1 Reverted every bag-side edit; confirmed `git status` shows no bag files modified.

## 4b. Surface parity (app / MCP / ShotServer behave the same)

- [x] 4b.1 Added additive signals `EquipmentStorage::packageUpdateFailed(id, reason)` and `RecipeStorage::recipeUpdateFailed(id, reason)`, emitted with `"nameInUse"` immediately BEFORE the terminal `*Updated(id, false)`. Existing signatures unchanged, so no caller or test stub breaks.
- [x] 4b.2 ShotServer equipment update and recipe update now pair a reason listener with the terminal listener and return 409 with a name-conflict message.
- [x] 4b.3 MCP `recipe_update` does the same. (There is no MCP equipment create/update-name path.)
- [x] 4b.4 Surfaces agree: create AND rename, equipment AND recipe — same active scope, same trim+case-insensitive normalization, same identifiable conflict on app / MCP / ShotServer.

## 5. i18n

- [x] 5.1 Added `equipment.dialog.nameInUse` and `recipe.dialog.nameInUse`, both with the identical fallback "That name is already in use — choose a different name." No registration file needed: TranslationManager self-registers a key/English pair from the `translate(key, fallback)` call site (see `tst_translationsourcedrift`), so using the pattern IS the registration.

## 6. Tests

- [x] 6.1 `tst_Equipment::activeNameUniqueness` — exact/case-insensitive/whitespace matches hit; different and blank names do not; the edited package is excluded (self-rename to a casing variant allowed); removing from inventory frees the name.
- [x] 6.2 `tst_RecipeStorage::activeNameUniqueness` — same matrix over non-archived recipes, plus archiving frees the name and unarchiving makes it collide again.
- [x] 6.3 Full suite green via Qt Creator: 85 binaries, 0 failed, 0 skipped, no warnings (tst_Equipment 29 passed, tst_RecipeStorage 52 passed).

## 7. Docs & verification

- [x] 7.1 Wiki manual updated and pushed (`Decenza.wiki` c294d8e): a note in **Equipment Packages** (also distinguishing this from the existing identical-gear rule) and an extension of the naming paragraph in **Creating a Recipe**.
- [x] 7.2 Smoke-tested in the running app: both the equipment and recipe forms block a colliding name and save normally otherwise.
- [x] 7.3 Archived as the last commit on the branch, with the `active-name-uniqueness` capability promoted into `openspec/specs/`.
