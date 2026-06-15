// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import PlasmaZones 1.0
import QtQuick
import QtQuick.Window

/**
 * OSD surface-shader decoration host (Stage d).
 *
 * Renders the daemon's OSD card through a SURFACE shader pack (rounded corners +
 * border today) resolved by C++ from `DecorationProfileTree.resolve("osd")`.
 * The pack SAMPLES the card content as `uTexture0` and REPLACES it with a
 * clipped-and-bordered version, so the decoration must (a) capture the live card
 * into a texture, (b) suppress the card's own square-cornered direct draw, and
 * (c) re-render that texture through the shader over the same on-screen rect.
 *
 * ## Capture target — the PopupFrame `shaderAnchor`
 *
 * Every OSD card body wraps its visible frame in a `PopupFrame`, whose
 * `captureItem` is tagged `objectName: "shaderAnchor"` (the same item
 * `SurfaceAnimator` captures for show/hide transitions). It is larger than the
 * visible frame by a glow ring and publishes `shaderContentRect` — the visible
 * frame's rect inside the (glow-padded) capture item, in anchor-local logical
 * px. We capture the WHOLE anchor as `uTexture0` and feed `shaderContentRect`
 * as the frame geometry, so the surface contract rounds to the visible frame
 * corners (not the glow-padded bounds). This is the faithful mapping for
 * PopupFrame's padded capture; surface_uniforms.glsl's
 * uSurfaceFrameTopLeft/uSurfaceFrameSize exist precisely for this.
 *
 * ## Hide-source idiom — mirrors SurfaceAnimator verbatim
 *
 * `SurfaceAnimator::attachShaderToAnchor` (libs/phosphor-animation) snapshots
 * the anchor via a `QQuickShaderEffectSource` with `hideSource: true`, parked
 * far off-screen and `live: true`, then feeds THAT source to the shader's
 * `setSourceItem`. `hideSource: true` is what suppresses the anchor's own direct
 * draw so its square corners don't show under the rounded output; the off-screen
 * park keeps Qt's FBO render alive (visible:false / opacity:0 would cull it)
 * while the source's own composite node draws where the user can't see it. The
 * shader effect is a SIBLING of the anchor (never a parent/descendant — a
 * feedback loop) reading the snapshot, not the live layer. This component
 * reproduces that exact chain in QML.
 *
 * ## Lifecycle
 *
 * The host (PassiveOverlayShell.osdSlot) passes the loaded OSD content root as
 * `contentItem` and the C++-resolved decoration props. When
 * `decorationShaderSource` is empty (no "osd" pack resolves) the component is
 * inert: the capture/shader items don't activate and the card draws normally
 * with its native square-cornered chrome.
 */
