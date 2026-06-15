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
        return m_autotileHandler->borderState().hideTitleBars && !isWindowFloating(windowId);
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

    // MEMBERSHIP → surface path (autotile.tiled / snap.snapped / else floating),
    // resolved INDEPENDENT of any showBorder gate. The resolved DecorationProfile
    // for that path is now the SOLE source of border APPEARANCE (width / radius /
    // colours / showBorder) + the surface-pack chain — the autotile/snap
    // BorderState no longer feeds appearance (Stage 2a).
    const QString surfacePath = resolveSurfacePathFor(windowId);
    const PhosphorSurfaceShaders::DecorationProfile profile = m_decorationTree.resolve(surfacePath);

    // Per-window rule override — applies to ANY matched window, snapped or
    // floating (mirrors SetOpacity). Resolved against the same evaluator the
    // opacity / animation rules use; gated on a non-empty rule set so windows
    // with no rules pay nothing.
    std::optional<ResolvedWindowAppearance> ovr;
    if (w && !m_shaderManager.animationRuleSet().isEmpty()) {
        ovr = resolveWindowAppearance(m_shaderManager.animationRuleEvaluator(),
                                      windowRuleQueryFor(w, getWindowScreenId(w)), windowId);
    }

    // Merge: a rule field wins; otherwise fall back to the resolved profile.
    // showBorder is the SOLE render gate now (the legacy mode showBorder coupling
    // was stripped from membership in resolveSurfacePathFor) — a floating window
    // whose profile says showBorder=false shows nothing unless a rule forces it.
    const bool show = (ovr && ovr->showBorder) ? *ovr->showBorder : profile.effectiveShowBorder();
    if (!show) {
        return;
    }

    const int bw = (ovr && ovr->borderWidth) ? *ovr->borderWidth : profile.effectiveBorderWidth();
    if (bw <= 0) {
        return;
    }

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    // Choose color. The resolved profile carries separate active and inactive
    // border colours (the daemon writes the system-resolved colours into these
    // when useSystemColors is on, so the effect reads resolved colours and never
    // the use-system flag — same contract as the old BorderState path). Pick the
    // one matching the window's current focus state. A per-window SetBorderColor
    // rule, when matched, overrides it. Focus-dependence of the RULE colour is
    // expressed in the rule itself via the IsFocused match condition:
    // `windowRuleQueryFor` set the query's isFocused flag, so a focus-scoped rule
    // (`WHEN focused`/`WHEN NOT focused`) only fills the border-colour slot in its
    // matching state, while a focus-agnostic rule applies in both. Either way
    // `ovr->borderColor` already holds the colour appropriate to this window's
    // current focus — no post-resolution switch. A window whose profile has no
    // colour and whose only rule is focus-scoped thus correctly shows no border
    // in the unmatched state; author a focus-agnostic rule to keep one in both.
    const bool isFocused = (w == KWin::effects->activeWindow());
    const QColor profileColor = isFocused ? profile.effectiveActiveColor() : profile.effectiveInactiveColor();
    const QColor bc = (ovr && ovr->borderColor) ? *ovr->borderColor : profileColor;
    if (!bc.isValid() || bc.alpha() == 0) {
        return;
    }

    const int br = (ovr && ovr->borderRadius) ? *ovr->borderRadius : profile.effectiveBorderRadius();

    // Resolve the surface-pack chain + the base pack to render this stage. The
    // full chain is stored whole so the next stage can composite chain[1..] over
    // the base; for now ONLY chain[0] renders (base pack). Default to "border"
    // when the profile declares an empty chain so a misconfigured/empty profile
    // still renders the canonical decoration rather than nothing.
    const QStringList chain = profile.effectiveChain();
    const QString basePackId = chain.value(0, QStringLiteral("border"));

    // Store the resolved appearance (LOGICAL pixels). pushBorderUniforms scales
    // these by viewport.scale() per-frame to reach device px for the shader,
    // and reads live frameGeometry()/expandedGeometry() so a resize/move needs
    // no geometry-sync bookkeeping — the OffscreenEffect redirect already drives
    // a fresh paint on every geometry change.
    WindowBorder wb;
    wb.width = bw;
    wb.radius = br;
    wb.color = bc;
    wb.chain = chain;
    wb.basePackId = basePackId;

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

    // Iterate all effect windows and create borders for any window managed by
    // a mode (autotile or snap) that currently shows borders, OR matched by a
    // per-window border rule (which can draw on an otherwise-borderless /
    // floating window). updateWindowBorder self-gates on the merged effective
    // appearance, so calling it when rules exist is safe; reconcile the rule
    // title-bar override in the same pass so it tracks context changes.
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
        // current desktop. Title-bar hiding (setNoBorder) is a persistent
        // decoration-state change that survives desktop switches, so reconcile
        // it for ALL windows the rule may match — otherwise a SetHideTitleBar
        // rule added while the matched window sits on another virtual desktop
        // would not take effect until that window is next activated.
        // Pre-filter on MEMBERSHIP (ignoring the legacy mode showBorder gate) so a
        // tiled/snapped member whose tree profile turns the border ON is picked up
        // even though the old per-mode showBorder is off. updateWindowBorder
        // self-gates on the resolved profile's showBorder, so this only decides
        // WHICH windows are worth re-resolving: members + (when rules exist)
        // rule-matchable floating windows. A floating window with no rule resolves
        // window.floating (showBorder=false by default) and is skipped here.
        const bool isMember = AutotileStateHelpers::isTiledWindow(m_autotileHandler->borderState(), wid)
            || m_snapHandler->isTiledWindow(wid);
        if (w->isOnCurrentDesktop() && (haveRules || isMember)) {
            updateWindowBorder(wid, w);
        }
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
    // Build a baseline mirroring the daemon's ConfigDefaults::decorationProfileTree()
    // from the SHARED DecorationDefaults constants (so the effect's pre-fetch
    // rendering can't drift from what the daemon would persist) + the default
    // "border" pack id. Every field is engaged so the baseline is a complete,
    // self-contained profile. Colours are left invalid (unset → inherit-to-empty):
    // the daemon delivers the RESOLVED active/inactive colours in the real fetch,
    // exactly as BorderState's colours arrive invalid before the async load lands.
    PhosphorSurfaceShaders::DecorationProfile baseline;
    baseline.chain = QStringList{QStringLiteral("border")};
    baseline.borderWidth = PhosphorCompositor::DecorationDefaults::BorderWidth;
    baseline.borderRadius = PhosphorCompositor::DecorationDefaults::BorderRadius;
    baseline.showBorder = PhosphorCompositor::DecorationDefaults::ShowBorder;
    baseline.hideTitlebar = PhosphorCompositor::DecorationDefaults::HideTitleBars;

    PhosphorSurfaceShaders::DecorationProfileTree tree;
    tree.setBaseline(baseline);
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
    const bool wantsBorder = (it != m_windowBorders.end()) && it->width > 0 && it->color.isValid();

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
        // Compile-on-first-use the window's resolved base pack; a failed compile
        // latches and no-ops.
        CompiledSurfacePack* const pack = compiledPackForWindow(windowId);
        if (!pack) {
            // No surface pack available (compile failed/latched, or no pack
            // resolved). Don't leave the window stuck redirected with a stale
            // shader bound: a transition that just ended hands the slot back here
            // still redirected (its setShader remains until we clear it), so tear
            // the redirect down rather than blit a dead shader forever.
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
        setShader(w, pack->shader.get());
        it->shaderApplied = true;
    } else if (it != m_windowBorders.end() && it->shaderApplied) {
        // Border removed but we still own the slot and no transition raced in.
        setShader(w, nullptr);
        unredirect(w);
        it->shaderApplied = false;
    }
}

