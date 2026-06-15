// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.snappingWindowAppearancePage
    // Per-screen snapping gap/padding helper for the Gaps card below. The
    // border / colour / titlebar cards that used to live here moved to the
    // Decoration page (per-surface chains + global defaults); only the
    // per-screen Gaps card remains on this page.

    function snappingSettingValue(key, globalValue) {
        return snappingHelper.settingValue(key, globalValue);
    }

    function writeSnappingSetting(key, value, globalSetter) {
        snappingHelper.writeSetting(key, value, globalSetter);
    }

    contentHeight: content.implicitHeight
    clip: true

    PerScreenOverrideHelper {
        id: snappingHelper

        appSettings: settingsController
        // Shared app-wide scope — a monitor picked on any per-monitor page
        // stays picked here.
        selectedScreenName: settingsController.scopeScreenName
        getterMethod: "getPerScreenSnappingSettings"
        setterMethod: "setPerScreenSnappingSetting"
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Gaps (per-screen) — the only card left on this page. Border, colour,
        // and titlebar controls moved to the Decoration page. The card opts
        // into the header scope chip.
        // =================================================================
        GapsSettingsCard {
            Layout.fillWidth: true
            scopeEnabled: true
            scopeAppSettings: settingsController
            scopeHasOverridesMethod: "hasPerScreenSnappingGapsSettings"
            scopeClearerMethod: "clearPerScreenSnappingGapsSettings"
            // Snapping shares the "Inner gap" / "Outer gap" labels with tiling
            // (consistent cross-mode wording) but has no Smart gaps. Inner-gap
            // bounds come from zonePaddingMin/Max; the outer / per-side gaps from
            // gapMin/Max — each matching its validator clamp.
            primaryGapLabel: i18n("Inner gap")
            primaryGapDescription: i18n("Space between snapped windows")
            outerGapLabel: i18n("Outer gap")
            outerGapDescription: i18n("Space from screen edges to snapped windows")
            showSmartGaps: false
            gapMin: root.settingsBridge.gapMin
            gapMax: root.settingsBridge.gapMax
            primaryGapMin: root.settingsBridge.zonePaddingMin
            primaryGapMax: root.settingsBridge.zonePaddingMax
            primaryGapValue: root.snappingSettingValue("ZonePadding", appSettings.zonePadding)
            outerGapValue: root.snappingSettingValue("OuterGap", appSettings.outerGap)
            usePerSideOuterGap: root.snappingSettingValue("UsePerSideOuterGap", appSettings.usePerSideOuterGap)
            outerGapTopValue: root.snappingSettingValue("OuterGapTop", appSettings.outerGapTop)
            outerGapBottomValue: root.snappingSettingValue("OuterGapBottom", appSettings.outerGapBottom)
            outerGapLeftValue: root.snappingSettingValue("OuterGapLeft", appSettings.outerGapLeft)
            outerGapRightValue: root.snappingSettingValue("OuterGapRight", appSettings.outerGapRight)
            onPrimaryGapModified: value => {
                return root.writeSnappingSetting("ZonePadding", value, function (v) {
                    appSettings.zonePadding = v;
                });
            }
            onOuterGapModified: value => {
                return root.writeSnappingSetting("OuterGap", value, function (v) {
                    appSettings.outerGap = v;
                });
            }
            onUsePerSideOuterGapToggled: checked => {
                return root.writeSnappingSetting("UsePerSideOuterGap", checked, function (v) {
                    appSettings.usePerSideOuterGap = v;
                });
            }
            onOuterGapTopModified: value => {
                return root.writeSnappingSetting("OuterGapTop", value, function (v) {
                    appSettings.outerGapTop = v;
                });
            }
            onOuterGapBottomModified: value => {
                return root.writeSnappingSetting("OuterGapBottom", value, function (v) {
                    appSettings.outerGapBottom = v;
                });
            }
            onOuterGapLeftModified: value => {
                return root.writeSnappingSetting("OuterGapLeft", value, function (v) {
                    appSettings.outerGapLeft = v;
                });
            }
            onOuterGapRightModified: value => {
                return root.writeSnappingSetting("OuterGapRight", value, function (v) {
                    appSettings.outerGapRight = v;
                });
            }
        }
    }
}
