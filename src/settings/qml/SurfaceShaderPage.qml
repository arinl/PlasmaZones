// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief Decoration → Surface shader page (GLOBAL scope).
 *
 * Unlike the per-event animation-shader pages, surface decoration uses a
 * SINGLE pack for every decorated window. This page provides:
 *   - a pack picker ComboBox bound to
 *     `settingsController.surfaceShaderPage.availableShaderEffects()`,
 *     writing through `setShaderEffectId`,
 *   - a preview Image (the selected pack's `previewPath`),
 *   - a ShaderParameterEditor (shared component) editing the per-pack
 *     parameter override map via `setShaderParam` / `resetShaderParams`.
 *
 * Selection writes straight to Settings, so dirty / Apply / Discard are
 * handled by the global SettingsController machinery — this page carries no
 * staged state of its own (see SurfaceShaderPageController's class doc).
 */
SettingsFlickable {
    id: root

    // Live bridge to the surface controller.
    readonly property var bridge: settingsController.surfaceShaderPage

    // ── Reactive model state ─────────────────────────────────────────────
    // Rebuilt from the controller whenever the installed-pack set changes
    // (shaderEffectsChanged) or the global selection changes
    // (selectionChanged). Imperative latch rather than a function binding so
    // the ComboBox / editor don't re-query the C++ surface on every repaint.
    property var _effects: []
    property string _selectedId: ""
    property var _selectedEffect: ({})
    property var _currentParams: ({})

    function _selectedEffectFor(id) {
        for (var i = 0; i < root._effects.length; i++) {
            if (root._effects[i] && root._effects[i].id === id)
                return root._effects[i];
        }
        return {};
    }

    function _indexOfId(id) {
        for (var i = 0; i < root._effects.length; i++) {
            if (root._effects[i] && root._effects[i].id === id)
                return i;
        }
        return -1;
    }

    function refresh() {
        root._effects = root.bridge ? root.bridge.availableShaderEffects() : [];
        root._selectedId = root.bridge ? root.bridge.currentShaderEffectId() : "";
        root._selectedEffect = root._selectedEffectFor(root._selectedId);
        root._currentParams = root.bridge ? root.bridge.currentShaderParams() : ({});
        packCombo.currentIndex = root._indexOfId(root._selectedId);
    }

    Component.onCompleted: root.refresh()

    Connections {
        target: root.bridge
        function onShaderEffectsChanged() {
            root.refresh();
        }
        function onSelectionChanged() {
            root.refresh();
        }
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ── Pack picker ──────────────────────────────────────────────────
        SettingsCard {
            headerText: i18n("Surface shader")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    type: Kirigami.MessageType.Information
                    visible: true
                    text: i18n("Select one surface shader pack. It applies to the decoration of every snapped or tiled window.")
                }

                SettingsRow {
                    title: i18n("Shader pack")
                    description: root._selectedEffect && root._selectedEffect.description ? root._selectedEffect.description : ""

                    ComboBox {
                        id: packCombo

                        Accessible.name: i18n("Surface shader pack")
                        model: root._effects
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: root._indexOfId(root._selectedId)
                        onActivated: index => {
                            if (index >= 0 && index < root._effects.length && root.bridge)
                                root.bridge.setShaderEffectId(root._effects[index].id);
                        }
                    }
                }

                // Empty-state when no packs are installed.
                Label {
                    Layout.fillWidth: true
                    visible: !root._effects || root._effects.length === 0
                    text: i18n("No surface shader packs are installed.")
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                }
            }
        }

        // ── Preview ──────────────────────────────────────────────────────
        SettingsCard {
            headerText: i18n("Preview")
            visible: root._selectedEffect && root._selectedEffect.id
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Image {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 10
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    visible: status === Image.Ready
                    source: (root._selectedEffect && root._selectedEffect.previewPath) ? ("file://" + root._selectedEffect.previewPath) : ""
                }

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    visible: !root._selectedEffect || !root._selectedEffect.previewPath
                    text: i18n("This pack ships no preview image.")
                    opacity: 0.7
                }
            }
        }

        // ── Parameters ───────────────────────────────────────────────────
        SettingsCard {
            headerText: i18n("Parameters")
            visible: root._selectedEffect && root._selectedEffect.id

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: i18n("Reset to defaults")
                        icon.name: "edit-reset"
                        enabled: root._currentParams && Object.keys(root._currentParams).length > 0
                        onClicked: {
                            if (root.bridge)
                                root.bridge.resetShaderParams();
                        }
                    }
                }

                PZCommon.ShaderParameterEditor {
                    Layout.fillWidth: true

                    compact: true
                    enableGroups: true
                    // The persisted override flow has no live lock / randomize
                    // affordances — those are editor-preview concerns.
                    enableLocking: false
                    enableRandomize: false
                    enableImage: true
                    parameters: (root._selectedEffect && root._selectedEffect.parameters) ? root._selectedEffect.parameters : []
                    currentValues: root._currentParams
                    onValueChanged: function (id, value) {
                        if (root.bridge)
                            root.bridge.setShaderParam(id, value);
                    }
                }
            }
        }

        // ── User shader directory ────────────────────────────────────────
        SettingsCard {
            headerText: i18n("Custom shaders")
            collapsible: true
            collapsed: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: i18n("Drop your own surface shader packs into the user shader directory. Each pack is a folder containing a metadata.json.")
                    wrapMode: Text.WordWrap
                    opacity: 0.8
                }

                Button {
                    text: i18n("Open user shader folder")
                    icon.name: "folder-open"
                    onClicked: {
                        if (root.bridge)
                            root.bridge.openUserShaderDirectory();
                    }
                }
            }
        }
    }
}
