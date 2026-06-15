// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Decoration → General — the global baseline defaults (path "").
 *
 * This is the "base curve" analogue of AnimationsGeneralPage: every
 * per-surface card (Windows / OSDs / Popups) inherits from here unless it
 * defines its own override. It edits the DecorationProfileTree baseline via
 * `settingsController.decorationPage` with the empty surface path "":
 *   - a default decoration-pack chain (ChainEditor), and
 *   - a border/titlebar card with width / radius spinboxes, active +
 *     inactive color pickers, useSystemColors / showBorder / hideTitlebar
 *     toggles.
 *
 * Reactive-latch pattern: imperative refresh from the controller on
 * `profilesChanged` / `shaderEffectsChanged`, NOT function bindings that
 * re-query C++ on every repaint (mirrors SurfaceShaderPage.qml's `_effects`
 * latch and AnimationEventCard's refreshFromTree).
 */
SettingsFlickable {
    id: page

    readonly property var bridge: settingsController.decorationPage
    // The baseline is addressed with the empty surface path.
    readonly property string surfacePath: ""

    // ── Reactive model state ─────────────────────────────────────────────
    property var _effects: []
    property var _chain: []
    property var _params: ({})
    // The fully-resolved baseline (defaults filled in) drives the border
    // controls so they always show a concrete value.
    property var _resolved: ({})

    function refresh() {
        page._effects = page.bridge ? page.bridge.availableShaderEffects() : [];
        page._chain = page.bridge ? page.bridge.chainAt(page.surfacePath) : [];
        var raw = page.bridge ? page.bridge.rawProfile(page.surfacePath) : ({});
        page._params = (raw && raw.parameters) ? raw.parameters : ({});
        page._resolved = page.bridge ? page.bridge.resolvedProfile(page.surfacePath) : ({});
    }

    Component.onCompleted: page.refresh()

    Connections {
        target: page.bridge
        function onProfilesChanged() {
            page.refresh();
        }
        function onShaderEffectsChanged() {
            page.refresh();
        }
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("These defaults apply to every decorated surface unless a sub-page (Windows, OSDs, Popups) defines its own override.")
        }

        // ── Default decoration chain ─────────────────────────────────────
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Default decoration chain")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: i18n("Ordered list of decoration shader packs applied to every surface. Each pack draws on top of the previous one.")
                    wrapMode: Text.WordWrap
                    opacity: 0.8
                }

                ChainEditor {
                    Layout.fillWidth: true
                    availableShaders: page._effects
                    chain: page._chain
                    packParameters: page._params
                    onChainChangeRequested: function (newChain) {
                        if (page.bridge)
                            page.bridge.setChain(page.surfacePath, newChain);
                    }
                    onParamChangeRequested: function (packId, paramId, value) {
                        if (page.bridge)
                            page.bridge.setChainParam(page.surfacePath, packId, paramId, value);
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: !page._effects || page._effects.length === 0
                    text: i18n("No decoration shader packs are installed.")
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                }
            }
        }

        // ── Border & titlebar ────────────────────────────────────────────
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Border and titlebar")
            showToggle: true
            toggleChecked: page._resolved && page._resolved.showBorder === true
            collapsible: true
            onToggleClicked: function (checked) {
                if (page.bridge)
                    page.bridge.setBorderField(page.surfacePath, "showBorder", checked);
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Use system accent color")
                    description: i18n("Derive border colors from your system color scheme")

                    SettingsSwitch {
                        id: useSystemColorsSwitch

                        checked: page._resolved && page._resolved.useSystemColors === true
                        accessibleName: i18n("Use system accent color")
                        onToggled: function (newValue) {
                            if (page.bridge)
                                page.bridge.setBorderField(page.surfacePath, "useSystemColors", newValue);
                        }
                    }
                }

                SettingsSeparator {
                    visible: !useSystemColorsSwitch.checked
                }

                SettingsRow {
                    visible: !useSystemColorsSwitch.checked
                    title: i18n("Active border color")
                    description: i18n("Border color for the focused window")

                    ColorSwatchRow {
                        id: activeSwatch

                        color: (page._resolved && page._resolved.activeColor) ? page._resolved.activeColor : "transparent"
                        onClicked: {
                            activeColorDialog.selectedColor = activeSwatch.color;
                            activeColorDialog.open();
                        }
                    }
                }

                SettingsSeparator {
                    visible: !useSystemColorsSwitch.checked
                }

                SettingsRow {
                    visible: !useSystemColorsSwitch.checked
                    title: i18n("Inactive border color")
                    description: i18n("Border color for unfocused windows")

                    ColorSwatchRow {
                        id: inactiveSwatch

                        color: (page._resolved && page._resolved.inactiveColor) ? page._resolved.inactiveColor : "transparent"
                        onClicked: {
                            inactiveColorDialog.selectedColor = inactiveSwatch.color;
                            inactiveColorDialog.open();
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Border width")
                    description: i18n("Thickness of colored borders around decorated windows")

                    SettingsSpinBox {
                        from: 0
                        to: 32
                        value: (page._resolved && page._resolved.borderWidth !== undefined) ? page._resolved.borderWidth : 0
                        onValueModified: value => {
                            if (page.bridge)
                                page.bridge.setBorderField(page.surfacePath, "borderWidth", value);
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Corner radius")
                    description: i18n("Roundness of border corners (0 for square)")

                    SettingsSpinBox {
                        from: 0
                        to: 32
                        value: (page._resolved && page._resolved.borderRadius !== undefined) ? page._resolved.borderRadius : 0
                        onValueModified: value => {
                            if (page.bridge)
                                page.bridge.setBorderField(page.surfacePath, "borderRadius", value);
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Hide title bars")
                    description: i18n("Remove window title bars while decorated, restored when floating")

                    SettingsSwitch {
                        checked: page._resolved && page._resolved.hideTitlebar === true
                        accessibleName: i18n("Hide title bars on decorated windows")
                        onToggled: function (newValue) {
                            if (page.bridge)
                                page.bridge.setBorderField(page.surfacePath, "hideTitlebar", newValue);
                        }
                    }
                }
            }
        }
    }

    // ── Color dialogs ────────────────────────────────────────────────────
    ColorDialog {
        id: activeColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Active Border Color")
        onAccepted: {
            if (page.bridge)
                page.bridge.setBorderField(page.surfacePath, "activeColor", selectedColor);
        }
    }

    ColorDialog {
        id: inactiveColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Inactive Border Color")
        onAccepted: {
            if (page.bridge)
                page.bridge.setBorderField(page.surfacePath, "inactiveColor", selectedColor);
        }
    }
}
