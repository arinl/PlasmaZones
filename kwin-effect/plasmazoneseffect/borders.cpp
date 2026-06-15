// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <epoxy/gl.h>

#include <PhosphorAnimation/AnimationShaderContract.h>

#include "../autotilehandler.h"
#include "../snaphandler.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorCompositor/AutotileState.h>
#include <PhosphorCompositor/DecorationDefaults.h>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QByteArray>
#include <QColor>
#include <QVariantMap>
#include <QVector2D>
#include <QVector4D>

#include <optional>

namespace PlasmaZones {

void PlasmaZonesEffect::setupDecorationManager()
{
    // The drain-time veto is the authoritative re-check for deferred
    // title-bar restores: a vetoed restore stays QUEUED (the manager
    // re-arms its fallback timer and bounds the retries), so the veto must
    // hold ONLY while a re-acquire is genuinely expected — the window's
    // screen re-entered autotile mid-drain, the mode's hide-title-bars is
    // still on, and the window is not floating (a retile never re-acquires
    // floated windows). Without the latter two conditions, a hide-toggle-off
    // drain or a floated window's restore would be vetoed until the
    // manager's retry bound overrides it.
    m_decorationManager->setRestoreVeto([this](const QString& windowId) {
        KWin::EffectWindow* w = findWindowById(windowId);
        // Exact-id re-check: findWindowById's appId fuzzy fallback can
        // resolve a same-app SIBLING when the exact id misses, and a
        // sibling's screen/floating state must never decide the veto for
        // the window the queue entry tracks (same hazard guard as
        // SnapHandler::markWindowSnapped and the manager's own
        // resolveExact for physical toggles).
        if (!w || getWindowId(w) != windowId || !m_autotileHandler->isAutotileScreen(getWindowScreenId(w))) {
            return false;
        }
        // Re-source the hide-titlebar decision from the per-window resolved
        // decoration tree (the authoritative source now that hide-titlebar is a
        // DecorationProfile field) rather than the legacy per-mode
        // borderState().hideTitleBars flag: resolve the window's surface path and
        // read effectiveHideTitlebar(). Floating windows are never re-acquired by
        // a retile, so the floating guard is retained.
        const PhosphorSurfaceShaders::DecorationProfile profile =
            m_decorationTree.resolve(resolveSurfacePathFor(windowId));
        return profile.effectiveHideTitlebar() && !isWindowFloating(windowId);
    });
    connect(m_decorationManager.get(), &DecorationManager::windowDecorationRestored, this,
            [this](const QString& windowId) {
                // A veto-driven restore leaves the window mode-owned and
                // still border-eligible — rebuild its overlay instead of
                // dropping it. updateWindowBorder self-gates on the merged
                // appearance (it removes first and re-creates only when
                // something should show). Border overlays are visual-only,
                // so off-desktop windows just get their stale item dropped —
                // the desktopChanged → updateAllBorders refresh rebuilds
                // theirs when they become visible (same policy as
                // updateAllBorders and markWindowSnapped). Exact-id re-check:
                // findWindowById's fuzzy appId fallback could resolve a
                // same-app sibling for a dead id, and creating a border item
                // keyed under the dead id against the sibling would linger
                // until the next full rebuild.
                KWin::EffectWindow* w = findWindowById(windowId);
                if (w && getWindowId(w) == windowId && w->isOnCurrentDesktop()) {
                    updateWindowBorder(windowId, w);
                } else {
                    removeWindowBorder(windowId);
                }
            });
    connect(m_decorationManager.get(), &DecorationManager::drainFinished, this, [this]() {
        updateAllBorders();
    });
}

void PlasmaZonesEffect::removeWindowBorder(const QString& windowId)
{
    auto it = m_windowBorders.find(windowId);
    if (it == m_windowBorders.end()) {
        return;
    }
    WindowBorder& wb = it.value();
    // Release the offscreen redirect + border shader slot IF this border owns
    // it. When an animation transition currently owns the slot (shaderApplied
    // is false — the transition's begin took it over), we must NOT touch
    // setShader / unredirect: the transition lifecycle owns the handover and a
    // stray unredirect here would tear down the animation mid-flight. The
    // transition-end path re-checks m_windowBorders and only re-applies the
    // border shader when the border still exists, so dropping the entry here
    // (after this guarded release) is the correct teardown.
    if (wb.shaderApplied) {
        if (KWin::EffectWindow* w = findWindowById(windowId)) {
            // Only clear when no transition raced in to own the slot between
            // this border being applied and now — reconcileBorderShader would
            // have cleared shaderApplied in that case, but guard defensively.
            if (!m_shaderManager.findTransition(w)) {
                setShader(w, nullptr);
                unredirect(w);
            }
        }
    }
    m_windowBorders.erase(it);

    // Free the window's multipass FBO targets (surfaceTex + buffer chain) — these
    // are only allocated for multipass packs and only while the window has a
    // border, so dropping them on border removal / window close reclaims the GPU
    // memory. No-op for single-pass packs (the map never held an entry).
    m_surfaceMultipass.erase(windowId);
}

void PlasmaZonesEffect::clearAllBorders()
{
    while (!m_windowBorders.isEmpty()) {
        removeWindowBorder(m_windowBorders.begin().key());
    }
}

void PlasmaZonesEffect::updateWindowBorder(const QString& windowId, KWin::EffectWindow* w)
{
    // Remove existing border for this window first
    removeWindowBorder(windowId);

    // MEMBERSHIP → surface path (autotile.tiled / snap.snapped / else floating).
    // The resolved DecorationProfile for that path is the SOLE source of the
    // surface-pack chain + the per-window hide-titlebar choice; border APPEARANCE
    // (width / radius / colours) is no longer a host-resolved decoration field —
    // it is the resolved pack's own PARAMETERS, baked into the CompiledSurfacePack
    // and pushed by pushBorderUniforms. The autotile/snap BorderState feeds none
    // of this.
    const QString surfacePath = resolveSurfacePathFor(windowId);
    const PhosphorSurfaceShaders::DecorationProfile profile = m_decorationTree.resolve(surfacePath);

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    // APP-WINDOW GATE: decoration applies to application windows only. Reuse the
    // same structural app-window filter that snapping / zone management already
    // use (shouldHandleWindow) — exactly as the animation path reuses it via
    // shouldAnimateWindow — so the catch-all "window.floating" surface path never
    // paints a border onto a non-window surface. resolveSurfacePathFor falls back
    // to window.floating for ANY unmanaged window (docks, panels, the desktop,
    // popups, dialogs, OSDs, tooltips, notifications, portal / plasma-shell
    // surfaces, our own overlays), and the window-node border default is inherited
    // there, so without this gate every such surface would get a border.
    if (!shouldHandleWindow(w)) {
        return;
    }

    // DECORATE GATE: a window decorates when it is a member (the isTiledWindow
    // path that produced a non-floating surfacePath above) AND its resolved
    // profile declares a non-empty pack chain. An explicitly-empty chain means
    // "no decoration packs for this surface" — render nothing. The window-rule
    // override path that used to force/suppress a border by width/colour/show is
    // dropped (those WindowBorder fields are gone); per-window appearance rules
    // are a follow-up that will override parameters[packId], not host fields.
    const QStringList chain = profile.effectiveChain();
    if (chain.isEmpty()) {
        return;
    }

    // The base pack id to render this stage. The full chain is stored whole so the
    // next stage can composite chain[1..] over the base; for now ONLY chain[0]
    // renders. chain is non-empty here, so value(0) is the real entry; the
    // "border" default is a defensive fallback only.
    const QString basePackId = chain.value(0, QStringLiteral("border"));

    // Store the resolved chain + hide-titlebar choice. The appearance lives with
    // the compiled pack (its baked customParams/customColors); pushBorderUniforms
    // reads live frameGeometry()/expandedGeometry() + viewport.scale() per frame
    // so a resize/move/output-scale change needs no geometry-sync bookkeeping —
    // the OffscreenEffect redirect already drives a fresh paint on every change.
    WindowBorder wb;
    wb.chain = chain;
    wb.basePackId = basePackId;
    wb.hideTitlebar = profile.effectiveHideTitlebar();

    // Corner rounding and the outline are entirely the SHADER's job (the
    // rounded-rect SDF in the border fragment shader), identically for decorated
    // and borderless windows. It operates on the COMPOSITED redirected texture, so
    // it never clips individual client subsurfaces — and crucially we do NOT touch
    // the KWin window's own BorderRadius: setting it made KWin clip the client
    // surface independently, which on a server-side-decorated window cut the inner
    // surface and left the shader's corner inset behind KWin's. We draw no drop
    // shadow: KWin does not render the shadow into this redirected texture (its
    // expanded margin arrives transparent), so there is nothing here to reshape.

    m_windowBorders.insert(windowId, wb);

    // Apply the offscreen border shader (redirect + setShader) unless an
    // animation transition currently owns this window's shader slot — in which
    // case reconcileBorderShader leaves the transition alone and the border
    // shader is (re-)applied when the transition ends. A geometry change drives
    // its own repaint, so no manual repaint is needed here for the steady case;
    // request one anyway so a border added to a STATIC window (no pending
    // damage) reaches paintWindow and the outline becomes visible immediately.
    reconcileBorderShader(windowId, w);
    // `w` is non-null here (the early-out above returns on !w), so no
    // KWin::effects guard is needed — addRepaintFull is a window method.
    w->addRepaintFull();
}

void PlasmaZonesEffect::updateAllBorders()
{
    clearAllBorders();

    // Iterate all effect windows and (re-)create borders. updateWindowBorder
    // fully self-gates now — the app-window filter (shouldHandleWindow) plus the
    // resolved decoration chain decide whether a window decorates — so border
    // creation is driven solely by that gate, NOT by tiling/snap membership or
    // by whether any animation rule exists. The rule set is still consulted only
    // for the title-bar override reconcile below.
    const bool haveRules = !m_shaderManager.animationRuleSet().isEmpty();
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted()) {
            continue;
        }
        const QString wid = getWindowId(w);
        // Self-heal compositor-initiated noBorder resets: KWin silently
        // re-decorates off-desktop windows on desktop switches. resyncWindow
        // is a self-guarding no-op unless the manager owns the window,
        // believes it hidden, and the compositor reports the decoration
        // back — so running it for every window here is cheap and covers
        // ALL owner kinds (autotile, snap, rule) on every desktop return,
        // activation, and border refresh.
        m_decorationManager->resyncWindow(wid);
        // Border overlays are visual, so only build them for windows on the
        // current desktop. Every on-desktop window is re-resolved; the gate
        // inside updateWindowBorder (app-window filter + resolved chain) decides
        // whether it actually decorates, so a floating application window picks
        // up the window-node border default regardless of tiling/snap membership
        // or whether any animation rule exists.
        if (w->isOnCurrentDesktop()) {
            updateWindowBorder(wid, w);
        }
        // Title-bar hiding (setNoBorder) is a persistent decoration-state change
        // that survives desktop switches, so reconcile the rule override for ALL
        // windows the rule may match — otherwise a SetHideTitleBar rule added
        // while the matched window sits on another virtual desktop would not take
        // effect until that window is next activated.
        if (haveRules) {
            reconcileRuleHiddenTitleBar(wid, w);
        }
    }
    // When no rules remain, the per-window reconcile above is skipped (haveRules
    // is false), so a Rule owner or force-show veto from a now-removed
    // SetHideTitleBar rule would linger until effect teardown. Clear all rule
    // overrides in that case — a no-op when the manager tracks none (the
    // common no-rules path), so this costs nothing when nothing is overridden.
    if (!haveRules) {
        restoreAllRuleHiddenTitleBars();
    }
}

