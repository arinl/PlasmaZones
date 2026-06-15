// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Per-surface decoration override card.
 *
 * One card per surface path. Leaf paths (window.tiled, osd,
 * popup.snapAssist, …) are concrete surfaces; CATEGORY paths (window,
 * popup) are parent nodes whose override cascades to every descendant leaf
 * unless that leaf has its own override. Mirrors AnimationEventCard: an
 * override toggle gates a per-surface override in the DecorationProfileTree.
 * When off, the card shows the RESOLVED chain (the walk-up result) read-only
 * with an "Inheriting from: …" breadcrumb (leaf) or a parent-node banner
 * (category). When on, it edits the DIRECT override at this path — a
 * ChainEditor (where border width / radius / colour are the "border" pack's
 * own inline parameters, NOT a separate appearance block) plus a single
 * "Hide title bar" toggle — and offers "Reset to inherited" (clearOverride).
 *
 * Reactive-latch pattern: imperative refresh from the controller on
 * `profilesChanged` / `shaderEffectsChanged`, NOT function bindings that
 * re-query C++ every repaint (mirrors AnimationEventCard.refreshFromTree).
 *
 * Required properties:
 *   - surfacePath: full path (e.g. "window.tiled" leaf, or "window" category)
 * Optional:
 *   - isParentNode: bool — flips the inheritance banner copy for a category
 *     node ("All windows" / "All popups").
 *   - showTitlebarToggle: bool — exposes the "Hide title bar" control. Title
 *     bars only make sense for WINDOWS, so only the window-subtree cards set
 *     this true; daemon surfaces (osd / popup / overlay) leave it false and
 *     never show the toggle.
 */
Item {
    id: root

    required property string surfacePath
    property bool collapsible: false
    property bool isParentNode: false
    property bool showTitlebarToggle: false

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
        // Title-bar state is only meaningful for window surfaces; daemon
        // surfaces omit it (they never expose the toggle).
        if (!root.showTitlebarToggle)
            return i18n("Packs: %1", packs);
        var titlebar = (root._resolved && root._resolved.hideTitlebar === true) ? i18n("title bar hidden") : i18n("title bar shown");
        return i18n("Packs: %1 · %2", packs, titlebar);
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
            // Parent-node category cards explain the cascade when ON; leaf
            // cards explain inheritance when OFF — same split as
            // AnimationEventCard.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                visible: root.isParentNode ? root._hasOverride : !root._hasOverride
                text: {
                    if (root.isParentNode)
                        return i18n("Settings here apply to all child surfaces unless individually overridden.");
                    return root._parentChainText.length > 0 ? i18n("Inheriting from: %1", root._parentChainText) : i18n("Using global defaults");
                }
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

                Label {
                    Layout.fillWidth: true
                    text: i18n("Each pack's settings (e.g. the Border pack's width, corner radius and colours) are shown beneath it. A surface shows a border only when the Border pack is in its chain.")
                    wrapMode: Text.WordWrap
                    opacity: 0.8
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
                    onParamsRandomizeRequested: function (packId, rolled) {
                        if (root.bridge)
                            root.bridge.setChainParams(root.surfacePath, packId, rolled);
                    }
                }

                // Title bar — WINDOW surfaces only. Daemon surfaces (osd /
                // popup / overlay) have no title bar concept, so the host leaves
                // showTitlebarToggle false and this whole section is omitted.
                SettingsSeparator {
                    visible: root.showTitlebarToggle
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.showTitlebarToggle
                    text: i18n("Title bar")
                    font.weight: Font.DemiBold
                }

                SettingsRow {
                    visible: root.showTitlebarToggle
                    title: i18n("Hide title bar")
                    description: i18n("Remove this surface's title bar")

                    SettingsSwitch {
                        checked: root._resolved && root._resolved.hideTitlebar === true
                        accessibleName: i18n("Hide title bar")
                        onToggled: function (newValue) {
                            if (root.bridge)
                                root.bridge.setHideTitlebar(root.surfacePath, newValue);
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
}