void PlasmaZonesEffect::pushBorderUniforms(KWin::EffectWindow* w, const CompiledSurfacePack& pack,
                                           const WindowBorder& border, qreal scale)
{
    // drawWindow (the sole caller) has already resolved @p border + @p pack,
    // confirmed it is applied, ruled out a transition owning the slot, and bound
    // pack.shader, so this just computes and writes the uniforms onto the bound
    // program.
    KWin::GLShader* shader = pack.shader.get();

    // The shader evaluates a rounded-rect SDF over the window FRAME to round the
    // corners + draw the outline. It needs the expanded (redirected) texture size
    // for the top-down pixel reconstruction plus the frame rect placed within that
    // texture, the outer corner radius, and the band thickness — all device px.
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
    const float thickness = static_cast<float>(border.width * scale);
    // OUTER radius = content radius + border width, so the outline band sits inside
    // it and the content corner ends one band-width in, at `border.radius`.
    const float radius = static_cast<float>((border.radius + border.width) * scale);

    const QColor& c = border.color;
    const QVector4D outlineColor(static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                                 static_cast<float>(c.blueF()), static_cast<float>(c.alphaF()));

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
    if (pack.uRadiusLoc >= 0) {
        shader->setUniform(pack.uRadiusLoc, radius);
    }
    if (pack.uBorderWidthLoc >= 0) {
        shader->setUniform(pack.uBorderWidthLoc, thickness);
    }
    if (pack.uColorLoc >= 0) {
        shader->setUniform(pack.uColorLoc, outlineColor);
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
        if (bit != m_windowBorders.constEnd() && bit->shaderApplied) {
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
                pushBorderUniforms(w, *pack, *bit, viewport.scale());

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