void PlasmaZonesEffect::reconcileRuleHiddenTitleBar(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w || windowId.isEmpty()) {
        return;
    }
    // Tri-state rule override, forwarded to the DecorationManager:
    //   unset → no opinion (mode owners decide)
    //   true  → rule hides (a Rule owner joins the mode owners)
    //   false → rule FORCE-SHOWS (a veto that pins the decoration visible
    //           over any mode owner; owners re-assert when the rule changes)
    // The manager owns the capability gate, the mode-ownership coordination
    // the old m_ruleHiddenTitleBars/modeBorderless dance approximated, and
    // the geometry re-assert across veto-driven decoration flips.
    const std::optional<ResolvedWindowAppearance> ovr = resolveWindowAppearance(
        m_shaderManager.animationRuleEvaluator(), windowRuleQueryFor(w, getWindowScreenId(w)), windowId);
    m_decorationManager->setRuleOverride(windowId, ovr ? ovr->hideTitleBar : std::nullopt);
}

bool PlasmaZonesEffect::isWindowMarkedSnapped(const QString& windowId) const
{
    return m_snapHandler->isTiledWindow(windowId);
}

const PhosphorCompositor::BorderState* PlasmaZonesEffect::resolveBorderStateFor(const QString& windowId) const
{
    // Autotile takes precedence; a window can transiently appear in both the
    // autotile and snap border sets during a mode switch (the call sites guard
    // against steady-state double-tracking via isAutotileScreen, but the
    // transition window is real), and resolving autotile-first is the
    // authoritative tie-break — this ordering is load-bearing, not cosmetic.
    const BorderState& autotile = m_autotileHandler->borderState();
    if (AutotileStateHelpers::shouldShowBorderForWindow(autotile, windowId)) {
        return &autotile;
    }
    if (m_snapHandler->shouldShowBorderForWindow(windowId)) {
        return &m_snapHandler->borderState();
    }
    return nullptr;
}

