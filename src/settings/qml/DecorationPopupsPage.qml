// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Decoration → Surfaces → Popups. Viewport-virtualized by
// DecorationSurfaceCardList; thin model declaration like AnimationsWindowsPage.
//
// An "All popups" parent-node card (path "popup") sits above the transient
// popup cards (snap assist, zone selector, layout picker); an override on it
// cascades to every popup via the DecorationProfileTree walk-up. The runtime
// zone overlay is its own root (not under "popup"), so it stays a standalone
// leaf. Without an override, each surface inherits its parents up to the
// global defaults. Popups have no title bar, so none expose the toggle.
DecorationSurfaceCardList {
    Accessible.name: i18n("Popup decoration surfaces")
    headerText: i18n("Decoration overrides for the transient popups and the zone overlay. \"All popups\" applies to every popup; each can override it. Without an override, each inherits its parents up to the global defaults.")
    surfaceModel: [
        {
            "surfacePath": "popup",
            "isParentNode": true,
            "showTitlebarToggle": false
        },
        {
            "surfacePath": "popup.snapAssist",
            "isParentNode": false,
            "showTitlebarToggle": false
        },
        {
            "surfacePath": "popup.zoneSelector",
            "isParentNode": false,
            "showTitlebarToggle": false
        },
        {
            "surfacePath": "popup.layoutPicker",
            "isParentNode": false,
            "showTitlebarToggle": false
        },
        {
            "surfacePath": "overlay",
            "isParentNode": false,
            "showTitlebarToggle": false
        }
    ]
}
