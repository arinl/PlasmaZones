// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Decoration → Surfaces → Windows.
 *
 * Per-surface override cards for the three window placement states
 * (window.tiled / window.snapped / window.floating). Each card inherits
 * from the global Decoration → General baseline unless it defines its own
 * override.
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
            text: i18n("Decoration overrides for each window placement state. Without an override, a state inherits the global decoration defaults.")
        }

        Repeater {
            model: ["window.tiled", "window.snapped", "window.floating"]

            delegate: DecorationSurfaceCard {
                required property string modelData

                Layout.fillWidth: true
                surfacePath: modelData
                collapsible: true
            }
        }
    }
}
