// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Decoration → Surfaces → Windows.
 *
 * An "All windows" category card (path "window") sits above the three
 * window placement-state cards (window.tiled / window.snapped /
 * window.floating). Setting an override on "All windows" cascades to every
 * placement state via the DecorationProfileTree walk-up; each placement
 * state inherits global → "All windows" unless it defines its own override.
 * Mirrors AnimationsWindowsPage's parent-node + per-leaf layout.
 */
SettingsFlickable {
    id: page

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Decoration overrides for windows. \"All windows\" applies to every placement state; each state can override it. Without an override, a state inherits its parents up to the global defaults.")
        }

        // ── "All windows" parent-node card (path "window") ───────────────
        DecorationSurfaceCard {
            Layout.fillWidth: true
            surfacePath: "window"
            isParentNode: true
            collapsible: true
            // Title bars are a window concept — exposed only on window cards.
            showTitlebarToggle: true
        }

        Repeater {
            model: ["window.tiled", "window.snapped", "window.floating"]

            delegate: DecorationSurfaceCard {
                required property string modelData

                Layout.fillWidth: true
                surfacePath: modelData
                collapsible: true
                showTitlebarToggle: true
            }
        }
    }
}