QString PlasmaZonesEffect::resolveSurfacePathFor(const QString& windowId) const
{
    // MEMBERSHIP-only resolution — IGNORES the owning mode's legacy showBorder
    // gate so the tree's effectiveShowBorder() is the sole render gate (see
    // updateWindowBorder). isTiledWindow tests bucket membership without the
    // showBorder coupling shouldShowBorderForWindow adds, so membership and the
    // show gate are cleanly separated WITHOUT any phosphor-compositor lib change
    // (both predicates already exist). Same autotile-first precedence as
    // resolveBorderStateFor; falls back to window.floating for an unmanaged
    // window (a per-window rule may still force a border there).
    if (AutotileStateHelpers::isTiledWindow(m_autotileHandler->borderState(), windowId)) {
        return QStringLiteral("window.tiled");
    }
    if (m_snapHandler->isTiledWindow(windowId)) {
        return QStringLiteral("window.snapped");
    }
    return QStringLiteral("window.floating");
}

void PlasmaZonesEffect::seedDecorationTreeBaseline()
{
    // Mirror the daemon's ConfigDefaults::decorationProfileTree() in the NEW
    // shape: borders + title-bar hiding are WINDOW-only, so the BASELINE is
    // empty/neutral (daemon surfaces inherit no decoration) and the border
    // default lives on the "window" node. A single "border" pack in the window
    // chain, the shared DecorationDefaults hide-titlebar constant, and the
    // border APPEARANCE carried as the "border" pack's PARAMETERS (not host
    // decoration fields) so the effect's pre-fetch rendering can't drift from
    // what the daemon would persist. Border width / corner radius come from the
    // SHARED DecorationDefaults; the active/inactive colours seed the border
    // pack's own metadata defaults as #AARRGGBB. The daemon's real fetch
    // overwrites this whole tree (with the system-resolved colours when
    // useSystemAccent is on), exactly as the old pre-fetch tree was overwritten
    // before the async load landed; live system-accent resolution is a
    // follow-up — useSystemAccent stays a declared param consumed later, not
    // resolved here.
    PhosphorSurfaceShaders::DecorationProfile baseline; // empty/neutral

    PhosphorSurfaceShaders::DecorationProfile window;
    window.chain = QStringList{QStringLiteral("border")};
    window.hideTitlebar = PhosphorCompositor::DecorationDefaults::HideTitleBars;

    QVariantMap borderParams;
    borderParams.insert(QStringLiteral("borderWidth"), PhosphorCompositor::DecorationDefaults::BorderWidth);
    borderParams.insert(QStringLiteral("cornerRadius"), PhosphorCompositor::DecorationDefaults::BorderRadius);
    borderParams.insert(QStringLiteral("useSystemAccent"), true);
    borderParams.insert(QStringLiteral("activeColor"), QColor(QStringLiteral("#ff3daee9")).name(QColor::HexArgb));
    borderParams.insert(QStringLiteral("inactiveColor"), QColor(QStringLiteral("#ff5c6370")).name(QColor::HexArgb));

    QVariantMap params;
    params.insert(QStringLiteral("border"), borderParams);
    window.parameters = params;

    PhosphorSurfaceShaders::DecorationProfileTree tree;
    tree.setBaseline(baseline);
    tree.setOverride(QStringLiteral("window"), window);
    m_decorationTree = std::move(tree);
}

