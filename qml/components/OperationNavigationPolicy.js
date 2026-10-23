.pragma library

// StackView policy used by SharedAppNavigation. Application roots retain startup,
// native device/window lifecycle and browser history wiring.
var operationPageNames = ["espressoPage", "steamPage", "hotWaterPage",
                          "flushPage", "descalingPage", "transportPage"]

function isOperationPage(name) {
    return operationPageNames.indexOf(name) >= 0
}

function operationAction(currentName, targetName) {
    if (currentName === targetName)
        return "stay"
    if (currentName === "screensaverPage" || isOperationPage(currentName))
        return "replace"
    return "push"
}

function backAction(stackDepth) {
    return stackDepth > 1 ? "pop" : "stay"
}

function dismissAction(stackDepth) {
    return stackDepth > 1 ? "pop" : "home"
}

function beginNavigation(shellState, stack, defer) {
    if (shellState.navigationInProgress || stack.busy)
        return false

    shellState.navigationInProgress = true
    defer(function() {
        if (!stack.busy)
            shellState.navigationInProgress = false
    })
    return true
}

function showOperationPage(stack, component, targetName, closePopups) {
    const current = stack.currentItem
    const currentName = current ? current.objectName : ""
    const action = operationAction(currentName, targetName)
    if (action === "replace")
        stack.replaceCurrentItem(component)
    else if (action === "push") {
        if (current)
            closePopups(current)
        stack.push(component)
    }
    return action
}

// Ordinary destinations retain their current instance. Each shell supplies its
// push path so browser history recording stays at the browser boundary.
function pushUnlessCurrent(current, component, targetName, properties, pushPage) {
    if (current && current.objectName === targetName)
        return null
    return pushPage(component, properties)
}

function showSettingsPage(currentSettings, component, tabId, tabIndex, pushPage, showTab) {
    const validTab = tabId !== undefined && tabId !== "" && tabIndex >= 0
    if (currentSettings) {
        if (validTab)
            showTab(tabId)
        return validTab ? "update" : "stay"
    }
    pushPage(component, validTab ? {requestedTabId: tabId} : undefined)
    return "push"
}

function showHistoryPage(currentHistory, component, filter, pushPage, applyFilter) {
    if (currentHistory) {
        // A context action with no filter must preserve a user's existing search.
        // An explicit empty object asks to clear it.
        if (!filter)
            return "stay"
        applyFilter(filter.initialFilter || null)
        return "update"
    }
    pushPage(component, filter || ({}))
    return "push"
}

function popIfPossible(stack) {
    if (backAction(stack.depth) !== "pop")
        return false
    stack.pop()
    return true
}

function leaveOperationPage(stack, mayNavigate, showHomeAtRoot) {
    const currentName = stack.currentItem ? stack.currentItem.objectName : ""
    if (!isOperationPage(currentName) || (mayNavigate && !mayNavigate()))
        return false

    if (popIfPossible(stack))
        return true
    if (showHomeAtRoot) {
        showHomeAtRoot()
        return true
    }
    return false
}

