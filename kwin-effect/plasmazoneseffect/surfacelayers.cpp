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
#include <opengl/glvertexbuffer.h>
#include <scene/item.h>
#include <scene/windowitem.h>

#include <PhosphorSurface/SurfaceShaderContract.h>

#include <QPoint>
#include <QRectF>
#include <QSize>
#include <QVector2D>
#include <QVector4D>

#include <array>
#include <epoxy/gl.h>

namespace PlasmaZones {

namespace {

/// Draw a fullscreen unit quad (NDC -1..1, texcoord 0..1) through the
/// already-bound shader as a triangle strip. texcoord [0,1] maps to the bound
/// FBO directly (bottom-origin), so a passthrough buffer reproduces uTexture0's
/// layout. Mirrors the helper in shader_transitions.cpp; both are file-local.
/// The caller owns the ShaderBinder.
void drawFullscreenQuad()
{
    const std::array<KWin::GLVertex2D, 4> verts = {{
        {QVector2D(-1.0f, -1.0f), QVector2D(0.0f, 0.0f)}, // bottom-left
        {QVector2D(1.0f, -1.0f), QVector2D(1.0f, 0.0f)}, // bottom-right
        {QVector2D(-1.0f, 1.0f), QVector2D(0.0f, 1.0f)}, // top-left
        {QVector2D(1.0f, 1.0f), QVector2D(1.0f, 1.0f)}, // top-right
    }};
    KWin::GLVertexBuffer* const vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(verts);
    vbo->render(GL_TRIANGLE_STRIP);
}

} // namespace

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

// Render the MULTIPASS surface pack's buffer passes for @p w into its per-window
// FBO chain (m_surfaceMultipass), so the IDLE drawWindow path can bind the
// buffer outputs as iChannel0..3 for the main pass. Single-pass packs (the
// border) have m_surfaceBufferPasses empty and never reach here — their cheap
// OffscreenData path stays untouched.
//
// ORIENTATION CONTRACT: surfaceTex and every bufferTex are bottom-origin GL FBOs
// — the SAME layout as KWin's redirected uTexture0. The fullscreen quad maps
// texcoord [0,1] to the FBO directly (v=0 at the bottom), and the buffer
// fragments sample uTexture0 / iChannelN through surface_uniforms.glsl's
// surfaceTexel() helper, which on the KWin branch samples uTexture0 with no
// Y-flip. So a passthrough buffer (fragColor = surfaceTexel(vTexCoord))
// reproduces the captured surface upright, and the main effect.frag samples the
// resulting iChannelN with the same convention it uses for uTexture0 — every
// stage stays in one consistent bottom-origin space.
//
// DURING A TRANSITION this is NOT called: a multipass pack degrades to
// single-pass while a window animates (renderSurfaceChain runs the border via
// the main shader with the iChannels unbound → sampled as 0). Feeding the buffer
// passes into the transition chain is a follow-up.
bool PlasmaZonesEffect::renderSurfaceBufferPasses(KWin::EffectWindow* w, qreal scale)
{
    if (!w || m_surfaceBufferPasses.empty()) {
        return false;
    }
    namespace SC = PhosphorSurfaceShaders::SurfaceShaderContract;

    // Size the targets to the window's expanded geometry × screen scale — the
    // redirected FBO covers frame + decoration + shadow, identically to
    // renderSurfaceChain / captureOldWindowSnapshot. The defensive cap keeps a
    // pathological window within GL texture limits (sampled by normalised uv, so
    // a reduced capture scale only costs resolution, never distortion).
    const QRectF logicalGeometry = w->expandedGeometry();
    qreal captureScale = scale;
    constexpr qreal kMaxSurfaceDim = 8192.0;
    const qreal longestPx = qMax(logicalGeometry.width(), logicalGeometry.height()) * captureScale;
    if (longestPx > kMaxSurfaceDim) {
        captureScale *= kMaxSurfaceDim / longestPx;
    }
    const QSize textureSize = (logicalGeometry.size() * captureScale).toSize();
    if (textureSize.isEmpty()) {
        return false;
    }

    // The pack's bufferScale downscales the buffer FBOs (the surface capture is
    // always full-resolution). Clamp matches SurfaceShaderEffect::fromJson.
    const PhosphorSurfaceShaders::SurfaceShaderEffect eff = m_surfaceShaderRegistry.effect(m_surfaceShaderId);
    const qreal bufferScale = qBound(PhosphorSurfaceShaders::SurfaceShaderEffect::kMinBufferScale, eff.bufferScale,
                                     PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferScale);
    QSize bufferSize(qMax(1, qRound(textureSize.width() * bufferScale)),
                     qMax(1, qRound(textureSize.height() * bufferScale)));

    // Get / (re)allocate the per-window targets. Reallocate the whole chain when
    // the full size changes — the buffer size is a fixed multiple of it.
    SurfaceMultipassState& state = m_surfaceMultipass[getWindowId(w)];
    const size_t passCount = m_surfaceBufferPasses.size();
    if (state.size != textureSize || !state.surfaceTex || state.bufferTex.size() != passCount) {
        state.surfaceTex = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
        if (!state.surfaceTex) {
            m_surfaceMultipass.erase(getWindowId(w));
            return false;
        }
        state.surfaceTex->setFilter(GL_LINEAR);
        state.surfaceTex->setWrapMode(GL_CLAMP_TO_EDGE);
        state.bufferTex.clear();
        state.bufferTex.reserve(passCount);
        for (size_t i = 0; i < passCount; ++i) {
            std::unique_ptr<KWin::GLTexture> bt = KWin::GLTexture::allocate(GL_RGBA8, bufferSize);
            if (!bt) {
                m_surfaceMultipass.erase(getWindowId(w));
                return false;
            }
            bt->setFilter(GL_LINEAR);
            bt->setWrapMode(GL_CLAMP_TO_EDGE);
            state.bufferTex.push_back(std::move(bt));
        }
        state.size = textureSize;
    }

    // ── Step 1: capture the RAW window surface into surfaceTex ───────────────
    // Exactly like captureOldWindowSnapshot: bypass our border shader so the
    // capture is the raw composited window (bottom-origin, same layout as
    // uTexture0), then restore the previously-bound shader. setShader(w,nullptr)
    // here, then restore — drawWindow re-binds the border shader for the main
    // blit after this returns.
    {
        KWin::GLFramebuffer fbo(state.surfaceTex.get());
        if (!fbo.valid()) {
            return false;
        }
        setShader(w, nullptr);
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
            // Route through effects->drawWindow (not OffscreenEffect::drawWindow)
            // so KWin's draw-chain iterator is advanced past us before
            // OffscreenData re-enters — same rationale as renderSurfaceChain and
            // captureOldWindowSnapshot. m_capturingSnapshot short-circuits the
            // passive border bind in our drawWindow override during this capture.
            KWin::effects->drawWindow(renderTarget, viewport, w, captureMask, KWin::Region::infinite(), captureData);
            KWin::GLFramebuffer::popFramebuffer();
        }
        m_capturingSnapshot = false;
        // The border shader is re-applied by drawWindow's own ShaderBinder after
        // this returns; restore the redirect's bound shader to it so the main
        // blit (OffscreenData::paint) runs the border program.
        setShader(w, m_borderShader.get());
    }

