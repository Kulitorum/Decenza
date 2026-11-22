.pragma library

// A bag's name, or a recipe's bean, from its roasterName and coffeeName.
// shortName: the coffee, else the roaster. fullName: "Roaster Coffee".

function shortName(bag) {
    bag = bag || ({})
    return String(bag.coffeeName || "") || String(bag.roasterName || "")
}

function fullName(bag) {
    bag = bag || ({})
    return (String(bag.roasterName || "") + " " + String(bag.coffeeName || "")).trim()
}
