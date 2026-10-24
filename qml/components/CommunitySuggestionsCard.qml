pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Decenza

// "Community & your history" (Bean Search): for the active COFFEE bag, YOUR OWN
// profiles and last grind for this bean, what the Visualizer community liked
// (Apply saves it as a new recipe and selects it; Create recipe opens the wizard
// prefilled), a button that opens the filtered Visualizer Community page, and
// opt-in web research.
//
// Both hosts use the shared bean, history and profile facades. Network/provider
// work and recipe application stay on the native host.
Rectangle {
    id: root

    // Parametrized by the host, so the same
    // card can serve the ACTIVE bag (BeanInfoPage) or a just-picked bag in a
    // later phase. Community/canonical is coffee-only, so the host gates isCoffee.
    property string beanBrand: ""
    property string beanType: ""
    property string roastLevel: ""
    property bool isCoffee: true
    // Canonical bag id (Visualizer UUID), when the active bag is linked — adds the
    // exact-match community leg. Empty is fine (bean_brand/bean_type search only).
    property string beanBaseId: ""
    // The bag Apply and Create recipe link (0 → the active bag).
    property int bagId: 0
    // The new-bag window: the bag isn't saved yet, so Apply and Create recipe are a
    // choice (tap to select, tap again to clear) the window carries out with
    // runPending() once it has saved the bag.
    property bool deferActions: false
    property string pendingAction: ""   // "" | "apply" | "create"
    // False when the new bag is going into the freezer: Apply then saves the recipe
    // without selecting it, as the window keeps the current bag.
    property bool selectOnApply: true
    property var _queuedRun: null       // runPending's args while the profile is still fetching
    // The bag's product page, for scoping web research to its roaster's site.
    property string productUrl: ""
    readonly property bool _hasTemperatureOverride: Settings.brew.hasTemperatureOverride
    readonly property real _temperatureOverride: Settings.brew.temperatureOverride
    readonly property int _recipeBagId: bagId > 0 ? bagId : Math.max(BagService.activeBagId, 0)

    // --- Community (Bean Search phase 4): authenticated in-app community shots for
    // this bean via BeanUiService, summarised by CommunityAggregator.
    // Falls back to the Phase-1 deep link when the endpoint is absent (apiAvailable
    // false) — then the community section renders nothing and only the button shows.
    readonly property bool _commEnabled: SettingsPreferencesService.preferences["visualizer.communitySearchEnabled"] === true
                                         && BeanUiService.communityApiAvailable
    property string _commKey: ""
    property bool _commLoading: false
    property string _commStatus: ""      // "" | "network" (superseded/unsupported don't paint an error)
    property bool _commRetried: false    // guards the one re-issue after an external supersede
    property var _summary: ({})
    readonly property var _topProfile: (_summary.profiles && _summary.profiles.length > 0)
                                       ? _summary.profiles[0] : null
    readonly property int _commShotCount: Number(_summary.shotCount ?? 0)
    readonly property int _commUserCount: Number(_summary.userCount ?? 0)
    readonly property bool _commHasSuggestion: _summary.hasSuggestion === true
    readonly property bool _commIsAllTime: _summary.isAllTime === true
    readonly property var _commShots: _summary.shots || []
    readonly property bool _commHasData: _commShotCount > 0

    // Apply (phase 5): tap → fetch the top shot's profile (temperature + import JSON) →
    // confirmation sheet → BeanUiService.applyCommunitySuggestion. Lazy — only on tap.
    property bool _applyFetching: false
    property string _applyRequestId: ""
    property string _applyShotId: ""
    property double _applyTempC: -1
    property string _applyProfileJson: ""
    property string _applyFetchedTitle: ""   // the profile's REAL title (from the fetch), for lookup/import dedup
    property bool _applyTempAvailable: false
    property string _applyResult: ""   // "" | "failed" (Apply's outcome is a shared global toast)
    property string _fetchFor: ""      // "apply" | "create": what the profile fetch is for
    property bool _applySubmitted: false // one recipe per confirmation, however fast the taps
    readonly property string _topShotId: root._topProfile ? String(root._topProfile.topShotId || "") : ""
    readonly property bool _canApply: BeanUiService.ready && root._commEnabled && !root._commLoading && root._commHasSuggestion
                                      && root._topProfile !== null && root._topShotId !== ""

    // Web research (phase 6a): opt-in, uses the user's chosen AI provider to search the
    // web; shown as CITED ADVICE, never applied, never mixed into the community numbers.
    readonly property bool _webAvailable: BeanUiService.ready && BeanUiService.isConfigured
                                          && BeanUiService.currentProviderSupportsWebSearch
    property bool _webSearching: false
    property string _webToken: ""
    property string _webAdvice: ""
    property string _webError: ""

    // History requests are correlated per card; changing the bean discards its
    // old request IDs before issuing the next reads.
    property string _rankedRequestId: ""
    property string _grindRequestId: ""
    property bool _historyFailed: false
    readonly property bool _historyReady: RecipeContext.historyReady
    readonly property bool _beanServiceReady: BeanUiService.ready
    on_HistoryReadyChanged: {
        if (_historyReady) {
            requestOwnHistory()
        } else {
            _rankedRequestId = ""
            _grindRequestId = ""
            _ranked = ({})
            _grind = ({})
            _historyFailed = true
        }
    }
    on_BeanServiceReadyChanged: if (_beanServiceReady) Qt.callLater(root.refresh)
    on_CommEnabledChanged: Qt.callLater(root.refresh)
    property var _ranked: ({})
    property var _grind: ({})
    // Coffee card: show only coffee-drink profiles — exclude tea and maintenance
    // (cleaning/descale) by their single-source beverage bucket
    // (ProfileService.beverageBucketForTitle → BeverageType::bucket). An unknown
    // bucket (a deleted profile we can't classify) is shown. Filter THEN cap at 3.
    readonly property var _profiles: {
        const out = []
        const src = _ranked.withBean || []
        for (let i = 0; i < src.length && out.length < 3; ++i) {
            const name = String(src[i].profileName || "")
            const bucket = name !== "" ? ProfileService.beverageBucketForTitle(name) : ""
            if (bucket === "tea" || bucket === "maintenance")
                continue
            out.push(src[i])
        }
        return out
    }
    readonly property string _grindSetting: String(_grind.grinderSetting || "")
    readonly property bool _hasGrind: _grindSetting !== ""
    // The latest grind can come from any profile; don't surface one dialed under a
    // tea/maintenance profile on a coffee card.
    readonly property bool _grindIsCoffee: {
        const name = String(_grind.profileName || "")
        if (name === "")
            return true
        const bucket = ProfileService.beverageBucketForTitle(name)
        return bucket !== "tea" && bucket !== "maintenance"
    }
    readonly property bool _showGrind: _hasGrind && _grindIsCoffee
    readonly property bool _hasLocal: _profiles.length > 0 || _showGrind

    visible: isCoffee && beanBrand !== "" && beanType !== ""
    implicitHeight: column.implicitHeight + Theme.spacingMedium * 2
    color: Theme.cardBackgroundColor
    radius: Theme.cardRadius

    // A host sets these together (one bag), so one refresh per event-loop turn, not
    // one per property: each refresh is a rate-limited Visualizer search (10/min).
    // beanBaseId alone can change on link/unlink; searchKey() includes it.
    onBeanBrandChanged: Qt.callLater(root.refresh)
    onBeanTypeChanged: Qt.callLater(root.refresh)
    onRoastLevelChanged: Qt.callLater(root.refresh)
    onBeanBaseIdChanged: Qt.callLater(root.refresh)
    onIsCoffeeChanged: Qt.callLater(root.refresh)
    Component.onCompleted: root.refresh()

    function refresh() {
        root._rankedRequestId = ""
        root._grindRequestId = ""
        root._ranked = ({})
        root._grind = ({})
        root._summary = ({})
        root._commStatus = ""
        root._commLoading = false
        root._commRetried = false
        root._commKey = ""
        // Drop any Apply in flight for the old bean so a late fetch can't open a
        // franken-sheet over the new one.
        root._applyRequestId = ""
        root._applyShotId = ""
        root._applyFetching = false
        root._applyResult = ""
        root._applyFetchedTitle = ""
        root._applyProfileJson = ""
        root.pendingAction = ""
        root._queuedRun = null
        applyDialog.close()
        // Drop stale web research for the old bean.
        root._webSearching = false
        root._webToken = ""
        root._webAdvice = ""
        root._webError = ""
        // Coffee only, and a hidden card must not search (visible doesn't stop this).
        if (!root.isCoffee || root.beanBrand === "" || root.beanType === "")
            return
        requestOwnHistory()
        if (root._commEnabled) {
            root._commKey = BeanUiService.communitySearchKey(root.beanBrand, root.beanType, root.beanBaseId)
            root._commLoading = true
            BeanUiService.communitySearch(root.beanBrand, root.beanType, root.beanBaseId)
        }
    }

    // lastUsed is epoch SECONDS (shothistorystorage_queries.cpp:852). 0/absent → "".
    function whenText(epochSeconds) {
        if (!epochSeconds)
            return ""
        return new Date(Number(epochSeconds) * 1000).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    // Format a median to one decimal (never "||" — a 0 g dose is falsy).
    function _oneDp(v) {
        return Number(v ?? 0).toFixed(1)
    }

    // The title the profile is actually stored under: the fetched real title when we
    // have it (the community row's modal title can differ, which would re-import a
    // duplicate every Apply), else the modal title (the only one we have on fetch failure).
    function _lookupTitle() {
        if (root._applyFetchedTitle !== "")
            return root._applyFetchedTitle
        return root._topProfile ? String(root._topProfile.title || "") : ""
    }
    function _profileInstalled() {
        return _lookupTitle() !== "" && ProfileService.findProfileByTitle(_lookupTitle()) !== ""
    }

    // Current EFFECTIVE values (for the sheet's "old → new"), not the vestigial
    // import-only settings keys: override if armed, else the loaded profile's own.
    function _currentTemp() {
        return root._hasTemperatureOverride
             ? root._temperatureOverride : Number(ProfileService.currentProfileSummary.espressoTemperatureC ?? 0)
    }
    function _currentRatio() {
        const dose = RecipeContext.preferences.dyeBeanWeight
        if (dose <= 0)
            return 0
        return ProfileManager.targetWeight / dose
    }

    function startApply() {
        if (!root._canApply)
            return
        root._applySubmitted = false
        root._applyResult = ""
        root._fetchFor = "apply"
        root._applyShotId = root._topShotId
        root._applyFetching = true
        root._applyRequestId = BeanUiService.fetchCommunityProfile(root._applyShotId)
    }

    // Create recipe: the wizard, prefilled; an installed profile needs no fetch.
    function startCreate() {
        if (!root._canApply)
            return
        root._applyResult = ""
        // The family title can differ from the top shot's own (version suffixes merge
        // into one family), so resolve the concrete title first, as Apply does.
        if (root._applyFetchedTitle !== "" && root._profileInstalled()) {
            root._openWizard("")
            return
        }
        root._fetchFor = "create"
        root._applyShotId = root._topShotId
        root._applyFetching = true
        root._applyRequestId = BeanUiService.fetchCommunityProfile(root._applyShotId)
    }

    function _prefill(profileJson) {
        const tp = root._topProfile || ({})
        const ratio = Number((tp.ratio || {}).median ?? 0)
        return {
            name: "",
            profileTitle: root._lookupTitle(),
            profileJson: profileJson,
            bagId: root._recipeBagId,
            beanBaseId: root.beanBaseId,
            roasterName: root.beanBrand,
            coffeeName: root.beanType,
            equipmentId: EquipmentService.activePackageId > 0 ? EquipmentService.activePackageId : 0,
            doseG: Number((tp.dose || {}).median ?? 0),
            yieldMode: ratio > 0 ? "ratio" : "none",
            yieldValue: Math.round(ratio * 10) / 10,
            tempOffsetC: 0,
            grindPinned: tp.grinderMatched === true ? String(tp.grindRepresentative || "") : ""
        }
    }

    function _openWizard(profileJson) {
        AppShell.recipeWizardRequested("create", { prefill: root._prefill(profileJson) })
    }

    // Select (or clear) what the new-bag window does on save; the top shot's
    // profile is fetched now when it isn't installed, so save has it in hand.
    function togglePending(kind) {
        if (root.pendingAction === kind) {
            root.pendingAction = ""
            return
        }
        root.pendingAction = kind
        root._applyResult = ""
        if (root._applyFetchedTitle !== "" && (root._profileInstalled() || root._applyProfileJson !== ""))
            return
        root._fetchFor = "pending"
        root._applyShotId = root._topShotId
        root._applyFetching = true
        root._applyRequestId = BeanUiService.fetchCommunityProfile(root._applyShotId)
    }

    // Run the selected choice for the bag the new-bag window just saved. While the
    // profile it needs is still being fetched, it runs when the fetch lands.
    function runPending(bagId, roaster, coffee, beanBaseId) {
        if (root.pendingAction !== "" && root._applyFetching && root._fetchFor === "pending") {
            root._queuedRun = { bagId: bagId, roaster: roaster, coffee: coffee, beanBaseId: beanBaseId }
            return
        }
        root._queuedRun = null
        if (root.pendingAction === "apply") {
            const s = root._suggestion()
            s.bagId = bagId
            s.roaster = roaster
            s.coffee = coffee
            s.beanBaseId = beanBaseId
            s.select = root.selectOnApply
            BeanUiService.applyCommunitySuggestion(s)
        } else if (root.pendingAction === "create") {
            const p = root._prefill(root._profileInstalled() ? "" : root._applyProfileJson)
            p.bagId = bagId
            p.roasterName = roaster
            p.coffeeName = coffee
            p.beanBaseId = beanBaseId
            AppShell.recipeWizardRequested("create", { prefill: p })
        }
        root.pendingAction = ""
    }

    ExternalUrlOpener { id: externalUrlOpener }
    function openExternal(url) {
        const reservation = externalUrlOpener.reserve()
        if (reservation) externalUrlOpener.openReserved(reservation, String(url))
    }

    function startWebResearch() {
        if (!root._webAvailable || root._webSearching)
            return
        root._webError = ""
        root._webAdvice = ""
        root._webToken = root.beanBrand + "\n" + root.beanType
        root._webSearching = true
        BeanUiService.researchBean(root._webToken, root.beanBrand, root.beanType, root.productUrl)
    }

    function _suggestion() {
        const tp = root._topProfile || ({})
        return {
            "key": root._commKey,
            "title": root._lookupTitle(),
            "topShotId": root._applyShotId,
            "profileJson": root._applyProfileJson,
            "doseG": Number((tp.dose || {}).median ?? 0),
            "ratio": Number((tp.ratio || {}).median ?? 0),
            "tempC": root._applyTempAvailable ? root._applyTempC : -1,
            "grind": String(tp.grindRepresentative || ""),
            "grinderMatched": tp.grinderMatched === true,
            "bagId": root._recipeBagId,
            "roaster": root.beanBrand,
            "coffee": root.beanType,
            "beanBaseId": root.beanBaseId
        }
    }

    function _confirmApply() {
        if (root._applySubmitted)
            return
        root._applySubmitted = true
        applyDialog.close()
        BeanUiService.applyCommunitySuggestion(root._suggestion())
    }

    function requestOwnHistory() {
        root._rankedRequestId = ""
        root._grindRequestId = ""
        root._historyFailed = false
        if (!RecipeContext.historyReady || !root.isCoffee || root.beanBrand === "" || root.beanType === "")
            return
        root._rankedRequestId = RecipeContext.newRequestId()
        RecipeContext.requestHistory(root._rankedRequestId, "rankedProfilesForBean", {
            beanBrand: root.beanBrand, beanType: root.beanType, roastLevel: root.roastLevel, teaType: ""
        })
        root._grindRequestId = RecipeContext.newRequestId()
        RecipeContext.requestHistory(root._grindRequestId, "latestGrindForBean", {
            beanBrand: root.beanBrand, beanType: root.beanType, roastLevel: root.roastLevel, equipmentId: 0
        })
    }

    Connections {
        target: RecipeContext
        function onHistoryCompleted(requestId, operation, success, result, reason) {
            if (operation === "rankedProfilesForBean" && requestId === root._rankedRequestId) {
                root._rankedRequestId = ""
                root._ranked = success ? result : ({})
                if (!success) root._historyFailed = true
            } else if (operation === "latestGrindForBean" && requestId === root._grindRequestId) {
                root._grindRequestId = ""
                root._grind = success ? result : ({})
                if (!success) root._historyFailed = true
            }
        }
    }

    // Community replies are keyed by searchKey(); drop any that aren't for the bean
    // shown here (the client is a shared singleton). "unsupported" latches apiAvailable
    // false (handled by _commEnabled) and needs no error paint. "superseded" means
    // another consumer of the shared client (the MCP bean_search community action, the
    // web /api/beans/community endpoint) displaced our in-flight search — the winner's
    // reply carries a different key, so ours is never coming. We re-issue once
    // (latest-wins + those consumers are one-shot, so it converges); a second
    // supersession stops rather than loop.
    Connections {
        target: BeanUiService
        function onCommunityResultsReady(key, summary) {
            if (key !== root._commKey)
                return
            root._summary = summary
            root._commStatus = ""
            root._commLoading = false
        }
        function onCommunitySearchFailed(key, status) {
            if (key !== root._commKey)
                return
            if (status === "superseded") {
                if (!root._commRetried) {
                    root._commRetried = true
                    BeanUiService.communitySearch(root.beanBrand, root.beanType, root.beanBaseId)
                } else {
                    root._commLoading = false
                    root._commStatus = "network"
                }
                return
            }
            root._commLoading = false
            root._commStatus = status === "unsupported" ? "" : "network"
        }
        // Apply's profile fetch. On success → open the sheet. On failure → the sheet
        // can still open IF the profile is installed locally (temp just unavailable);
        // otherwise there's nothing to apply onto, so fail honestly.
        function onCommunityProfileReady(requestId, shotId, result) {
            if (requestId !== root._applyRequestId || shotId !== root._applyShotId)
                return
            root._applyRequestId = ""
            root._applyFetching = false
            if (root._fetchFor === "pending") {
                root._applyProfileJson = String(result.profileJson || "")
                root._applyFetchedTitle = String(result.title || "")
                const queued = root._queuedRun
                if (queued)
                    root.runPending(queued.bagId, queued.roaster, queued.coffee, queued.beanBaseId)
                return
            }
            if (root._fetchFor === "create") {
                root._applyFetchedTitle = String(result.title || "")
                root._openWizard(root._profileInstalled() ? "" : String(result.profileJson || ""))
                return
            }
            root._applyProfileJson = String(result.profileJson || "")
            root._applyFetchedTitle = String(result.title || "")
            // The recipe uses an installed profile's own temperature, not the fetched one.
            root._applyTempC = root._profileInstalled()
                ? Number(ProfileService.profileCatalogInfoForTitle(root._lookupTitle()).espressoTemperatureC
                         ?? result.temperatureC ?? -1)
                : Number(result.temperatureC ?? -1)
            root._applyTempAvailable = true
            applyDialog.open()
        }
        function onCommunityProfileFailed(requestId, shotId, status) {
            if (requestId !== root._applyRequestId || shotId !== root._applyShotId)
                return
            root._applyRequestId = ""
            root._applyFetching = false
            root._applyTempAvailable = false
            root._applyProfileJson = ""
            if (root._fetchFor === "pending") {
                const queued = root._queuedRun
                if (queued) {
                    // The bag is saved: carry on. Apply without the profile is refused
                    // and reported by the toast; Create recipe lets the user pick one.
                    root.runPending(queued.bagId, queued.roaster, queued.coffee, queued.beanBaseId)
                } else if (!root._profileInstalled()) {
                    root.pendingAction = ""
                    root._applyResult = "failed"
                }
                return
            }
            if (root._profileInstalled())
                root._fetchFor === "create" ? root._openWizard("") : applyDialog.open()
            else
                root._applyResult = "failed"
        }
    }

    Connections {
        target: BeanUiService
        function onCommunitySuggestionApplied(key, ok, recipeName, selected) {
            if (key !== root._commKey)
                return
            applyDialog.close()
        }
    }

    // Web-research replies (phase 6a), keyed by our token so a reply for a bean we no
    // longer show is dropped.
    Connections {
        target: BeanUiService
        function onBeanResearchReady(token, advice) {
            if (token !== root._webToken)
                return
            root._webSearching = false
            root._webAdvice = advice
            root._webError = ""
        }
        function onBeanResearchFailed(token, error) {
            if (token !== root._webToken)
                return
            root._webSearching = false
            root._webError = error
        }
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingSmall

        Text {
            Layout.fillWidth: true
            // Re-titles toward "Community" only while the community lane is enabled AND
            // real community data has rendered; disabled/loading/empty/error/absent keep
            // the honest Phase-1 title.
            text: (root._commEnabled && root._commHasData)
                  ? TranslationManager.translate("beaninfo.community.titleCommunity", "Community & your history")
                  : TranslationManager.translate("beaninfo.community.title", "Your history & Visualizer")
            font: Theme.subtitleFont
            color: Theme.textColor
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Text {
            visible: root._profiles.length > 0
            Layout.fillWidth: true
            text: TranslationManager.translate("beaninfo.community.yourProfiles", "Your profiles for this bean")
            font: Theme.labelFont
            color: Theme.textSecondaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Repeater {
            model: root._profiles
            delegate: Text {
                required property var modelData
                readonly property string _when: root.whenText(modelData.lastUsed)
                Layout.fillWidth: true
                text: "•  " + String(modelData.profileName || "")
                      + (_when !== ""
                         ? "  " + TranslationManager.translate("beaninfo.community.lastUsed", "last used %1").arg(_when)
                         : "")
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        // Honour matchLevel: never present a similar-roast grind as this bean's own.
        Text {
            visible: root._showGrind
            Layout.fillWidth: true
            text: {
                const profile = String(root._grind.profileName || "")
                if (String(root._grind.matchLevel || "") === "similarRoast")
                    return TranslationManager.translate("beaninfo.community.grindSimilar", "Similar-roast grind: %1 (with %2)")
                        .arg(root._grindSetting).arg(profile)
                return TranslationManager.translate("beaninfo.community.grindBean", "Last grind for this bean: %1 (with %2)")
                    .arg(root._grindSetting).arg(profile)
            }
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // No local history yet — still offer the community link.
        Text {
            visible: !root._hasLocal
            Layout.fillWidth: true
            text: !root._historyReady || root._historyFailed
                  ? TranslationManager.translate("beaninfo.community.historyUnavailable", "Your shot history is unavailable.")
                  : root._rankedRequestId !== "" || root._grindRequestId !== ""
                    ? TranslationManager.translate("common.loading", "Loading…")
                    : TranslationManager.translate("beaninfo.community.noLocalShots", "No shots logged for this bean yet.")
            font: Theme.bodyFont
            color: !root._historyReady || root._historyFailed ? Theme.warningColor : Theme.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // ---- Community section (phase 4). One state at a time; hidden entirely
        // when disabled or the endpoint is absent (apiAvailable false), leaving the
        // Phase-1 card + deep link as the honest fallback.
        Rectangle {
            visible: root._commEnabled
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSmall
            implicitHeight: 1
            color: Theme.borderColor
        }

        // Loading.
        Text {
            visible: root._commEnabled && root._commLoading
            Layout.fillWidth: true
            text: TranslationManager.translate("beaninfo.community.loading", "Checking community shots…")
            font: Theme.bodyFont
            color: Theme.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // Count line (shots + users), with an all-time qualifier.
        Text {
            visible: root._commEnabled && !root._commLoading && root._commHasData
            Layout.fillWidth: true
            text: (root._commIsAllTime
                   ? TranslationManager.translate("beaninfo.community.countLineAllTime", "%1 shots from %2 users (all time)")
                   : TranslationManager.translate("beaninfo.community.countLine", "%1 shots from %2 users"))
                  .arg(root._commShotCount).arg(root._commUserCount)
            font: Theme.labelFont
            color: Theme.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // Top community profile summary (medians), shown when there's a suggestion.
        Text {
            visible: root._commEnabled && !root._commLoading && root._commHasSuggestion && root._topProfile !== null
            Layout.fillWidth: true
            text: TranslationManager.translate("beaninfo.community.topProfile", "Top community profile: %1")
                  .arg(root._topProfile ? String(root._topProfile.title || "") : "")
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Text {
            visible: root._commEnabled && !root._commLoading && root._commHasSuggestion && root._topProfile !== null
            Layout.fillWidth: true
            text: {
                if (root._topProfile === null)
                    return ""
                const d = root._topProfile.dose || {}
                const y = root._topProfile.yield || {}
                const ra = root._topProfile.ratio || {}
                const t = root._topProfile.timeSec || {}
                return TranslationManager.translate("beaninfo.community.profileParams", "Dose %1 g → %2 g (1:%3), %4 s")
                    .arg(root._oneDp(d.median)).arg(root._oneDp(y.median))
                    .arg(root._oneDp(ra.median)).arg(root._oneDp(t.median))
            }
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // Grind only when the community used a grinder matching yours.
        Text {
            visible: root._commEnabled && !root._commLoading && root._commHasSuggestion
                     && root._topProfile !== null && root._topProfile.grinderMatched === true
                     && String((root._topProfile || {}).grindRepresentative || "") !== ""
            Layout.fillWidth: true
            text: root._topProfile
                  ? TranslationManager.translate("beaninfo.community.profileGrind", "Grind %1 (same grinder: %2)")
                        .arg(String(root._topProfile.grindRepresentative || ""))
                        .arg(String(root._topProfile.grinderDisplayName || ""))
                  : ""
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // Low sample: too few distinct users to rank — list a few shots instead.
        Text {
            visible: root._commEnabled && !root._commLoading && !root._commHasSuggestion && root._commHasData
            Layout.fillWidth: true
            text: TranslationManager.translate("beaninfo.community.fewShots", "Only a few community shots for this bean so far:")
            font: Theme.bodyFont
            color: Theme.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Repeater {
            model: (root._commEnabled && !root._commLoading && !root._commHasSuggestion && root._commHasData)
                   ? root._commShots.slice(0, 5) : []
            delegate: Text {
                required property var modelData
                Layout.fillWidth: true
                text: "•  " + TranslationManager.translate("beaninfo.community.shotRow", "%1 — %2 g → %3 g, enjoyment %4")
                      .arg(String(modelData.profileTitle || ""))
                      .arg(root._oneDp(modelData.doseG)).arg(root._oneDp(modelData.yieldG))
                      .arg(modelData.rated ? String(Math.round(Number(modelData.enjoyment ?? 0))) : "—")
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        // Empty (reached the endpoint, nothing matched).
        Text {
            visible: root._commEnabled && !root._commLoading && root._commStatus === "" && !root._commHasData
                     && Object.keys(root._summary).length > 0
            Layout.fillWidth: true
            text: TranslationManager.translate("beaninfo.community.empty", "No community shots found for this bean.")
            font: Theme.bodyFont
            color: Theme.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // Network error + retry.
        RowLayout {
            visible: root._commEnabled && !root._commLoading && root._commStatus === "network"
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("beaninfo.community.networkError", "Couldn't reach Visualizer.")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            AccessibleButton {
                text: TranslationManager.translate("beaninfo.community.retry", "Retry")
                accessibleName: TranslationManager.translate("beaninfo.community.accessible.retry",
                                                             "Retry loading community shots")
                onClicked: root.refresh()
            }
        }

        // Apply saves the top suggestion as a new recipe and selects it (after the
        // sheet); Create recipe opens the wizard on it. Both fetch the top shot's
        // profile first when it isn't installed. In the new-bag window they are a
        // single choice the window carries out when it saves the bag.
        Flow {
            visible: root._canApply
            Layout.fillWidth: true
            Layout.topMargin: Theme.scaled(4)
            spacing: Theme.spacingSmall
            AccessibleButton {
                enabled: !root._applyFetching
                primary: root.deferActions && root.pendingAction === "apply"
                text: root._applyFetching && root._fetchFor === "apply"
                      ? TranslationManager.translate("beaninfo.community.applying", "Fetching shot details…")
                      : TranslationManager.translate("beaninfo.community.apply", "Apply")
                accessibleName: root.deferActions
                    ? TranslationManager.translate("beaninfo.community.accessible.applyOnSave",
                          "Apply when the bag is saved: save the top community suggestion as a new recipe and select it")
                      + (root.pendingAction === "apply"
                         ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                    : TranslationManager.translate("beaninfo.community.accessible.apply",
                          "Save the top community suggestion as a new recipe and select it")
                onClicked: root.deferActions ? root.togglePending("apply") : root.startApply()
            }
            AccessibleButton {
                enabled: !root._applyFetching
                primary: root.deferActions && root.pendingAction === "create"
                text: root._applyFetching && root._fetchFor === "create"
                      ? TranslationManager.translate("beaninfo.community.applying", "Fetching shot details…")
                      : TranslationManager.translate("beaninfo.community.createRecipe", "Create recipe")
                accessibleName: root.deferActions
                    ? TranslationManager.translate("beaninfo.community.accessible.createOnSave",
                          "Create recipe when the bag is saved: open the recipe editor with the top community suggestion")
                      + (root.pendingAction === "create"
                         ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                    : TranslationManager.translate("beaninfo.community.accessible.createRecipe",
                          "Open the recipe editor with the top community suggestion")
                onClicked: root.deferActions ? root.togglePending("create") : root.startCreate()
            }
        }

        Text {
            visible: root._canApply && root.deferActions
            Layout.fillWidth: true
            text: root.pendingAction === "apply"
                  ? (root.selectOnApply
                     ? TranslationManager.translate("beaninfo.community.onSave.apply",
                           "When you save the bag: saves a new recipe, %1, and selects it.")
                     : TranslationManager.translate("beaninfo.community.onSave.applyFrozen",
                           "When you save the bag: saves a new recipe, %1. It isn't selected while the bag is frozen."))
                        .arg(BeanUiService.communityRecipeName(root.beanType, root._lookupTitle()))
                  : root.pendingAction === "create"
                    ? TranslationManager.translate("beaninfo.community.onSave.create",
                          "When you save the bag: opens the recipe editor with this suggestion.")
                    : TranslationManager.translate("beaninfo.community.onSave.none",
                          "Pick Apply or Create recipe to use this suggestion when you save the bag.")
            font: Theme.labelFont
            color: Theme.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Text {
            visible: root._applyResult !== ""
            Layout.fillWidth: true
            text: TranslationManager.translate("beaninfo.community.applyFailed", "Couldn't apply the suggestion.")
            font: Theme.bodyFont
            color: Theme.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        AccessibleButton {
            Layout.topMargin: Theme.scaled(4)
            text: TranslationManager.translate("beaninfo.community.openVisualizer", "Open on Visualizer")
            accessibleName: TranslationManager.translate("beaninfo.community.accessible.openVisualizer",
                                                         "Open community shots for this bean on Visualizer")
            onClicked: root.openExternal(BeanUiService.communityPageUrl(root.beanBrand, root.beanType))
        }

        // ---- Web research (phase 6a): opt-in, uses the chosen AI provider. Hidden
        // unless that provider can search the web. Advice is cited and SHOWN ONLY —
        // never applied, never folded into the community numbers.
        Rectangle {
            visible: root._webAvailable
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSmall
            implicitHeight: 1
            color: Theme.borderColor
        }
        Text {
            visible: root._webAvailable
            Layout.fillWidth: true
            text: TranslationManager.translate("beaninfo.community.web.sectionTitle", "From the web")
            font: Theme.labelFont
            color: Theme.textSecondaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        AccessibleButton {
            visible: root._webAvailable && root._webAdvice === "" && root._webError === ""
            enabled: !root._webSearching
            Layout.topMargin: Theme.scaled(2)
            text: root._webSearching
                  ? TranslationManager.translate("beaninfo.community.web.searching", "Searching the web…")
                  : TranslationManager.translate("beaninfo.community.web.button", "Search the web")
            accessibleName: text
            onClicked: root.startWebResearch()
        }
        Text {
            visible: root._webAvailable && root._webAdvice === "" && root._webError === "" && !root._webSearching
            Layout.fillWidth: true
            // Translations are merged from the backend (unauthenticated upload), so this string is
            // attacker-influenceable; escape it and render as StyledText — sitting beside the RichText
            // web-advice renderer, an unescaped remote <img> in a translation would otherwise be
            // fetched on render. Same guard as BeanBaseDetailsRow.
            text: Theme.escapeHtml(TranslationManager.translate("beaninfo.community.web.costHint",
                                               "Uses your AI provider — web search may cost extra"))
            textFormat: Text.StyledText
            font: Theme.labelFont
            color: Theme.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
        // Markdown → HTML → emoji-as-img → RichText (the app's safe AI-text path — a bare
        // MarkdownText truncates at the first emoji and can crash the macOS render thread).
        // RichText ignores elide, so WordWrap only; onLinkActivated opens the cited sources.
        Text {
            visible: root._webAvailable && root._webAdvice !== ""
            Layout.fillWidth: true
            text: Theme.replaceEmojiWithImg(MarkdownRenderer.toHtml(root._webAdvice), Theme.bodyFont.pixelSize, true)
            textFormat: Text.RichText
            onLinkActivated: (link) => root.openExternal(link)
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: root._webAdvice
        }
        Text {
            visible: root._webAvailable && root._webAdvice !== ""
            Layout.fillWidth: true
            text: TranslationManager.translate("beaninfo.community.web.disclaimer",
                      "Advice from web sources, not community data — check the sources before using it.")
            font: Theme.labelFont
            color: Theme.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        AccessibleButton {
            visible: root._webAvailable && root._webError !== ""
            Layout.topMargin: Theme.scaled(2)
            text: TranslationManager.translate("beaninfo.community.web.failed", "Web search failed — tap to try again")
            accessibleName: text
            onClicked: root.startWebResearch()
        }
    }

    // Confirmation sheet (phase 5). Lists what Apply will change (old → new), read from
    // the current setup. Buttons live in the content (DecenzaDialog renders no footer
    // unless standardButtons is set).
    DecenzaDialog {
        id: applyDialog
        logName: "communityApply"
        anchors.centerIn: Overlay.overlay
        width: Math.min(Theme.scaled(460), root.width - Theme.scaled(24))
        padding: Theme.scaled(20)
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        readonly property var _tp: root._topProfile || ({})

        contentItem: ColumnLayout {
            spacing: Theme.spacingSmall

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("beaninfo.community.applySheet.title", "Apply community suggestion?")
                font: Theme.subtitleFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("beaninfo.community.applySheet.recipe",
                          "Saves a new recipe, %1, and selects it. Your current recipe isn't changed.")
                      .arg(BeanUiService.communityRecipeName(root.beanType, root._lookupTitle()))
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                // _lookupTitle() (not the family title _tp.title): aggregation merges
                // version-suffixed titles into one family, and Apply installs/imports the
                // concrete fetched title — so name the profile that will actually be applied.
                text: root._profileInstalled()
                      ? TranslationManager.translate("beaninfo.community.applySheet.profile", "Profile: %1")
                            .arg(root._lookupTitle())
                      : TranslationManager.translate("beaninfo.community.applySheet.profileImport", "Profile: %1 (will be imported from Visualizer)")
                            .arg(root._lookupTitle())
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("beaninfo.community.applySheet.dose", "Dose: %1 g → %2 g")
                      .arg(root._oneDp(RecipeContext.preferences.dyeBeanWeight)).arg(root._oneDp((applyDialog._tp.dose || {}).median))
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("beaninfo.community.applySheet.ratio", "Ratio: 1:%1 → 1:%2")
                      .arg(root._oneDp(root._currentRatio())).arg(root._oneDp((applyDialog._tp.ratio || {}).median))
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }
            Text {
                visible: root._applyTempAvailable
                Layout.fillWidth: true
                text: TranslationManager.translate("beaninfo.community.applySheet.temp", "Temperature: %1 °C → %2 °C")
                      .arg(root._oneDp(root._currentTemp())).arg(root._oneDp(root._applyTempC))
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }
            Text {
                visible: !root._applyTempAvailable
                Layout.fillWidth: true
                text: TranslationManager.translate("beaninfo.community.applySheet.tempUnavailable", "Temperature couldn't be read from the top shot.")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                wrapMode: Text.WordWrap
            }
            Text {
                visible: applyDialog._tp.grinderMatched === true && String(applyDialog._tp.grindRepresentative || "") !== ""
                Layout.fillWidth: true
                text: TranslationManager.translate("beaninfo.community.applySheet.grind", "Grind: %1 → %2")
                      .arg(String(RecipeContext.preferences.dyeGrinderSetting || "—")).arg(String(applyDialog._tp.grindRepresentative || ""))
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSmall
                spacing: Theme.spacingSmall
                Item { Layout.fillWidth: true }
                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: text
                    onClicked: applyDialog.close()
                }
                AccessibleButton {
                    text: TranslationManager.translate("common.button.apply", "Apply")
                    accessibleName: text
                    onClicked: root._confirmApply()
                }
            }
        }
    }
}