void PlasmaZonesEffect::restoreAllRuleHiddenTitleBars()
{
    // The authoritative window-rule state is gone (rule set emptied, daemon
    // loss, effect teardown): clear every Rule owner and force-show veto. The
    // manager restores a title bar only where no mode owner remains, so the
    // modes' decoration management is never fought.
    m_decorationManager->clearAllRuleOverrides();
}

// ─────────────────────────────────────────────────────────────────────────────
// Surface shader (the window border / rounded-corner pack and any future surface
// pack) — per-pack compile + registry search-path setup live in
// shader_transitions.cpp (compiledPack() / compiledPackForWindow() /
// ensureSurfaceRegistryPaths()), reusing the shared GLSL include / param-preamble
// / #define-PLASMAZONES_KWIN pipeline there. The border is the first surface
// shader pack: data/surface/border, loaded via SurfaceShaderRegistry.
// ─────────────────────────────────────────────────────────────────────────────

void PlasmaZonesEffect::reconcileBorderShader(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    auto it = m_windowBorders.find(windowId);
    // The presence of a WindowBorder entry IS the "wants border" gate now:
    // updateWindowBorder only inserts one for a member window whose resolved
    // profile declares a non-empty pack chain (border appearance moved into the
    // pack's own params, so there is no host width/colour to test here).
    const bool wantsBorder = (it != m_windowBorders.end());

    // An in-flight animation transition owns the shader slot — the transition's
    // begin already called setShader(animationShader) and redirect(). Leave it
    // alone: the transition-end path re-applies the border shader (and keeps the
    // redirect) when the border still exists. Just clear our ownership flag so
    // the per-frame push and removeWindowBorder defer to the transition.
    if (m_shaderManager.findTransition(w)) {
        if (it != m_windowBorders.end()) {
            it->shaderApplied = false;
        }
        return;
    }

    if (wantsBorder) {
        // The redirect shader OffscreenData::paint runs to present this window:
        //   single pack → the pack's own main shader (it blits the redirected
        //                 surface through itself, sampling its buffer iChannels);
        //   multi pack  → the passthrough present shader, which samples the
        //                 pre-composited final FBO that paintWindow's
        //                 renderSurfaceChainComposite produced (the per-pack mains
        //                 ran as FBO passes there, not via OffscreenData).
        // Both compile-on-first-use; a null result tears the redirect down rather
        // than leave the window blitting a dead/stale shader forever (a just-ended
        // transition hands the slot back here still redirected).
        KWin::GLShader* redirectShader = nullptr;
        if (it->chain.size() > 1) {
            redirectShader = surfacePresentShader();
        } else if (CompiledSurfacePack* const pack = compiledPackForWindow(windowId)) {
            redirectShader = pack->shader.get();
        }
        if (!redirectShader) {
            // setShader(nullptr)/unredirect are no-ops when the window was never
            // redirected.
            setShader(w, nullptr);
            unredirect(w);
            it->shaderApplied = false;
            return;
        }
        // redirect() is idempotent for an already-redirected window; setShader()
        // replaces any prior pointer. Re-applying the same shader is a no-op.
        redirect(w);
        setShader(w, redirectShader);
        it->shaderApplied = true;
    } else if (it != m_windowBorders.end() && it->shaderApplied) {
        // Border removed but we still own the slot and no transition raced in.
        setShader(w, nullptr);
        unredirect(w);
        it->shaderApplied = false;
    }
}

