.pragma library

// Browser history is a transport for route identity and small replayable
// arguments only. It must never carry service snapshots, credentials, local
// paths, or arbitrary page objects.
var routeNames = [
    "home", "recipes", "history", "beans", "equipment", "espresso", "steam",
    "hotwater", "flush", "profiles", "favorites", "favoriteInfo",
    "screensaver", "descaling", "transport", "flowCalibration", "sensorCalibration",
    "settings", "profileImport", "visualizer", "visualizerMultiImport", "ai",
    "addLanguage", "stringBrowser", "shotDetail", "shotComparison", "postShotReview",
    "profileInfo", "profileEditor", "recipeWizard"
]

var maximumStateChars = 262144
var maximumFragmentChars = 2048
var favoriteGroups = ["bean", "profile", "bean_profile", "bean_profile_grinder",
                      "bean_profile_grinder_weight"]

function _stringArg(value, maximum) {
    return typeof value === "string" && value.length <= maximum ? value : ""
}

function _stringFits(value, maximum) {
    return value === undefined || value === null
        || (typeof value === "string" && value.length <= maximum)
}

function _positiveId(value) {
    var number = Number(value)
    return Number.isSafeInteger(number) && number > 0 ? number : 0
}

function _copyShotIds(value, requireUnique) {
    // QML lists and QVariantList properties are array-like Qt sequences, not
    // Array objects (Qt 6.12 qv4sequenceobject_p.h:143, qv4object.cpp:1105).
    if (!value || typeof value !== "object" || !Number.isInteger(value.length)
            || value.length < 0 || value.length > 500)
        return null
    var output = []
    for (var i = 0; i < value.length; ++i) {
        var id = _positiveId(value[i])
        if (id <= 0 || (requireUnique && output.indexOf(id) >= 0)) return null
        output.push(id)
    }
    return output
}

function _shotNeighborWindow(value, shotId) {
    if (!value || typeof value !== "object" || !Number.isSafeInteger(value.length) || value.length < 0)
        return null
    if (value.length <= 500) return _copyShotIds(value, false)
    // History can contain more rows than one replayable route. Keep the opened
    // record and its neighbors; cap lookup work even for an unusually large list.
    if (value.length > 100000) return shotId > 0 ? [shotId] : []
    var selected = -1
    for (var i = 0; i < value.length; ++i) {
        if (_positiveId(value[i]) === shotId) { selected = i; break }
    }
    if (selected < 0) return shotId > 0 ? [shotId] : []
    var start = Math.max(0, Math.min(selected - 249, value.length - 500))
    var neighbors = []
    for (var j = start; j < start + 500; ++j) neighbors.push(value[j])
    return _copyShotIds(neighbors, false)
}

function _copyChipIds(value, allowed) {
    if (!value || typeof value !== "object" || !Number.isInteger(value.length)
            || value.length < 0 || value.length > allowed.length) return null
    var output = []
    for (var i = 0; i < value.length; ++i) {
        if (allowed.indexOf(value[i]) < 0 || output.indexOf(value[i]) >= 0) return null
        output.push(value[i])
    }
    return output
}

