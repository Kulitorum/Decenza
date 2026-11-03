pragma ComponentBehavior: Bound

import QtQuick
import Decenza

RatioChoiceDialog {
    id: root
    titleText: TranslationManager.translate("water.ratio.title", "Added water ratio")
    subtitle: TranslationManager.translate("water.ratio.hint", "Added water as a multiple of target espresso yield")
    ratioPrefix: ""
    ratioSuffix: "×"
    allowCustom: true
    onOpened: customRatio = compareRatio > 0 ? compareRatio : 2.5
    presets: [
        {idx: 1, ratio: 2.0, key: "water.ratio.less", name: "Less water", desc: "2 g added water per gram of target espresso"},
        {idx: 2, ratio: 2.5, key: "water.ratio.default", name: "Default", desc: "2.5 g added water per gram of target espresso"},
        {idx: 3, ratio: 3.0, key: "water.ratio.more", name: "More water", desc: "3 g added water per gram of target espresso"}
    ]
}