void PlasmaZonesEffect::pushBorderUniforms(KWin::EffectWindow* w, const CompiledSurfacePack& pack, qreal scale)
{
    // drawWindow (the sole caller) has already resolved @p pack, confirmed the
    // border is applied, ruled out a transition owning the slot, and bound
    // pack.shader, so this just computes and writes the uniforms onto the bound
    // program. The border APPEARANCE is no longer a parameter here — it rides the
    // pack's baked customParams/customColors, pushed below.
    KWin::GLShader* shader = pack.shader.get();

    // The shader evaluates a rounded-rect SDF over the window FRAME to round the
    // corners + draw the outline. It needs the expanded (redirected) texture size
    // for the top-down pixel reconstruction, the frame rect placed within that
    // texture (device px), and the logical-to-device scale so the pack can scale
    // its own logical-px appearance params (border width / corner radius).
    // windowExpandedSize is the redirected FBO extent in device px; expandedGeometry
    // covers frame + drop shadow (falls back to frame when empty, e.g. a shadowless
    // window). frameTopLeft = (frameGeometry.topLeft - expandedGeometry.topLeft) *
    // scale is the frame's offset inside that texture (top-down device px), and
    // frameSize = frameGeometry.size * scale its extent.
    const QRectF frame = w->frameGeometry();
    QRectF expanded = w->expandedGeometry();
    if (expanded.isEmpty()) {
        expanded = frame;
    }
    const QVector2D windowExpandedSize(static_cast<float>(expanded.width() * scale),
                                       static_cast<float>(expanded.height() * scale));
    const QVector2D frameTopLeft(static_cast<float>((frame.left() - expanded.left()) * scale),
                                 static_cast<float>((frame.top() - expanded.top()) * scale));
    const QVector2D frameSize(static_cast<float>(frame.width() * scale), static_cast<float>(frame.height() * scale));

    // The caller binds the border shader (a KWin::ShaderBinder kept in scope
    // through the subsequent effects->drawWindow) — setUniform writes to the
    // currently bound program, so we must NOT bind/unbind here or the uniforms
    // would be set on the wrong (or no) program.
    if (pack.uSurfaceSizeLoc >= 0) {
        shader->setUniform(pack.uSurfaceSizeLoc, windowExpandedSize);
    }
    if (pack.uFrameTopLeftLoc >= 0) {
        shader->setUniform(pack.uFrameTopLeftLoc, frameTopLeft);
    }
    if (pack.uFrameSizeLoc >= 0) {
        shader->setUniform(pack.uFrameSizeLoc, frameSize);
    }
    // Logical-to-device scale: the pack multiplies its logical-px params by this.
    if (pack.uScaleLoc >= 0) {
        shader->setUniform(pack.uScaleLoc, static_cast<float>(scale));
    }
    // Focus flag: the pack mixes its active/inactive appearance params on this.
    if (pack.uFocusedLoc >= 0) {
        const float focused = (KWin::effects && w == KWin::effects->activeWindow()) ? 1.0f : 0.0f;
        shader->setUniform(pack.uFocusedLoc, focused);
    }
    // Continuous time for an animated pack. -1 (static pack, e.g. the border)
    // pushes nothing; postPaintScreen only drives the window to repaint when a
    // pack actually references iTime (windowSurfaceAnimates), so a static
    // decoration neither pays this push nor forces per-frame repaints.
    if (pack.uTimeLoc >= 0) {
        shader->setUniform(pack.uTimeLoc, surfaceShaderTimeSeconds());
    }

    // Pack-declared parameters (customParams / customColors). Values are resolved
    // at compile time from the pack's DecorationProfile overrides merged over its
    // declared defaults (compiledPack). Only slots the shader actually references
    // resolve to a valid location, so the border pack (no params) pushes nothing.
    for (int slot = 0; slot < PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams; ++slot) {
        if (pack.customParamsLoc[slot] >= 0) {
            shader->setUniform(pack.customParamsLoc[slot], pack.customParamsValues[slot]);
        }
    }
    for (int slot = 0; slot < PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors; ++slot) {
        if (pack.customColorsLoc[slot] >= 0) {
            shader->setUniform(pack.customColorsLoc[slot], pack.customColorsValues[slot]);
        }
    }
}