function _copyFilter(source, depth) {
    depth = depth || 0
    if (depth > 2) return null
    if (!source || typeof source !== "object" || Array.isArray(source))
        return ({})
    var output = ({})
    var stringFields = ["profileName", "beanBrand", "beanType", "grinderBrand", "grinderModel",
                        "grinderBurrs", "grinderSetting", "roastLevel", "searchText", "recipeName",
                        "bagTerm", "sortField", "sortDirection", "bagLabel", "equipmentLabel"]
    for (var i = 0; i < stringFields.length; ++i) {
        var key = stringFields[i]
        var maximum = key === "searchText" ? 8192 : 160
        if (typeof source[key] === "string" && !_stringFits(source[key], maximum))
            return null
        if (typeof source[key] === "string")
            output[key] = _stringArg(source[key], maximum)
    }
    var numberFields = ["equipmentId", "minEnjoyment", "maxEnjoyment", "minDose", "maxDose",
                        "minYield", "maxYield", "targetWeight", "minDuration", "maxDuration",
                        "minTds", "maxTds", "minEy", "maxEy", "dateFrom", "dateTo",
                        "recipeId", "bagId"]
    for (var j = 0; j < numberFields.length; ++j) {
        var numberKey = numberFields[j]
        if (source[numberKey] !== undefined && source[numberKey] !== null
                && !Number.isFinite(Number(source[numberKey])))
            return null
        if (Number.isFinite(Number(source[numberKey])))
            output[numberKey] = Number(source[numberKey])
    }
    var booleanFields = ["onlyWithVisualizer", "filterChanneling", "filterGrindIssue",
                         "filterSkipFirstFrame", "filterPourTruncated"]
    for (var k = 0; k < booleanFields.length; ++k) {
        var booleanKey = booleanFields[k]
        if (source[booleanKey] !== undefined && source[booleanKey] !== null
                && typeof source[booleanKey] !== "boolean")
            return null
        if (typeof source[booleanKey] === "boolean")
            output[booleanKey] = source[booleanKey]
    }
    if (typeof source.initialFilter === "object" && source.initialFilter !== null) {
        output.initialFilter = _copyFilter(source.initialFilter, depth + 1)
        if (!output.initialFilter) return null
    }
    return output
}

function _copyRecipePrefill(source) {
    if (!source || typeof source !== "object" || Array.isArray(source))
        return ({})
    var allowed = ["id", "name", "drinkType", "beanId", "bagId", "beanBaseId", "roasterName",
                   "coffeeName", "equipmentId", "doseG", "yieldValue", "yieldMode", "yieldG", "tempOffsetC", "profileJson",
                   "grindPinned", "rpmPinned", "steamJson", "hotWaterJson", "createdFromShotId",
                   "clonedFromRecipeId", "profileFilename", "profileTitle"]
    var stringFields = ["name", "drinkType", "beanBaseId", "roasterName", "coffeeName", "yieldMode",
                        "profileJson", "grindPinned", "steamJson", "hotWaterJson", "profileFilename", "profileTitle"]
    var numberFields = ["id", "beanId", "bagId", "equipmentId", "doseG", "yieldValue", "yieldG",
                        "tempOffsetC", "rpmPinned", "createdFromShotId", "clonedFromRecipeId"]
    var output = ({})
    for (var i = 0; i < allowed.length; ++i) {
        var key = allowed[i]
        var value = source[key]
        if (typeof value === "string") {
            if (!_stringFits(value, 65536)) return null
            output[key] = value
        } else if (value !== undefined && value !== null && stringFields.indexOf(key) >= 0) {
            return null
        } else if (typeof value === "number" && Number.isFinite(value)) {
            output[key] = value
        } else if (value !== undefined && value !== null && numberFields.indexOf(key) >= 0) {
            return null
        }
    }
    return output
}

