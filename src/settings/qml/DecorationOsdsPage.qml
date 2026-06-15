// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Decoration → Surfaces → OSDs.
 *
 * Per-surface override card for the OSD (on-screen display) surface. It
 * inherits the global Decoration → General baseline unless it defines its
 * own override.
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
            text: i18n("Decoration override for the on-screen display. Without an override, it inherits the global decoration defaults.")
        }

        DecorationSurfaceCard {
            Layout.fillWidth: true
            surfacePath: "osd"
        }
    }
}
