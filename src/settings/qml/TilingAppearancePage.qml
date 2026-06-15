// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.tilingAppearancePage
    readonly property int gapMax: root.settingsBridge.autotileGapMax
    // Per-screen override helper for the Gaps card below. The border / colour /
    // titlebar cards that used to live here moved to the Decoration page
    // (per-surface chains + global defaults); only the per-screen Gaps card
    // remains on this page.
    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }

    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    contentHeight: content.implicitHeight
    clip: true

    PerScreenOverrideHelper {
        id: psHelper

        appSettings: settingsController
        // Shared app-wide scope — a monitor picked on any per-monitor page
        // stays picked here.
        selectedScreenName: settingsController.scopeScreenName
        getterMethod: "getPerScreenAutotileSettings"
        setterMethod: "setPerScreenAutotileSetting"
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Gaps (per-screen) — the only card left on this page. Border, colour,
        // and titlebar controls moved to the Decoration page. The card opts
        // into the header scope chip, reading as a normal global card until
        // you pick a monitor.
        // =================================================================
        GapsSettingsCard {
            Layout.fillWidth: true
            scopeEnabled: true
            scopeAppSettings: settingsController
            // Gaps sub-domain only — must not report/reset the Algorithm card's
            // per-monitor overrides (shared autotile map, disjoint key subsets).
            scopeHasOverridesMethod: "hasPerScreenAutotileGapsSettings"
            scopeClearerMethod: "clearPerScreenAutotileGapsSettings"
            gapMax: root.gapMax
            gapMin: root.settingsBridge.autotileGapMin
            primaryGapMin: root.settingsBridge.autotileInnerGapMin
            primaryGapMax: root.settingsBridge.autotileInnerGapMax
            primaryGapValue: root.settingValue("InnerGap", appSettings.autotileInnerGap)
            outerGapValue: root.settingValue("OuterGap", appSettings.autotileOuterGap)
            usePerSideOuterGap: root.settingValue("UsePerSideOuterGap", appSettings.autotileUsePerSideOuterGap)
            smartGapsValue: root.settingValue("SmartGaps", appSettings.autotileSmartGaps)
            outerGapTopValue: root.settingValue("OuterGapTop", appSettings.autotileOuterGapTop)
            outerGapBottomValue: root.settingValue("OuterGapBottom", appSettings.autotileOuterGapBottom)
            outerGapLeftValue: root.settingValue("OuterGapLeft", appSettings.autotileOuterGapLeft)
            outerGapRightValue: root.settingValue("OuterGapRight", appSettings.autotileOuterGapRight)
            onPrimaryGapModified: value => {
                return root.writeSetting("InnerGap", value, function (v) {
                    appSettings.autotileInnerGap = v;
                });
            }
            onOuterGapModified: value => {
                return root.writeSetting("OuterGap", value, function (v) {
                    appSettings.autotileOuterGap = v;
                });
            }
            onUsePerSideOuterGapToggled: checked => {
                return root.writeSetting("UsePerSideOuterGap", checked, function (v) {
                    appSettings.autotileUsePerSideOuterGap = v;
                });
            }
            onOuterGapTopModified: value => {
                return root.writeSetting("OuterGapTop", value, function (v) {
                    appSettings.autotileOuterGapTop = v;
                });
            }
            onOuterGapBottomModified: value => {
                return root.writeSetting("OuterGapBottom", value, function (v) {
                    appSettings.autotileOuterGapBottom = v;
                });
            }
            onOuterGapLeftModified: value => {
                return root.writeSetting("OuterGapLeft", value, function (v) {
                    appSettings.autotileOuterGapLeft = v;
                });
            }
            onOuterGapRightModified: value => {
                return root.writeSetting("OuterGapRight", value, function (v) {
                    appSettings.autotileOuterGapRight = v;
                });
            }
            onSmartGapsToggled: checked => {
                return root.writeSetting("SmartGaps", checked, function (v) {
                    appSettings.autotileSmartGaps = v;
                });
            }
        }
    }
}