function normalizeEntry(value) {
    if (!value || typeof value !== "object" || Array.isArray(value)
            || routeNames.indexOf(value.route) < 0)
        return null
    var args = value.args && typeof value.args === "object" && !Array.isArray(value.args)
            ? value.args : ({})
    var safe = ({})
    switch (value.route) {
    case "steam":
    case "hotwater":
    case "flush":
        if (args.openedFromSettingsSearch !== undefined
                && typeof args.openedFromSettingsSearch !== "boolean") return null
        if (args.openedFromSettingsSearch === true) safe.openedFromSettingsSearch = true
        break
    case "stringBrowser":
        if (!_stringFits(args.searchText, 8192)) return null
        safe.searchText = _stringArg(args.searchText, 8192)
        safe.filterMode = args.filterMode === undefined ? 0 : args.filterMode
        if (!Number.isInteger(safe.filterMode) || safe.filterMode < 0 || safe.filterMode > 3) return null
        break
    case "profiles":
        if (args.pickerContext !== undefined) {
            var picker = args.pickerContext
            if (!picker || typeof picker !== "object" || !_stringFits(picker.searchText, 8192)
                    || (picker.sortMode !== "usage" && picker.sortMode !== "alpha")) return null
            var sources = _copyChipIds(picker.sources, ["builtin", "downloaded", "mine"])
            var beverages = _copyChipIds(picker.beverages, ["espresso", "filter", "tea", "maintenance"])
            if (!sources || !beverages || typeof picker.favorites !== "boolean") return null
            safe.pickerContext = {searchText: _stringArg(picker.searchText, 8192), favorites: picker.favorites,
                                  sources: sources, beverages: beverages, sortMode: picker.sortMode}
        }
        break
    case "history":
        safe = _copyFilter(args)
        if (!safe) return null
        if (args.selectedShots !== undefined) {
            safe.selectedShots = _copyShotIds(args.selectedShots, true)
            if (!safe.selectedShots) return null
        }
        if (args.selectionHostId !== undefined) {
            if (!_stringFits(args.selectionHostId, 64)) return null
            safe.selectionHostId = _stringArg(args.selectionHostId, 64)
        }
        if (args.viewport !== undefined && args.viewport !== null) {
            var viewport = args.viewport
            if (typeof viewport !== "object" || !Number.isInteger(viewport.loaded)
                    || viewport.loaded < 1 || viewport.loaded > 5000
                    || !Number.isFinite(viewport.y) || viewport.y < 0) return null
            safe.viewport = {loaded: viewport.loaded, y: viewport.y}
        }
        break
    case "recipes":
    case "beans":
        if (!_stringFits(args.searchQuery, 8192)) return null
        safe.searchQuery = _stringArg(args.searchQuery, 8192)
        if (value.route === "recipes") safe.showArchived = args.showArchived === true
        else safe.showFinished = args.showFinished === true
        break
    case "shotComparison":
        if (args.selectedShotIds !== undefined) {
            safe.selectedShotIds = _copyShotIds(args.selectedShotIds, true)
            if (!safe.selectedShotIds) return null
            safe.baseShotId = _positiveId(args.baseShotId)
            if (safe.baseShotId > 0 && safe.selectedShotIds.indexOf(safe.baseShotId) < 0) return null
            safe.windowStart = Number(args.windowStart || 0)
            if (!Number.isInteger(safe.windowStart) || safe.windowStart < 0
                    || safe.windowStart > Math.max(0, safe.selectedShotIds.length - 3)) return null
        }
        break
    case "settings":
        if (!_stringFits(args.requestedTabId, 64)) return null
        safe.requestedTabId = _stringArg(args.requestedTabId, 64)
        break
    case "sensorCalibration":
        if (args.sensor !== undefined && !Number.isInteger(Number(args.sensor))) return null
        safe.sensor = Number.isInteger(Number(args.sensor)) ? Number(args.sensor) : 0
        if (safe.sensor < 0 || safe.sensor > 4) return null
        break
    case "shotDetail":
    case "postShotReview": {
        safe.editShotId = _positiveId(args.editShotId)
        safe.autoClose = args.autoClose === true
        if (args.shotIds !== undefined) {
            safe.shotIds = _shotNeighborWindow(args.shotIds, safe.editShotId)
            if (!safe.shotIds) return null
        }
        break
    }
    case "favoriteInfo": {
        var fields = ["shotId", "groupBy", "beanBrand", "beanType", "profileName", "grinderBrand",
                      "grinderModel", "equipmentName", "grinderSetting"]
        for (var f = 0; f < fields.length; ++f) {
            var key = fields[f]
            if (typeof args[key] === "string" && !_stringFits(args[key], 160)) return null
            if (typeof args[key] === "string") safe[key] = _stringArg(args[key], 160)
        }
        ["shotId", "equipmentId", "doseBucket", "targetWeight", "avgEnjoyment", "shotCount"].forEach(function(key) {
            if (args[key] !== undefined && args[key] !== null
                    && !Number.isFinite(Number(args[key]))) safe.__invalidNumber = true
            else if (Number.isFinite(Number(args[key]))) safe[key] = Number(args[key])
        })
        if (safe.__invalidNumber) return null
        delete safe.__invalidNumber
        if (args.resolveFromShot === true) safe.resolveFromShot = true
        break
    }
    case "profileInfo":
        if (!_stringFits(args.profileFilename, 200) || !_stringFits(args.profileName, 160)) return null
        safe.profileFilename = _stringArg(args.profileFilename, 200)
        safe.profileName = _stringArg(args.profileName, 160)
        break
    case "profileEditor":
        if (!_stringFits(args.editorEntryFilename, 200)
                || !_stringFits(args.editorEntryTitle, 160)) return null
        safe.editorEntryFilename = _stringArg(args.editorEntryFilename, 200)
        if (args.editorEntryType !== undefined && ["pressure", "flow", "dflow", "aflow", "advanced"].indexOf(args.editorEntryType) < 0)
            return null
        safe.editorEntryType = ["pressure", "flow", "dflow", "aflow", "advanced"].indexOf(args.editorEntryType) >= 0
                ? args.editorEntryType : "advanced"
        safe.editorEntryTitle = _stringArg(args.editorEntryTitle, 160)
        safe.editorEntryIsNew = args.editorEntryIsNew === true
        break
    case "recipeWizard":
        if (args.mode !== undefined && args.mode !== "edit" && args.mode !== "create") return null
        safe.mode = args.mode === "edit" ? "edit" : "create"
        if (_positiveId(args.editRecipeId)) safe.editRecipeId = _positiveId(args.editRecipeId)
        if (Number.isFinite(Number(args.promoteShotId)) && Number(args.promoteShotId) > 0)
            safe.promoteShotId = Number(args.promoteShotId)
        if (args.prefill && typeof args.prefill === "object") {
            safe.prefill = _copyRecipePrefill(args.prefill)
            if (!safe.prefill) return null
        }
        break
    default:
        break
    }
    return {route: value.route, args: safe}
}

