// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Decoration → Surfaces → Popups.
 *
 * An "All popups" category card (path "popup") sits above the transient
 * popup cards (snap assist, zone selector, layout picker); an override on it
 * cascades to every popup via the DecorationProfileTree walk-up. The runtime
 * zone overlay is its own root (not under "popup"), so it stays a standalone
 * leaf card. Without an override, each surface inherits its parents up to the
 * global defaults. Mirrors AnimationsWindowsPage's parent-node + per-leaf
 * layout.
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
            text: i18n("Decoration overrides for the transient popups and the zone overlay. \"All popups\" applies to every popup; each can override it. Without an override, each inherits its parents up to the global defaults.")
        }

        // ── "All popups" parent-node card (path "popup") ─────────────────
        DecorationSurfaceCard {
            Layout.fillWidth: true
            surfacePath: "popup"
            isParentNode: true
            collapsible: true
        }

        Repeater {
            model: ["popup.snapAssist", "popup.zoneSelector", "popup.layoutPicker", "overlay"]

            delegate: DecorationSurfaceCard {
                required property string modelData

                Layout.fillWidth: true
                surfacePath: modelData
                collapsible: true
            }
        }
    }
}
