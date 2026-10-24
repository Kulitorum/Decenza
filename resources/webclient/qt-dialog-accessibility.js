/* Qt 6.12 Wasm dialog visibility workaround.
 *
 * qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:494 creates <dialog>,
 * but setHtmlElementVisibility() only toggles `inert` (:684-688). A closed HTML
 * dialog hides its descendants even when Qt considers it visible. Mirror Qt's
 * visibility onto `open`, retaining its dialog role, labels, actions and geometry.
 * Do not use showModal(): Qt owns focus, modality and the visible popup.
 * Upstream tracking: https://bugreports.qt.io/browse/QTBUG-144330
 * Pending fix: https://codereview.qt-project.org/c/qt/qtbase/+/707847
 *
 * Scoped to this application's Qt shadow root. Recheck/remove on a Qt upgrade.
 */
function installQtDialogAccessibility(container) {
    const root = container.querySelector("#qt-shadow-container")?.shadowRoot;
    if (!root)
        throw new Error("Qt accessibility workaround could not find the Qt shadow root.");

    // Match the geometry reset in upstream review 707847: browser dialog
    // defaults otherwise add centering/padding to the geometry supplied by Qt.
    const style = document.createElement("style");
    style.textContent = `.qt-window-a11y-container dialog {
        margin: 0; padding: 0; border: none; right: auto; bottom: auto;
        max-width: none; max-height: none;
    }`;
    root.appendChild(style);

    function sync(dialog) {
        dialog.toggleAttribute("open", !dialog.hasAttribute("inert"));
    }
    function syncTree(node) {
        if (!(node instanceof Element)) return;
        if (node.matches("dialog")) sync(node);
        node.querySelectorAll("dialog").forEach(sync);
    }

    const observer = new MutationObserver(records => {
        for (const record of records) {
            if (record.type === "attributes") {
                if (record.target.matches("dialog")) sync(record.target);
            } else {
                record.addedNodes.forEach(syncTree);
            }
        }
    });
    observer.observe(root, {subtree: true, childList: true,
                            attributes: true, attributeFilter: ["inert"]});
    root.querySelectorAll("dialog").forEach(sync);
    return () => {
        observer.disconnect();
        style.remove();
    };
}