function normalizeStack(value) {
    if (!Array.isArray(value) || value.length < 1 || value.length > 64)
        return null
    var stack = []
    for (var i = 0; i < value.length; ++i) {
        var entry = normalizeEntry(value[i])
        if (!entry) return null
        stack.push(entry)
    }
    if (stack[0].route !== "home") return null
    return stack
}

function makeState(sessionId, index, stack) {
    var safeStack = normalizeStack(stack)
    if (!safeStack || typeof sessionId !== "string" || sessionId.length < 8 || sessionId.length > 64
            || !Number.isSafeInteger(index) || index < 0)
        return null
    var state = {__decenzaHistory: 1, sessionId: sessionId, index: index, stack: safeStack}
    var serialized = JSON.stringify(state)
    return serialized.length <= maximumStateChars ? serialized : null
}

function parseState(value) {
    var state = value
    if (typeof value === "string") {
        if (value.length > maximumStateChars) return null
        try { state = JSON.parse(value) } catch (_) { return null }
    }
    try {
        if (JSON.stringify(state).length > maximumStateChars) return null
    } catch (_) {
        return null
    }
    if (!state || state.__decenzaHistory !== 1)
        return null
    var stack = normalizeStack(state.stack)
    if (!stack || typeof state.sessionId !== "string" || state.sessionId.length < 8
            || !Number.isSafeInteger(state.index) || state.index < 0)
        return null
    return {__decenzaHistory: 1, sessionId: state.sessionId, index: state.index, stack: stack}
}

function _encodedPublicFilename(value) {
    if (typeof value !== "string" || value.length === 0 || value.length > 200
            || value === "." || value === ".." || /[\/\\:\x00-\x1f\x7f]/.test(value)) return ""
    try { return encodeURIComponent(value) } catch (_) { return "" }
}

function _urlRecordId(value) {
    var id = _positiveId(value)
    // The shared shot/recipe pages accept their record ID through QML int inputs.
    return id <= 2147483647 ? id : 0
}

// Only minimal public record identity goes in the URL. Full stack, filters,
// neighbor lists, editor metadata and draft seeds remain in history.state.
function entryLocationHash(value) {
    var entry = normalizeEntry(value)
    if (!entry || entry.route === "home") return ""
    var args = entry.args
    if (entry.route === "favoriteInfo" && _urlRecordId(args.shotId) > 0
            && favoriteGroups.indexOf(args.groupBy) >= 0)
        return entry.route + "/" + args.groupBy + "/" + args.shotId
    if ((entry.route === "shotDetail" || entry.route === "postShotReview") && _urlRecordId(args.editShotId) > 0)
        return entry.route + "/" + args.editShotId
    if (entry.route === "sensorCalibration") return entry.route + "/" + args.sensor
    if (entry.route === "recipeWizard" && args.mode === "edit" && _urlRecordId(args.editRecipeId) > 0)
        return entry.route + "/" + args.editRecipeId
    var filename = entry.route === "profileInfo" ? args.profileFilename
        : entry.route === "profileEditor" && !args.editorEntryIsNew ? args.editorEntryFilename : ""
    var encoded = _encodedPublicFilename(filename)
    return encoded ? entry.route + "/" + encoded : entry.route
}

