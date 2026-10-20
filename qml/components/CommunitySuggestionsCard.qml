pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Decenza

// "Your history & Visualizer" (Bean Search, phase 1): for the active COFFEE bag,
// a read-only summary of YOUR OWN profiles and last grind for this bean, plus a
// button that opens the filtered Visualizer Community page in a browser. No
// community data is fetched in-app yet — that waits on a Visualizer
// GET /api/community/shots endpoint (a later phase); until then the community
// path is the deep link, and the title names honestly what it shows today.
//
// Upstream-clean: depends only on MainController.shotHistory, Settings.dye and
// Qt.openUrlExternally — no src/barista dependency, so the feature can be offered
// upstream to Kulitorum/Decenza.
Rectangle {
    id: root

    // Parametrized by the host, not bound to Settings internally, so the same
    // card can serve the ACTIVE bag (BeanInfoPage) or a just-picked bag in a
    // later phase. Community/canonical is coffee-only, so the host gates isCoffee.
    property string beanBrand: ""
    property string beanType: ""
    property string roastLevel: ""
    property bool isCoffee: true
    // Canonical bag id (Visualizer UUID), when the active bag is linked — adds the
    // exact-match community leg. Empty is fine (bean_brand/bean_type search only).
    property string beanBaseId: ""

    // --- Community (Bean Search phase 4): authenticated in-app community shots for
    // this bean via MainController.communitySettings, summarised by CommunityAggregator.
    // Falls back to the Phase-1 deep link when the endpoint is absent (apiAvailable
    // false) — then the community section renders nothing and only the button shows.
    readonly property bool _commEnabled: Settings.visualizer.communitySearchEnabled
                                         && MainController.communitySettings.apiAvailable
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
    // confirmation sheet → MainController.applyCommunitySuggestion. Lazy — only on tap.
    property bool _applyFetching: false
    property string _applyShotId: ""
    property double _applyTempC: -1
    property string _applyProfileJson: ""
    property string _applyFetchedTitle: ""   // the profile's REAL title (from the fetch), for lookup/import dedup
    property bool _applyTempAvailable: false
    property string _applyResult: ""   // "" | "applied" | "failed"
    readonly property string _topShotId: root._topProfile ? String(root._topProfile.topShotId || "") : ""
    readonly property bool _canApply: root._commEnabled && !root._commLoading && root._commHasSuggestion
                                      && root._topProfile !== null && root._topShotId !== ""

    // Web research (phase 6a): opt-in, uses the user's chosen AI provider to search the
    // web; shown as CITED ADVICE, never applied, never mixed into the community numbers.
    readonly property bool _webAvailable: MainController.aiManager.isConfigured
                                          && MainController.aiManager.currentProviderSupportsWebSearch()
    property bool _webSearching: false
    property string _webToken: ""
    property string _webAdvice: ""
    property string _webError: ""

    // Replies arrive on MainController.shotHistory, a singleton every surface
    // shares, so each is dropped unless its echoed query matches the bean shown
    // here (same guard ProfilePicker/RecipeWizardPage use).
    property var _ranked: ({})
    property var _grind: ({})
    // Coffee card: show only coffee-drink profiles — exclude tea and maintenance
    // (cleaning/descale) by their single-source beverage bucket
    // (ProfileManager.beverageBucketForTitle → Profile::beverageBucket). An unknown
    // bucket (a deleted profile we can't classify) is shown. Filter THEN cap at 3.
    readonly property var _profiles: {
        const out = []
        const src = _ranked.withBean || []
        for (let i = 0; i < src.length && out.length < 3; ++i) {
            const name = String(src[i].profileName || "")
            const bucket = name !== "" ? ProfileManager.beverageBucketForTitle(name) : ""
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
        const bucket = ProfileManager.beverageBucketForTitle(name)
        return bucket !== "tea" && bucket !== "maintenance"
    }
    readonly property bool _showGrind: _hasGrind && _grindIsCoffee
    readonly property bool _hasLocal: _profiles.length > 0 || _showGrind

    visible: isCoffee && beanBrand !== "" && beanType !== ""
    implicitHeight: column.implicitHeight + Theme.spacingMedium * 2
    color: Theme.cardBackgroundColor
    radius: Theme.cardRadius

    onBeanBrandChanged: root.refresh()
    onBeanTypeChanged: root.refresh()
    onRoastLevelChanged: root.refresh()
    Component.onCompleted: root.refresh()

    function refresh() {
        root._ranked = ({})
        root._grind = ({})
        root._summary = ({})
        root._commStatus = ""
        root._commLoading = false
        root._commRetried = false
        root._commKey = ""
        // Drop any Apply in flight for the old bean so a late fetch can't open a
        // franken-sheet over the new one.
        root._applyShotId = ""
        root._applyFetching = false
        root._applyResult = ""
        root._applyFetchedTitle = ""
        applyDialog.close()
        // Drop stale web research for the old bean.
        root._webSearching = false
        root._webToken = ""
        root._webAdvice = ""
        root._webError = ""
        if (root.beanBrand === "" && root.beanType === "")
            return
        // teaType omitted (coffee-only card) → "" default on the C++ side.
        MainController.shotHistory.requestRankedProfilesForBean(root.beanBrand, root.beanType, root.roastLevel)
        MainController.shotHistory.requestLatestGrindForBean(root.beanBrand, root.beanType, root.roastLevel)
        if (root._commEnabled) {
            root._commKey = MainController.communitySettings.searchKey(root.beanBrand, root.beanType, root.beanBaseId)
            root._commLoading = true
            MainController.communitySettings.search(root.beanBrand, root.beanType, root.beanBaseId)
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
        return _lookupTitle() !== "" && ProfileManager.findProfileByTitle(_lookupTitle()) !== ""
    }

    // Current EFFECTIVE values (for the sheet's "old → new"), not the vestigial
    // import-only settings keys: override if armed, else the loaded profile's own.
    function _currentTemp() {
        return Settings.brew.hasTemperatureOverride
             ? Settings.brew.temperatureOverride : ProfileManager.profileTargetTemperature
    }
    function _currentRatio() {
        const dose = Settings.dye.dyeBeanWeight
        if (dose <= 0)
            return 0
        const y = Settings.brew.hasBrewYieldOverride
                ? Settings.brew.brewYieldOverride : ProfileManager.profileTargetWeight
        return y / dose
    }

    function startApply() {
        if (!root._canApply)
            return
        root._applyResult = ""
        root._applyShotId = root._topShotId
        root._applyFetching = true
        MainController.communitySettings.fetchShotProfile(root._applyShotId)
    }

    // The roaster's product URL from the active bag's Bean Base blob, for scoping the
    // web search. Empty → an unscoped search.
    function _productUrl() {
        try {
            return String(JSON.parse(Settings.dye.dyeBeanBaseData || "{}").link || "")
        } catch (e) {
            return ""
        }
    }

    function startWebResearch() {
        if (!root._webAvailable || root._webSearching)
            return
        root._webError = ""
        root._webAdvice = ""
        root._webToken = root.beanBrand + "\n" + root.beanType
        root._webSearching = true
        MainController.aiManager.researchBean(root._webToken, root.beanBrand, root.beanType, root._productUrl())
    }

    function _confirmApply() {
        const tp = root._topProfile || ({})
        MainController.applyCommunitySuggestion({
            "key": root._commKey,
            "title": root._lookupTitle(),
            "topShotId": root._applyShotId,
            "profileJson": root._applyProfileJson,
            "doseG": Number((tp.dose || {}).median ?? 0),
            "ratio": Number((tp.ratio || {}).median ?? 0),
            "tempC": root._applyTempAvailable ? root._applyTempC : -1,
            "grind": String(tp.grindRepresentative || ""),
            "grinderMatched": tp.grinderMatched === true
        })
    }

    Connections {
        target: MainController.shotHistory
        function onRankedProfilesForBeanReady(result) {
            if (String(result.queryBrand || "") !== root.beanBrand
                || String(result.queryType || "") !== root.beanType)
                return
            root._ranked = result
        }
        function onLatestGrindForBeanReady(grind) {
            if (String(grind.queryBrand || "") !== root.beanBrand
                || String(grind.queryType || "") !== root.beanType)
                return
            root._grind = grind
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
        target: MainController.communitySettings
        function onResultsReady(key, summary) {
            if (key !== root._commKey)
                return
            root._summary = summary
            root._commStatus = ""
            root._commLoading = false
        }
        function onSearchFailed(key, status) {
            if (key !== root._commKey)
                return
            if (status === "superseded") {
                if (!root._commRetried) {
                    root._commRetried = true
                    MainController.communitySettings.search(root.beanBrand, root.beanType, root.beanBaseId)
                } else {
                    root._commLoading = false
                    root._commStatus = "network"
                }
                return
            }
            root._commLoading = false
            root._commStatus = (status === "network") ? "network" : ""
        }
        // Apply's profile fetch. On success → open the sheet. On failure → the sheet
        // can still open IF the profile is installed locally (temp just unavailable);
        // otherwise there's nothing to apply onto, so fail honestly.
        function onShotProfileReady(shotId, result) {
            if (shotId !== root._applyShotId)
                return
            root._applyFetching = false
            root._applyTempC = Number(result.temperatureC ?? -1)
            root._applyProfileJson = String(result.profileJson || "")
            root._applyFetchedTitle = String(result.title || "")
            root._applyTempAvailable = true
            applyDialog.open()
        }
        function onShotProfileFailed(shotId, status) {
            if (shotId !== root._applyShotId || status === "superseded")
                return
            root._applyFetching = false
            root._applyTempAvailable = false
            root._applyProfileJson = ""
            if (root._profileInstalled())
                applyDialog.open()
            else
                root._applyResult = "failed"
        }
    }

    Connections {
        target: MainController
        function onCommunitySuggestionApplied(key, ok) {
            if (key !== root._commKey)
                return
            applyDialog.close()
            root._applyResult = ok ? "applied" : "failed"
        }
    }

    // Web-research replies (phase 6a), keyed by our token so a reply for a bean we no
    // longer show is dropped.
    Connections {
        target: MainController.aiManager
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
            text: TranslationManager.translate("beaninfo.community.noLocalShots", "No shots logged for this bean yet.")
            font: Theme.bodyFont
            color: Theme.textSecondaryColor
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

        // Apply the top suggestion (fetches the top shot's profile first).
        AccessibleButton {
            visible: root._canApply
            enabled: !root._applyFetching
            Layout.topMargin: Theme.scaled(4)
            text: root._applyFetching
                  ? TranslationManager.translate("beaninfo.community.applying", "Fetching shot details…")
                  : TranslationManager.translate("beaninfo.community.apply", "Apply")
            accessibleName: TranslationManager.translate("beaninfo.community.accessible.apply",
                                                         "Apply the top community suggestion to your setup")
            onClicked: root.startApply()
        }

        Text {
            visible: root._applyResult !== ""
            Layout.fillWidth: true
            text: root._applyResult === "applied"
                  ? TranslationManager.translate("beaninfo.community.applied", "Community suggestion applied")
                  : TranslationManager.translate("beaninfo.community.applyFailed", "Couldn't apply the suggestion.")
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
            onClicked: Qt.openUrlExternally(MainController.communitySettings.communityPageUrl(root.beanBrand, root.beanType))
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
            text: TranslationManager.translate("beaninfo.community.web.costHint",
                                               "Uses your AI provider — web search may cost extra")
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
            onLinkActivated: (link) => Qt.openUrlExternally(link)
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
                text: root._profileInstalled()
                      ? TranslationManager.translate("beaninfo.community.applySheet.profile", "Profile: %1")
                            .arg(String(applyDialog._tp.title || ""))
                      : TranslationManager.translate("beaninfo.community.applySheet.profileImport", "Profile: %1 (will be imported from Visualizer)")
                            .arg(String(applyDialog._tp.title || ""))
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("beaninfo.community.applySheet.dose", "Dose: %1 g → %2 g")
                      .arg(root._oneDp(Settings.dye.dyeBeanWeight)).arg(root._oneDp((applyDialog._tp.dose || {}).median))
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
                      .arg(String(Settings.dye.dyeGrinderSetting || "—")).arg(String(applyDialog._tp.grindRepresentative || ""))
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
