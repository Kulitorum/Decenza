#pragma once

// "Save as recipe" from a shot, shared by the shot list ("Recipe" beside Load)
// and the shot page. Prompts for a name and whether it is a milk drink, then
// POSTs /api/recipes/from-shot/<id>; the server prefills the recipe from the
// shot. `notify(message)` reports a failure the page's own way (toast or alert).
inline constexpr const char* WEB_JS_RECIPE_FROM_SHOT = R"JS(
        function createRecipeFromShot(id, notify) {
            var name = prompt('Name for the new recipe (e.g. Morning cappuccino):');
            if (!name || !name.trim()) return;
            var hasMilk = confirm('Is this a milk drink? (OK = yes, Cancel = no)');
            fetch('/api/recipes/from-shot/' + id, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ name: name.trim(), hasMilk: hasMilk })
            })
            .then(function(r) { return r.json().then(function(d) {
                if (!r.ok || d.error) throw new Error(d.error || ('Server error (' + r.status + ')'));
                return d;
            }); })
            .then(function() {
                if (confirm('Recipe created. Open the Recipes page?'))
                    window.location.href = '/recipes';
            })
            .catch(function(e) { notify('Could not create recipe: ' + e.message); });
        }
)JS";