function routeHash(stack) {
    return Array.isArray(stack) && stack.length > 0 ? entryLocationHash(stack[stack.length - 1]) : ""
}

function _parseLocationHash(value) {
    if (typeof value !== "string") return null
    var fragment = value.charAt(0) === "#" ? value.slice(1) : value
    if (fragment.length > maximumFragmentChars) return null
    try { fragment = decodeURIComponent(fragment) } catch (_) { return null }
    if (fragment.length > 256) return null
    if (fragment === "") return {entry: {route: "home", args: ({})}, hasIdentity: false}
    var parts = fragment.split("/")
    if (parts.length === 1) {
        var alias = normalizeEntry({route: fragment, args: ({})})
        return alias ? {entry: alias, hasIdentity: false} : null
    }
    if (parts.length === 3 && parts[0] === "favoriteInfo") {
        var group = parts[1]
        var shotId = _urlRecordId(parts[2])
        if (favoriteGroups.indexOf(group) < 0
                || shotId <= 0 || String(shotId) !== parts[2]) return null
        return {entry: normalizeEntry({route: "favoriteInfo",
                    args: {groupBy: group, shotId: shotId, resolveFromShot: true}}), hasIdentity: true}
    }
    if (parts.length !== 2) return null
    var route = parts[0]
    var identity = parts[1]
    var args = ({})
    if (route === "shotDetail" || route === "postShotReview" || route === "recipeWizard") {
        var id = _urlRecordId(identity)
        if (id <= 0 || String(id) !== identity) return null
        if (route === "recipeWizard") args = {mode: "edit", editRecipeId: id}
        else args = {editShotId: id, autoClose: false}
    } else if (route === "profileInfo" || route === "profileEditor") {
        if (!_encodedPublicFilename(identity)) return null
        args = route === "profileInfo" ? {profileFilename: identity} : {editorEntryFilename: identity}
    } else if (route === "sensorCalibration") {
        if (!/^[0-4]$/.test(identity)) return null
        args = {sensor: Number(identity)}
    } else return null
    var entry = normalizeEntry({route: route, args: args})
    return entry ? {entry: entry, hasIdentity: true} : null
}

function normalizeLocationHash(value) {
    var parsed = _parseLocationHash(value)
    return parsed ? parsed.entry : null
}

// Generic links use the host's current profile; named links select the same
// catalog editor as ProfilePicker. URL text cannot supply editor metadata.
function resolveLocationEntry(value, profiles, currentProfile) {
    var entry = normalizeEntry(value)
    if (!entry || entry.route !== "profileEditor") return entry
    if (!entry.args.editorEntryFilename) {
        var type = currentProfile && currentProfile.editorType
        if (["pressure", "flow", "dflow", "aflow", "advanced"].indexOf(type) >= 0)
            entry.args.editorEntryType = type
        return entry
    }
    if (!profiles || !Number.isSafeInteger(profiles.length)) return null
    for (var i = 0; i < profiles.length; ++i) {
        var row = profiles[i]
        if (!row || row.name !== entry.args.editorEntryFilename) continue
        if (["pressure", "flow", "dflow", "aflow", "advanced"].indexOf(row.editorType) < 0) return null
        return normalizeEntry({route: "profileEditor", args: {editorEntryFilename: row.name,
            editorEntryType: row.editorType}})
    }
    return null
}

function stateMatchesLocationHash(stateValue, hashValue) {
    var state = parseState(stateValue)
    var parsed = _parseLocationHash(hashValue)
    if (!state || !parsed) return false
    var current = state.stack[state.stack.length - 1]
    return parsed.hasIdentity ? entryLocationHash(current) === entryLocationHash(parsed.entry)
                              : current.route === parsed.entry.route
}