void PlasmaZonesEffect::drawWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                   KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                                   KWin::WindowPaintData& data)
{
    // Apply the border shader passively for static bordered windows: bind it +
    // set its uniforms, then let OffscreenEffect::drawWindow re-blit the
    // redirected FBO through it. OffscreenData::paint re-binds the SAME program
    // (the one from setShader in reconcileBorderShader), and uniform values
    // persist in the program object across our ShaderBinder pop — so the values
    // we set here are live for that blit. This is the KDE-Rounded-Corners model:
    // the shader applies on every composite, idle included, with no re-render,
    // and no forced per-frame repaints. Skip during a transition (the animation
    // shader owns the setShader slot and paintWindow's transition branch drives
    // it) and during snapshot capture.
    //
    // MULTIPASS surface packs additionally run their buffer passes
    // (renderSurfaceBufferPasses) into per-window FBOs and bind the outputs as
    // iChannel0..3 so the main pass can sample them. Single-pass packs (the
    // border, pack.bufferPasses empty) skip all of this and take the cheap
    // OffscreenData path unchanged.
    //
    // Texture-unit map for the idle border blit's multipass channels: start a
    // few units PAST the animation path's user-texture / old-snapshot /
    // surface-layer units (which live at 0..2+kMaxUserTextureSlots on the
    // paintWindow transition path) so the two paths never collide even though
    // they don't run for the same window at the same time. Unit 0 is uTexture0
    // (KWin's OffscreenData::paint binds the redirected surface there); the
    // buffer-output iChannelN go to kSurfaceChannelBaseUnit + N.
    int boundChannels = 0; // # of iChannel units we bound (for post-draw cleanup)
    constexpr int kSurfaceChannelBaseUnit = 3 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
    if (!m_capturingSnapshot && !m_windowBorders.isEmpty() && !m_shaderManager.findTransition(w)) {
        const auto bit = m_windowBorders.constFind(getWindowId(w));
        if (bit != m_windowBorders.constEnd() && bit->shaderApplied && bit->chain.size() > 1) {
            // MULTI-PACK present: the whole chain was already composited into a
            // per-window FBO by paintWindow (renderSurfaceChainComposite). Bind the
            // final slot to a high unit and point the present passthrough's uFinal
            // at it. OffscreenData::paint re-binds the present program (the
            // setShader one from reconcileBorderShader) for its blit, so the
            // uniform persists; the texture stays bound until the post-draw cleanup.
            KWin::GLShader* const present = surfacePresentShader();
            const auto stateIt = m_surfaceMultipass.find(getWindowId(w));
            if (present && stateIt != m_surfaceMultipass.end()
                && stateIt->second.compositeTex[stateIt->second.finalSlot]) {
                const int unit = kSurfaceChannelBaseUnit;
                KWin::ShaderBinder binder(present);
                glActiveTexture(GL_TEXTURE0 + unit);
                stateIt->second.compositeTex[stateIt->second.finalSlot]->bind();
                if (m_surfacePresentFinalLoc >= 0) {
                    present->setUniform(m_surfacePresentFinalLoc, unit);
                }
                glActiveTexture(GL_TEXTURE0);
                boundChannels = 1; // unit kSurfaceChannelBaseUnit+0, freed in the cleanup below
            }
        } else if (bit != m_windowBorders.constEnd() && bit->shaderApplied) {
            // Per-window resolved base pack — replaces the old single global
            // m_borderShader. nullptr → compile failed/latched (render nothing).
            CompiledSurfacePack* const pack = compiledPackForWindow(getWindowId(w));
            if (pack) {
                // Multipass buffer outputs are rendered in paintWindow
                // (renderSurfaceBufferPasses), NOT here. That render re-enters the
                // draw chain (effects->drawWindow) to capture the raw surface;
                // calling it from inside THIS drawWindow override would re-enter
                // KWin's shared draw-window iterator while it is already mid-walk,
                // corrupting it and crashing the OffscreenEffect::drawWindow below.
                // paintWindow runs the capture on a fresh iterator; here we only bind
                // the ready per-window buffer textures as iChannels.
                const auto stateIt = m_surfaceMultipass.find(getWindowId(w));
                const bool channelsReady = !pack->bufferPasses.empty() && stateIt != m_surfaceMultipass.end()
                    && !stateIt->second.bufferTex.empty();

                KWin::ShaderBinder binder(pack->shader.get());
                pushBorderUniforms(w, *pack, viewport.scale());

                if (channelsReady) {
                    const SurfaceMultipassState& state = stateIt->second;
                    const int n = qMin(static_cast<int>(state.bufferTex.size()), 4);
                    for (int i = 0; i < n; ++i) {
                        if (!state.bufferTex[i]) {
                            continue;
                        }
                        const int unit = kSurfaceChannelBaseUnit + i;
                        glActiveTexture(GL_TEXTURE0 + unit);
                        state.bufferTex[i]->bind();
                        if (pack->iChannelLoc[i] >= 0) {
                            pack->shader->setUniform(pack->iChannelLoc[i], unit);
                        }
                        if (pack->iChannelResolutionLoc[i] >= 0) {
                            const QVector4D res(static_cast<float>(state.bufferTex[i]->width()),
                                                static_cast<float>(state.bufferTex[i]->height()), 0.0f, 0.0f);
                            pack->shader->setUniform(pack->iChannelResolutionLoc[i], res);
                        }
                        ++boundChannels;
                    }
                    // Restore GL_TEXTURE0 as the active unit so OffscreenData::paint
                    // (which binds the redirected surface to unit 0 without a
                    // preceding glActiveTexture) targets the right unit.
                    glActiveTexture(GL_TEXTURE0);
                }
            }
        }
    }
    KWin::OffscreenEffect::drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);

    // Unbind the multipass channel units we bound and restore GL_TEXTURE0 —
    // texture hygiene mirroring paint_pipeline.cpp, so a stray bind doesn't leak
    // into the next window's draw. No-op when boundChannels == 0 (single-pass).
    for (int i = 0; i < boundChannels; ++i) {
        glActiveTexture(GL_TEXTURE0 + kSurfaceChannelBaseUnit + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (boundChannels > 0) {
        glActiveTexture(GL_TEXTURE0);
    }
}

} // namespace PlasmaZones
