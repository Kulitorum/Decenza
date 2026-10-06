import QtQuick
import Decenza

// "Add" before a page's create buttons (Beans, Recipes, Equipment), so each
// button can name only its kind: "Add [Bag of Coffee] [Bag of Tea]". The
// buttons' accessible names already say "Add a new ...", so this is skipped.
Tr {
    key: "common.add"
    fallback: "Add"
    font: Theme.bodyFont
    color: Theme.textColor
    Accessible.ignored: true
}
