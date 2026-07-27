import QtQuick
import Decenza

// Live-resolves a recipe's identity map by id (add-recipes), shared by the shot
// pages' recipe cards. A shot-linked recipe can only be archived, never deleted,
// so the row always resolves. Feed `sourceRecipeId` from the shot (e.g.
// shotData.recipeId || -1); read the resolved map from `recipe` (empty ({})
// until it lands, and whenever there is no recipe). `_resolvedId` guards against
// re-requesting — binding sourceRecipeId to the shot means this only reacts when
// the id actually changes, not on every shot reassignment. Non-visual.
Item {
    id: resolver
    property int sourceRecipeId: -1
    property var recipe: ({})
    property int _resolvedId: -1

    onSourceRecipeIdChanged: {
        if (sourceRecipeId > 0 && sourceRecipeId !== _resolvedId) {
            _resolvedId = sourceRecipeId
            recipe = ({})
            MainController.recipeStorage.requestRecipe(sourceRecipeId)
        } else if (sourceRecipeId <= 0) {
            _resolvedId = -1
            recipe = ({})
        }
    }

    Connections {
        target: MainController.recipeStorage
        function onRecipeReady(recipeId, r) {
            if (recipeId === resolver._resolvedId)
                resolver.recipe = r
        }
    }
}
