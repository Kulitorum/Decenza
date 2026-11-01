import QtQml
import Decenza

// The string property stops unchanged text in a replaced snapshot from
// invalidating Markdown conversion. Multiple views share the parsed result.
QtObject {
    property string markdown: ""
    readonly property string html: MarkdownRenderer.toHtml(markdown)
}
