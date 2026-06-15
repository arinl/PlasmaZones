// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Decoration → Surfaces → Windows. The card list is viewport-virtualized by
// DecorationSurfaceCardList (only visible DecorationSurfaceCards build) — see
// that component for the rationale. Mirrors AnimationsWindowsPage's thin
// model-declaration shape.
//
// An "All windows" parent-node card (path "window") sits above the three
// placement-state cards (window.tiled / window.snapped / window.floating);
// an override on it cascades to every state via the DecorationProfileTree
// walk-up, and each state inherits global → "All windows" unless it defines
// its own override. Title bars are a window concept, so every window card
// exposes the hide-title-bar toggle.
DecorationSurfaceCardList {
    Accessible.name: i18n("Window decoration surfaces")
    headerText: i18n("Decoration overrides for windows. \"All windows\" applies to every placement state; each state can override it. Without an override, a state inherits its parents up to the global defaults.")
    surfaceModel: [
        {
            "surfacePath": "window",
            "isParentNode": true,
            "showTitlebarToggle": true
        },
        {
            "surfacePath": "window.tiled",
            "isParentNode": false,
            "showTitlebarToggle": true
        },
        {
            "surfacePath": "window.snapped",
            "isParentNode": false,
            "showTitlebarToggle": true
        },
        {
            "surfacePath": "window.floating",
            "isParentNode": false,
            "showTitlebarToggle": true
        }
    ]
}
