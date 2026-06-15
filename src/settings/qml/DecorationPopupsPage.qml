// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Decoration → Surfaces → Popups.
 *
 * Per-surface override cards for the transient popups (snap assist, zone
 * selector, layout picker) and the runtime zone overlay. Each inherits the
 * global Decoration → General baseline unless it defines its own override.
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
            text: i18n("Decoration overrides for the transient popups and the zone overlay. Without an override, each inherits the global decoration defaults.")
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