Item {
    id: root

    /// Loaded OSD content root (osdLoader.item). Its nested PopupFrame exposes
    /// the `shaderAnchor` capture item we decorate.
    property Item contentItem: null

    /// C++-resolved surface pack, written by OverlayService::applyOsdDecoration:
    ///   • decorationShaderSource  — file:// url of the pack's effect.frag (""
    ///                               = no decoration; component stays inert).
    ///   • decorationParamPreamble — generated `#define p_<id> …` preamble.
    ///   • decorationShaderParams  — translated `customParamsN_*` / `customColorN`
    ///                               slot map (SurfaceShaderRegistry output).
    property url decorationShaderSource
    property string decorationParamPreamble: ""
    property var decorationShaderParams: ({})

    /// Logical→device scale for the decorated surface. The OSD shell tracks the
    /// active output's devicePixelRatio; Screen.devicePixelRatio is the live
    /// value for the window this item lives in.
    readonly property real surfaceScale: Screen.devicePixelRatio

    /// Whether any decoration is active. Gates the capture + shader items so an
    /// undecorated OSD pays nothing and draws its native card.
    readonly property bool decorationActive: decorationShaderSource.toString() !== "" && shaderAnchorItem !== null

    /// The PopupFrame capture item (objectName "shaderAnchor") inside the loaded
    /// content. Re-resolved whenever the content swaps (Loader re-instantiation
    /// on each show produces a fresh anchor — matches the per-show shaderAnchor
    /// the dismiss path forces via the mode="" unload).
    property Item shaderAnchorItem: null

    function _resolveAnchor() {
        shaderAnchorItem = contentItem ? _findByObjectName(contentItem, "shaderAnchor") : null;
    }

    // Depth-first search for the shaderAnchor by objectName. Mirrors the C++
    // findQmlItemByName the selector/snap-assist paths use to locate the same
    // item; QML has no built-in recursive findChild for visual items.
    function _findByObjectName(node, name) {
        if (!node)
            return null;
        var kids = node.children;
        for (var i = 0; i < kids.length; i++) {
            var child = kids[i];
            if (child.objectName === name)
                return child;
            var found = _findByObjectName(child, name);
            if (found)
                return found;
        }
        return null;
    }

    anchors.fill: parent
    // Re-resolve the anchor whenever the loaded content changes identity (mode
    // swap / Loader re-instantiation). onCompleted of the content is not
    // observable here, so bind on contentItem and let the bindings below settle
    // once the anchor's geometry is non-zero.
    onContentItemChanged: _resolveAnchor()
    Component.onCompleted: _resolveAnchor()

    // ── Snapshot of the card (hide-source) ───────────────────────────────────
    // QQuickShaderEffectSource registered as the QML element ShaderEffectSource.
    // hideSource:true suppresses the anchor's own direct draw; the off-screen
    // park keeps the FBO render alive without a second visible composite. live:
    // true re-captures each frame so a card whose content animates (e.g. badge
    // toast) stays current under the decoration.
    ShaderEffectSource {
        id: cardSnapshot

        // Park far off-screen — see the SurfaceAnimator rationale above. A
        // zero/negative SIZE would skip updatePaintNode and starve the shader's
        // uTexture0, so size tracks the anchor and only the POSITION is hidden.
        readonly property real offscreenCoord: -1000000

        sourceItem: root.decorationActive ? root.shaderAnchorItem : null
        live: true
        hideSource: root.decorationActive
        width: root.shaderAnchorItem ? root.shaderAnchorItem.width : 0
        height: root.shaderAnchorItem ? root.shaderAnchorItem.height : 0
        x: offscreenCoord
        y: offscreenCoord
        // MUST stay visible: SurfaceAnimator's rationale (surfaceanimator.cpp
        // ~640) is that visible:false (and opacity:0) suppress updatePaintNode
        // and therefore the FBO render — starving the shader's uTexture0. The
        // off-screen park above is what hides it; Qt keeps processing it there.
        // When no pack resolves, sourceItem is null + hideSource false, so this
        // captures nothing and the card draws itself normally.
        visible: true
    }

    // ── Surface shader pass ──────────────────────────────────────────────────
    // Sibling of the captured card (parented to this host, which is a sibling of
    // osdLoader inside osdSlot — never an ancestor of the anchor, so no feedback
    // loop). Positioned over the anchor's on-screen rect, mapped into this
    // host's coordinate space.
    SurfaceShaderItem {
        id: decoration

        // Anchor rect mapped into this host's coordinate space. The anchor lives
        // deep inside the loaded content; mapToItem walks the transform chain so
        // the decoration lands exactly over the card regardless of nesting.
        readonly property point anchorOrigin: (root.decorationActive && root.shaderAnchorItem) ? root.shaderAnchorItem.mapToItem(root, 0, 0) : Qt.point(0, 0)

        visible: root.decorationActive
        x: anchorOrigin.x
        y: anchorOrigin.y
        width: root.shaderAnchorItem ? root.shaderAnchorItem.width : 0
        height: root.shaderAnchorItem ? root.shaderAnchorItem.height : 0

        // The pack samples this snapshot as uTexture0 and REPLACES the card.
        sourceItem: cardSnapshot

        // Surface-state inputs (device px). The whole CAPTURE item is uTexture0;
        // the FRAME rect within it (shaderContentRect, anchor-local logical px)
        // scaled to device px is what the border rounds to — excluding the glow
        // ring PopupFrame's capture adds. surfaceFrameSize == surfaceSize only
        // if the capture had no padding; PopupFrame pads, so the real frame
        // rect is fed through.
        surfaceScale: root.surfaceScale
        // OSD is always shown for the active context — the focused colour params
        // are the intended look. A literal true is correct here.
        surfaceFocused: true
        surfaceSize: root.shaderAnchorItem ? Qt.size(root.shaderAnchorItem.width * root.surfaceScale, root.shaderAnchorItem.height * root.surfaceScale) : Qt.size(0, 0)
        surfaceFrameTopLeft: (root.shaderAnchorItem && root.shaderAnchorItem.shaderContentRect !== undefined) ? Qt.point(root.shaderAnchorItem.shaderContentRect.x * root.surfaceScale, root.shaderAnchorItem.shaderContentRect.y * root.surfaceScale) : Qt.point(0, 0)
        surfaceFrameSize: (root.shaderAnchorItem && root.shaderAnchorItem.shaderContentRect !== undefined) ? Qt.size(root.shaderAnchorItem.shaderContentRect.width * root.surfaceScale, root.shaderAnchorItem.shaderContentRect.height * root.surfaceScale) : Qt.size(0, 0)

        // Pack source + params. paramPreamble/shaderParams BEFORE shaderSource
        // is the load-trigger ordering the inherited ShaderEffect setters expect;
        // here they are bindings, so QML evaluates the value graph before the
        // first paint regardless of declaration order, but mirroring the
        // applyShaderInfoToWindow order keeps the intent explicit.
        paramPreamble: root.decorationParamPreamble
        shaderParams: root.decorationShaderParams
        shaderSource: root.decorationShaderSource
        // iTime is left at its default: the border pack is static (no iTime
        // reference survives the linker), so no per-frame driver is wired.
    }
}
