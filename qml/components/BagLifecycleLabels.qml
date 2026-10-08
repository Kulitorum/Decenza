pragma Singleton
import QtQuick
import Decenza

// The app's wording for CoffeeBag::lifecycleParts (Roasted · Thawed · Opened).
// C++ decides which parts appear and their ages; BagCard and BeanSummary only
// render this, and the web page words the same parts.
QtObject {

    // A stored date in the locale's short format; the raw text when it isn't ISO.
    function formatDate(raw) {
        if (!raw || raw.length < 8) return raw || ""
        var d = new Date(raw.substring(0, 10) + "T00:00:00")
        if (isNaN(d.getTime())) return raw
        return Qt.formatDate(d, Qt.locale().dateFormat(Locale.ShortFormat))
    }

    function describe(parts) {
        return (parts || []).map(function(p) {
            var date = formatDate(p.date)
            switch (p.kind) {
            case "roasted":
                return TranslationManager.translate("beans.summary.roastedDate", "Roasted %1").arg(date)
            case "frozen":
                return TranslationManager.translate("beans.summary.frozenDate", "Frozen %1").arg(date)
            case "thawed":
                return TranslationManager.translate("beans.summary.thawedDate", "Thawed %1 (%2d)").arg(date).arg(p.ageDays)
            case "opened":
                return TranslationManager.translate("beans.summary.openedDate", "Opened %1 (%2d)").arg(date).arg(p.ageDays)
            }
            return ""
        }).filter(function(s) { return s.length > 0 })
    }
}
