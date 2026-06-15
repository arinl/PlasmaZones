// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Per-surface decoration override card.
 *
 * One card per leaf surface path (window.tiled, osd, popup.snapAssist, …).
 * Mirrors AnimationEventCard: an override toggle gates a per-surface
 * override in the DecorationProfileTree. When off, the card shows the
 * RESOLVED chain + border (the walk-up result) read-only, with an
 * "Inheriting from: …" breadcrumb. When on, it edits the DIRECT override at
 * this path — a ChainEditor plus border/titlebar field overrides — and
 * offers "Reset to inherited" (clearOverride).
 *
 * Reactive-latch pattern: imperative refresh from the controller on
 * `profilesChanged` / `shaderEffectsChanged`, NOT function bindings that
 * re-query C++ every repaint (mirrors AnimationEventCard.refreshFromTree).
 *
 * Required properties:
 *   - surfacePath: full leaf path (e.g. "window.tiled")
 */
Item {
    id: root

    required property string surfacePath
    property bool collapsible: false

    readonly property var bridge: settingsController.decorationPage

    // ── Reactive model state ─────────────────────────────────────────────
    property var _effects: []
    property bool _hasOverride: false
    // Effective (resolved) values for the read-only / preview view.
    property var _resolved: ({})
    // Direct-override sparse map: which fields are engaged AT this path.
    property var _raw: ({})
    // Chain shown by the editor — the direct chain when overriding, else the
    // resolved chain (so the user previews "what they'd start from").
    property var _chain: []
    property var _params: ({})
    property string _label: ""
    property string _parentChainText: ""

    function _computeParentChainText() {
        if (!root.bridge)
            return "";
        var chain = root.bridge.parentChain(root.surfacePath);
        // Drop self (chain[0]); show ancestors then the global baseline.
        var rest = (chain.length > 1) ? chain.slice(1) : [];
        var labels = [];
        for (var i = 0; i < rest.length; i++)
            labels.push(root.bridge.surfaceLabel(rest[i]));
        labels.push(i18n("Global"));
        return labels.join(" ← ");
    }

    function refresh() {
        if (!root.bridge)
            return;
        root._effects = root.bridge.availableShaderEffects();
        root._label = root.bridge.surfaceLabel(root.surfacePath);
        root._hasOverride = root.bridge.hasOverride(root.surfacePath);
        root._resolved = root.bridge.resolvedProfile(root.surfacePath);
        root._raw = root.bridge.rawProfile(root.surfacePath);
        root._chain = root.bridge.chainAt(root.surfacePath);
        root._params = (root._raw && root._raw.parameters) ? root._raw.parameters : ({});
        root._parentChainText = root._computeParentChainText();
    }

    // Engage a per-surface override: seed the chain with the currently
    // resolved chain so the override starts visibly equal to what was
    // inherited, then the user diverges from there.
    function _engageOverride() {
        if (root.bridge)
            root.bridge.setChain(root.surfacePath, root.bridge.chainAt(root.surfacePath));
    }

    function _resolvedSummary() {
        var c = root._resolved && root._resolved.chain ? root._resolved.chain : [];
        var packs = c.length > 0 ? root._packNames(c).join(", ") : i18n("None");
        var bw = (root._resolved && root._resolved.borderWidth !== undefined) ? root._resolved.borderWidth : 0;
        var show = root._resolved && root._resolved.showBorder === true;
        var border = show ? i18n("Border %1 px", bw) : i18n("No border");
        return i18n("Packs: %1 · %2", packs, border);
    }

    function _packNames(ids) {
        var out = [];
        for (var i = 0; i < ids.length; i++) {
            var found = ids[i];
            for (var j = 0; j < root._effects.length; j++) {
                if (root._effects[j] && root._effects[j].id === ids[i]) {
                    found = root._effects[j].name;
                    break;
                }
            }
            out.push(found);
        }
        return out;
    }

    implicitHeight: card.implicitHeight
    Layout.fillWidth: true
    Component.onCompleted: root.refresh()

    Connections {
        target: root.bridge
        function onProfilesChanged() {
            root.refresh();
        }
        function onShaderEffectsChanged() {
            root.refresh();
        }
    }

    SettingsCard {
        id: card

        anchors.fill: parent
        headerText: root._label
        showToggle: true
        toggleChecked: root._hasOverride
        collapsible: root.collapsible
        onToggleClicked: function (checked) {
            if (checked)
                root._engageOverride();
            else if (root.bridge)
                root.bridge.clearOverride(root.surfacePath);
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            // ── Inheritance summary (override off) ────────────────────────
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                visible: !root._hasOverride
                text: root._parentChainText.length > 0 ? i18n("Inheriting from: %1", root._parentChainText) : i18n("Using global defaults")
            }

            Label {
                Layout.fillWidth: true
                visible: !root._hasOverride
                text: i18n("Current: %1", root._resolvedSummary())
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
                wrapMode: Text.WordWrap
            }

            // ── Override editor (override on) ─────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                visible: root._hasOverride
                spacing: Kirigami.Units.largeSpacing

                Label {
                    Layout.fillWidth: true
                    text: i18n("Decoration chain")
                    font.weight: Font.DemiBold
                }

                ChainEditor {
                    Layout.fillWidth: true
                    availableShaders: root._effects
                    chain: root._chain
                    packParameters: root._params
                    onChainChangeRequested: function (newChain) {
                        if (root.bridge)
                            root.bridge.setChain(root.surfacePath, newChain);
                    }
                    onParamChangeRequested: function (packId, paramId, value) {
                        if (root.bridge)
                            root.bridge.setChainParam(root.surfacePath, packId, paramId, value);
                    }
                }

                SettingsSeparator {}

                Label {
                    Layout.fillWidth: true
                    text: i18n("Border and titlebar")
                    font.weight: Font.DemiBold
                }

                SettingsRow {
                    title: i18n("Show border")
                    description: i18n("Draw a border around this surface")

                    SettingsSwitch {
                        checked: root._resolved && root._resolved.showBorder === true
                        accessibleName: i18n("Show border")
                        onToggled: function (newValue) {
                            if (root.bridge)
                                root.bridge.setBorderField(root.surfacePath, "showBorder", newValue);
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Use system accent color")
                    description: i18n("Derive border colors from your system color scheme")

                    SettingsSwitch {
                        id: useSystemColorsSwitch

                        checked: root._resolved && root._resolved.useSystemColors === true
                        accessibleName: i18n("Use system accent color")
                        onToggled: function (newValue) {
                            if (root.bridge)
                                root.bridge.setBorderField(root.surfacePath, "useSystemColors", newValue);
                        }
                    }
                }

                SettingsRow {
                    visible: !useSystemColorsSwitch.checked
                    title: i18n("Active border color")
                    description: i18n("Border color for the focused surface")

                    ColorSwatchRow {
                        id: activeSwatch

                        color: (root._resolved && root._resolved.activeColor) ? root._resolved.activeColor : "transparent"
                        onClicked: {
                            activeColorDialog.selectedColor = activeSwatch.color;
                            activeColorDialog.open();
                        }
                    }
                }

                SettingsRow {
                    visible: !useSystemColorsSwitch.checked
                    title: i18n("Inactive border color")
                    description: i18n("Border color for the unfocused surface")

                    ColorSwatchRow {
                        id: inactiveSwatch

                        color: (root._resolved && root._resolved.inactiveColor) ? root._resolved.inactiveColor : "transparent"
                        onClicked: {
                            inactiveColorDialog.selectedColor = inactiveSwatch.color;
                            inactiveColorDialog.open();
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Border width")
                    description: i18n("Thickness of the colored border")

                    SettingsSpinBox {
                        from: 0
                        to: 32
                        value: (root._resolved && root._resolved.borderWidth !== undefined) ? root._resolved.borderWidth : 0
                        onValueModified: value => {
                            if (root.bridge)
                                root.bridge.setBorderField(root.surfacePath, "borderWidth", value);
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Corner radius")
                    description: i18n("Roundness of border corners (0 for square)")

                    SettingsSpinBox {
                        from: 0
                        to: 32
                        value: (root._resolved && root._resolved.borderRadius !== undefined) ? root._resolved.borderRadius : 0
                        onValueModified: value => {
                            if (root.bridge)
                                root.bridge.setBorderField(root.surfacePath, "borderRadius", value);
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Hide title bar")
                    description: i18n("Remove this surface's title bar")

                    SettingsSwitch {
                        checked: root._resolved && root._resolved.hideTitlebar === true
                        accessibleName: i18n("Hide title bar")
                        onToggled: function (newValue) {
                            if (root.bridge)
                                root.bridge.setBorderField(root.surfacePath, "hideTitlebar", newValue);
                        }
                    }
                }

                SettingsSeparator {}

                RowLayout {
                    Layout.fillWidth: true

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: i18n("Reset to inherited")
                        icon.name: "edit-reset"
                        onClicked: {
                            if (root.bridge)
                                root.bridge.clearOverride(root.surfacePath);
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
            if (root.bridge)
                root.bridge.setBorderField(root.surfacePath, "activeColor", selectedColor);
        }
    }

    ColorDialog {
        id: inactiveColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Inactive Border Color")
        onAccepted: {
            if (root.bridge)
                root.bridge.setBorderField(root.surfacePath, "inactiveColor", selectedColor);
        }
    }
}
