// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Decoration → Surfaces → OSDs. Viewport-virtualized by
// DecorationSurfaceCardList; thin model declaration like the animation
// sub-pages. The OSD surface inherits the global Decoration → General
// baseline unless it defines its own override. OSDs have no title bar.
DecorationSurfaceCardList {
    Accessible.name: i18n("OSD decoration surface")
    headerText: i18n("Decoration override for the on-screen display. Without an override, it inherits the global decoration defaults.")
    surfaceModel: [
        {
            "surfacePath": "osd",
            "isParentNode": false,
            "showTitlebarToggle": false
        }
    ]
}
