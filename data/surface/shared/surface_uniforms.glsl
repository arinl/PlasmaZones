// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Canonical uniform contract for SURFACE shaders — the third shader-pack
// category, alongside animation (data/animations) and overlay (data/shaders).
// A surface shader decorates a rendered surface: it samples the surface's own
// content (uTexture0) and the geometry of the content rect within that texture,
// and paints decoration (rounded corners + border today; tint / glow / frost in
// future packs). The window border/rounded-corner effect is the first surface
// pack (data/surface/border).
//
// DUAL-RUNTIME, like the animation contract. The SAME pack source compiles for:
//
//   • The compositor (kwin-effect): uTexture0 is the redirected window surface
//     (expanded geometry, device px). The effect prepends `#define
//     PLASMAZONES_KWIN` after `#version`, so the KWin branch below is taken;
//     uniforms are classic default-block uniforms set with GLShader::setUniform.
//
//   • The daemon (PhosphorRendering Qt-RHI): uTexture0 is the daemon surface's
//     own texture. Qt-RHI's SPIR-V pipeline mandates UBO-bound uniforms, so the
//     #else branch binds a std140 UBO at binding=0. (Daemon C++ consumption is
//     wired in a follow-up pass; the branch is authored here so packs compile
//     for both runtimes from day one and need no edits when the daemon lands.)
//
// All length uniforms are DEVICE pixels. The host (compositor or daemon)
// resolves the decoration STATE — geometry from the surface, and radius / width
// / colour / focus from its own settings (the compositor feeds the tiling
// BorderState, focus-resolved) — and the pack only consumes it. Pack-specific
// tweakables ride customParams / customColors via the standard parameter slots.

#ifndef PLASMAZONES_SURFACE_UNIFORMS_GLSL
#define PLASMAZONES_SURFACE_UNIFORMS_GLSL

#ifdef PLASMAZONES_KWIN

// ── Compositor branch — classic default-block uniforms ──────────────────────
// uTexture0 is bound to texture unit 0 by KWin's OffscreenData::paint; a
// sampler2D left unset defaults to unit 0, so no explicit binding is needed.
uniform sampler2D uTexture0;

// Geometry of the surface texture and the content rect within it (device px).
// uSurfaceSize is the full uTexture0 extent (the compositor redirects the
// EXPANDED window geometry: frame + decoration + shadow). The content/frame
// rect sits at uSurfaceFrameTopLeft (top-down) and spans uSurfaceFrameSize, so
// the decoration rounds to the frame corners, not the shadow-padded bounds.
uniform vec2 uSurfaceSize;
uniform vec2 uSurfaceFrameTopLeft;
uniform vec2 uSurfaceFrameSize;

// Host-resolved decoration state.
uniform float uSurfaceRadius;       // outer corner radius (content radius + width)
uniform float uSurfaceBorderWidth;  // decoration band thickness
uniform vec4 uSurfaceColor;         // straight (non-premultiplied) RGBA, focus-resolved
uniform float uSurfaceFocused;      // 1.0 when the surface is focused/active, else 0.0

// Pack-specific tweakable parameters (declared in metadata.json, addressed by
// `#define p_<id> customParamsN_x` / `customColorN` preambles the registry
// generates — identical to the animation/overlay categories).
uniform vec4 customParams[8];
uniform vec4 customColors[16];

#else

// ── Daemon branch — std140 UBO at binding 0 ─────────────────────────────────
// qt_Matrix / qt_Opacity lead the block to match Qt Quick's scene-graph
// expectation (same as the animation/overlay UBOs). Field order is laid out for
// std140: vec2s packed in pairs, vec4/array members 16-aligned. The C++
// mirror struct + offset static_asserts are added with the daemon consumer.
layout(std140, binding = 0) uniform SurfaceUniforms {
    mat4 qt_Matrix;              // offset 0   (64)
    float qt_Opacity;            // offset 64  (4)
    float uSurfaceRadius;        // offset 68  (4)
    float uSurfaceBorderWidth;   // offset 72  (4)
    float uSurfaceFocused;       // offset 76  (4)
    vec2 uSurfaceSize;           // offset 80  (8)
    vec2 uSurfaceFrameTopLeft;   // offset 88  (8)
    vec2 uSurfaceFrameSize;      // offset 96  (8)
    // implicit 8-byte std140 pad (96+8=104 → 112) before the next vec4
    vec4 uSurfaceColor;          // offset 112 (16)
    vec4 customParams[8];        // offset 128 (128)
    vec4 customColors[16];       // offset 256 (256)
};                               // total 512 bytes

layout(binding = 7) uniform sampler2D uTexture0;

#endif // PLASMAZONES_KWIN

// ── Shared helpers (runtime-agnostic; packs use these, not raw uniforms) ─────

// This fragment's position in the surface texture, TOP-DOWN device pixels, with
// the content/frame rect occupying [uSurfaceFrameTopLeft, +uSurfaceFrameSize].
// `uv` is the incoming vTexCoord. The compositor's redirected FBO is
// bottom-origin (Y-up), so the Y is flipped there to reach the top-down space
// the geometry uniforms are expressed in.
vec2 surfacePixel(vec2 uv) {
#ifdef PLASMAZONES_KWIN
    return vec2(uv.x, 1.0 - uv.y) * uSurfaceSize;
#else
    return uv * uSurfaceSize;
#endif
}

// The surface's own texel at `uv`, upright on both runtimes.
vec4 surfaceTexel(vec2 uv) {
#ifdef PLASMAZONES_KWIN
    return texture(uTexture0, uv);
#else
    return texture(uTexture0, vec2(uv.x, 1.0 - uv.y));
#endif
}

#endif // PLASMAZONES_SURFACE_UNIFORMS_GLSL
