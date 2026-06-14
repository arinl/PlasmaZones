// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "types.h"
#include "window_query.h"

#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <scene/item.h>
#include <scene/windowitem.h>

#include <QPoint>
#include <QRectF>
#include <QSize>

namespace PlasmaZones {

// Render the window's active surface-layer stack into the transition's FBO chain
// and return the texture holding the final composited surface. The animation
// pass then samples this (as `uSurfaceLayer`) instead of the bare live
// `uTexture0`, so surface layers — the border / rounded corners today, and any
// future layer (tint, glow) — stay composited UNDER the animation for the whole
// transition rather than vanishing when the animation shader takes the draw slot.
//
// Layer 0 is the border, rendered through the existing border shader via
// OffscreenData (the same path captureOldWindowSnapshot uses for the morph
// snapshot): KWin renders the raw window into its internal FBO, then
// OffscreenData::paint blits it through the border shader into our chain
// texture. This reuses the border shader and its MVP vertex path verbatim, so
// the result shares uTexture0's bottom-origin layout and premultiplied alpha —
// `surfaceColor()` samples it with the same Y-flip and opacity dim.
//
// Additional layers (none today) chain as passthrough-quad FBO→FBO blits over
// the ping-pong pair; that rung is the documented extension point for future
// surface effects.
KWin::GLTexture* PlasmaZonesEffect::renderSurfaceChain(ShaderTransition& transition, KWin::EffectWindow* w, qreal scale)
{
    if (!w || !transition.cached) {
        return nullptr;
    }

    // Layer 0: border. Mirror reconcileBorderShader's "wants border" gate — a
    // window with no valid border has no active surface layers, so the caller
    // animates the bare uTexture0 with zero FBO overhead.
    const QString windowId = getWindowId(w);
    const auto bit = m_windowBorders.constFind(windowId);
    const bool wantsBorder = bit != m_windowBorders.constEnd() && bit->width > 0 && bit->color.isValid();
    if (!wantsBorder) {
        return nullptr;
    }
    // Compile-on-first-use; a latched compile failure means no surface layer.
    KWin::GLShader* const border = borderShader();
    if (!border) {
        return nullptr;
    }

    // Size the chain to the window's expanded geometry × screen scale, exactly
    // as captureOldWindowSnapshot sizes the morph snapshot — the redirected FBO
    // covers frame + decoration + shadow. The defensive cap keeps a pathological
    // window within GL texture limits (sampled by normalised uv, so a reduced
    // capture scale only costs resolution, never distortion).
    const QRectF logicalGeometry = w->expandedGeometry();
    qreal captureScale = scale;
    constexpr qreal kMaxSurfaceDim = 8192.0;
    const qreal longestPx = qMax(logicalGeometry.width(), logicalGeometry.height()) * captureScale;
    if (longestPx > kMaxSurfaceDim) {
        captureScale *= kMaxSurfaceDim / longestPx;
    }
    const QSize textureSize = (logicalGeometry.size() * captureScale).toSize();
    if (textureSize.isEmpty()) {
        return nullptr;
    }

    // Reuse the chain textures across frames; reallocate only when the window's
    // expanded size × scale changes. Slot 0 holds layer 0's output (and the
    // final surface for the common single-layer border-only case).
    if (transition.surfaceLayerChainSize != textureSize || !transition.surfaceLayerChain[0]) {
        transition.surfaceLayerChain[0] = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
        // Slot 1 is allocated lazily by the passthrough rung when a second layer
        // is active; drop any stale ping-pong partner on resize.
        transition.surfaceLayerChain[1].reset();
        transition.surfaceLayerChainSize = textureSize;
        if (!transition.surfaceLayerChain[0]) {
            transition.surfaceLayerChainSize = QSize();
            return nullptr;
        }
        transition.surfaceLayerChain[0]->setFilter(GL_LINEAR);
        transition.surfaceLayerChain[0]->setWrapMode(GL_CLAMP_TO_EDGE);
    }

    KWin::GLTexture* const layer0 = transition.surfaceLayerChain[0].get();
    KWin::GLFramebuffer fbo(layer0);
    if (!fbo.valid()) {
        return nullptr;
    }

    // Temporarily swap the redirect's bound shader from the animation shader to
    // the border shader so OffscreenData::paint blits the captured window through
    // the border. Restore the animation shader afterwards — paintWindow's
    // transition branch re-binds it and sets the animation uniforms for the
    // on-screen draw. setShader is idempotent and the redirect is already live
    // (beginShaderTransition redirected the window), so no redirect() here.
    KWin::GLShader* const animShader = transition.cached->shader.get();
    setShader(w, border);

    // Re-entrancy guard: like captureOldWindowSnapshot, suppress the passive
    // border bind in the drawWindow override (we bind it ourselves below) and
    // keep apply()'s surface-extent quad deform off so the window is captured
    // 1:1 into the FBO rather than as the in-flight animation quad.
    m_capturingSnapshot = true;
    {
        KWin::RenderTarget renderTarget(&fbo);
        KWin::RenderViewport viewport(logicalGeometry, captureScale, renderTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        KWin::ItemEffect keepRenderable(w->windowItem());
        KWin::WindowPaintData captureData;
        captureData.setOpacity(1.0);
        const int captureMask = PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT;
        // Bind the border shader and push the window's border uniforms onto it.
        // OffscreenData::paint re-binds the SAME program for its blit, so the
        // uniforms persist into the draw — identical to the passive drawWindow
        // path. The binder is held across effects->drawWindow.
        KWin::ShaderBinder binder(border);
        pushBorderUniforms(w, *bit, captureScale);
        // Route through effects->drawWindow (not OffscreenEffect::drawWindow) so
        // KWin's draw-chain iterator is advanced past us before OffscreenData's
        // internal capture re-enters the chain — same rationale as the on-screen
        // and snapshot paths.
        KWin::effects->drawWindow(renderTarget, viewport, w, captureMask, KWin::Region::infinite(), captureData);
        KWin::GLFramebuffer::popFramebuffer();
    }
    m_capturingSnapshot = false;

    setShader(w, animShader);

    // Additional passthrough layers (tint, glow, ...) would chain here, blitting
    // the current top slot into the other ping-pong slot through each layer's
    // shader and returning the final slot. None are registered today, so layer
    // 0's output is the final surface.
    return layer0;
}

} // namespace PlasmaZones
