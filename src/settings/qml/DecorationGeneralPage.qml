// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Decoration → General — the global baseline defaults (path "").
 *
 * This is the "base curve" analogue of AnimationsGeneralPage: every
 * per-surface card (Windows / OSDs / Popups) inherits from here unless it
 * defines its own override. It edits the DecorationProfileTree baseline via
 * `settingsController.decorationPage` with the empty surface path "":
 *   - a decoration-pack chain (ChainEditor). Border width / corner radius /
 *     colour are the "border" pack's OWN parameters, edited inline by
 *     expanding the pack in the chain (same as any other pack's params).
 *     There is no separate "border appearance" block: a surface shows a
 *     border iff "border" is in its chain.
 *
 * Note: there is NO hide-title-bar control here. Title bars are a WINDOW
 * concept, not a global-baseline one (daemon surfaces have none), so that
 * toggle lives only on the Windows surface cards (DecorationWindowsPage).
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

    function refresh() {
        page._effects = page.bridge ? page.bridge.availableShaderEffects() : [];
        page._chain = page.bridge ? page.bridge.chainAt(page.surfacePath) : [];
        var raw = page.bridge ? page.bridge.rawProfile(page.surfacePath) : ({});
        page._params = (raw && raw.parameters) ? raw.parameters : ({});
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
                    text: i18n("Ordered list of decoration shader packs applied to every surface. Each pack draws on top of the previous one. Each pack's settings (for example, the Border pack's width, corner radius and colours) are shown beneath it.")
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
                    onParamsRandomizeRequested: function (packId, rolled) {
                        if (page.bridge)
                            page.bridge.setChainParams(page.surfacePath, packId, rolled);
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
    }
}