function sameStack(left, right) {
    return JSON.stringify(normalizeStack(left)) === JSON.stringify(normalizeStack(right))
}

function isStackPrefix(prefix, stack) {
    var safePrefix = normalizeStack(prefix)
    var safeStack = normalizeStack(stack)
    if (!safePrefix || !safeStack || safePrefix.length > safeStack.length)
        return false
    for (var i = 0; i < safePrefix.length; ++i) {
        if (JSON.stringify(safePrefix[i]) !== JSON.stringify(safeStack[i]))
            return false
    }
    return true
}

function commonStackPrefixLength(left, right) {
    var safeLeft = normalizeStack(left)
    var safeRight = normalizeStack(right)
    if (!safeLeft || !safeRight) return 0
    var length = Math.min(safeLeft.length, safeRight.length)
    var index = 0
    while (index < length
           && JSON.stringify(safeLeft[index]) === JSON.stringify(safeRight[index]))
        ++index
    return index
}

// Only replay declared page inputs. In particular history's text is restored
// through its initial-search input, not an undeclared searchText property.
function pageProperties(entry, hostId) {
    var args = entry.args || ({})
    switch (entry.route) {
    case "steam":
    case "hotwater":
    case "flush": return args.openedFromSettingsSearch === true
        ? {openedFromSettingsSearch: true} : ({})
    case "stringBrowser": return {initialSearchText: args.searchText || "", initialFilterMode: args.filterMode || 0}
    case "profiles": return {initialPickerContext: args.pickerContext || null}
    case "history": return {initialFilter: args.initialFilter || null,
                             initialSearchText: typeof args.searchText === "string" ? args.searchText : null,
                             initialSelectedShots: args.selectionHostId && args.selectionHostId === hostId
                                                   ? (args.selectedShots || []) : [],
                             initialViewport: args.selectionHostId && args.selectionHostId === hostId
                                              ? (args.viewport || null) : null}
    case "recipes": return {searchQuery: args.searchQuery || "", showArchived: args.showArchived === true,
                             retainSearchOnActivation: true}
    case "beans": return {searchQuery: args.searchQuery || "", showFinished: args.showFinished === true,
                           retainSearchOnActivation: true}
    case "sensorCalibration": return {sensor: args.sensor || 0}
    case "settings": return {requestedTabId: args.requestedTabId || ""}
    case "shotDetail": return {editShotId: args.editShotId || 0, shotIds: args.shotIds || [], autoClose: false}
    case "postShotReview": return {editShotId: args.editShotId || 0, shotIds: args.shotIds || [], autoClose: args.autoClose === true}
    case "favoriteInfo": return args
    case "profileInfo": return {profileFilename: args.profileFilename || "", profileName: args.profileName || ""}
    case "recipeWizard":
    case "profileEditor": return args
    default: return ({})
    }
}

function entryWithPageContext(entry, page) {
    var names = {settings: "settingsPage", history: "shotHistoryPage",
                 shotDetail: "postShotReviewPage", postShotReview: "postShotReviewPage",
                 recipes: "recipesPage", beans: "bagInventoryPage", shotComparison: "shotComparisonPage",
                 stringBrowser: "stringBrowserPage", profiles: "profileSelectorPage"}
    if (!entry || !page || names[entry.route] !== page.objectName
            || !page.navigationContext || typeof page.navigationContext !== "object")
        return null
    return normalizeEntry({route: entry.route, args: Object.assign({}, entry.args, page.navigationContext)})
}

function pageHasUnsavedChanges(page) {
    return !!page && ((typeof page.profileModified === "boolean" && page.profileModified)
        || (typeof page.browserHasUnsavedChanges === "boolean" && page.browserHasUnsavedChanges))
}

function stackHasUnsavedChanges(stack) {
    // currentItem records same-depth replacements as a binding dependency too.
    if (pageHasUnsavedChanges(stack.currentItem)) return true
    for (var i = 0; i < stack.depth; ++i)
        if (pageHasUnsavedChanges(stack.get(i))) return true
    return false
}