    // ── Step 2: run each buffer pass into its FBO ────────────────────────────
    // Pass i samples surfaceTex (GL_TEXTURE0) + every earlier bufferTex
    // (GL_TEXTURE1+j as iChannelN) and writes bufferTex[i]. We draw a fullscreen
    // quad with NO MVP (the quad is already NDC), restoring glActiveTexture to
    // GL_TEXTURE0 after each pass for hygiene.
    for (size_t i = 0; i < passCount; ++i) {
        const CompiledSurfaceBufferPass& pass = m_surfaceBufferPasses[i];
        KWin::GLTexture* const target = state.bufferTex[i].get();
        KWin::GLFramebuffer fbo(target);
        if (!fbo.valid()) {
            return false;
        }
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glViewport(0, 0, target->width(), target->height());
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        {
            KWin::ShaderBinder binder(pass.shader.get());

            // uTexture0 — the captured surface on unit 0.
            glActiveTexture(GL_TEXTURE0);
            state.surfaceTex->bind();
            if (pass.uTexture0Loc >= 0) {
                pass.shader->setUniform(pass.uTexture0Loc, 0);
            }

            // iChannel0..(i-1) — prior buffer outputs on units 1+j.
            for (size_t j = 0; j < i && j < 4; ++j) {
                glActiveTexture(GL_TEXTURE1 + static_cast<int>(j));
                state.bufferTex[j]->bind();
                if (pass.iChannelLoc[j] >= 0) {
                    pass.shader->setUniform(pass.iChannelLoc[j], 1 + static_cast<int>(j));
                }
                if (pass.iChannelResolutionLoc[j] >= 0) {
                    const QVector4D res(static_cast<float>(state.bufferTex[j]->width()),
                                        static_cast<float>(state.bufferTex[j]->height()), 0.0f, 0.0f);
                    pass.shader->setUniform(pass.iChannelResolutionLoc[j], res);
                }
            }

            // Pack-declared parameter values (reuse the main pass's resolved set).
            for (int slot = 0; slot < SC::kMaxCustomParams; ++slot) {
                if (pass.customParamsLoc[slot] >= 0) {
                    pass.shader->setUniform(pass.customParamsLoc[slot], m_surfaceCustomParamsValues[slot]);
                }
            }
            for (int slot = 0; slot < SC::kMaxCustomColors; ++slot) {
                if (pass.customColorsLoc[slot] >= 0) {
                    pass.shader->setUniform(pass.customColorsLoc[slot], m_surfaceCustomColorsValues[slot]);
                }
            }

            drawFullscreenQuad();
        }
        // Texture hygiene: unbind every channel unit we touched and restore
        // GL_TEXTURE0 as the active unit (mirrors paint_pipeline.cpp).
        for (size_t j = 0; j < i && j < 4; ++j) {
            glActiveTexture(GL_TEXTURE1 + static_cast<int>(j));
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        KWin::GLFramebuffer::popFramebuffer();
    }

    return true;
}

} // namespace PlasmaZones
