// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glow surface shader — the Border pack's rounded-corner + border clip, PLUS a
// soft glow halo sampled from the blurred surface (iChannel0, produced by
// buffer0.frag). A multipass demo for the compositor's surface buffer chain.
//
// The base rounded-rect SDF logic is identical to data/surface/border: one
// analytic SDF over the frame rect clips the content to the inner rounded rect
// and lays the border band over the background. On top of that, the blurred
// surface (iChannel0) is added as a halo concentrated in and just outside the
// border band, tinted by the host border colour — so the window appears to glow
// with its own content's colour along the edge. Static (no iTime).

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 tex = surfaceTexel(vTexCoord);

    // Fragment's top-down device pixel within the surface texture; the content
    // rect sits at uSurfaceFrameTopLeft..+uSurfaceFrameSize.
    vec2 p = surfacePixel(vTexCoord);
    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 cen = uSurfaceFrameTopLeft + halfSz;
    float r = clamp(uSurfaceRadius, 0.0, min(halfSz.x, halfSz.y));

    // Analytic rounded-rect SDF over the frame (Inigo-Quilez); < 0 inside.
    vec2 q = abs(p - cen) - halfSz + r;
    float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;

    float aa = 0.7;
    float insideMask = 1.0 - smoothstep(-aa, aa, d);
    float edge = smoothstep(-uSurfaceBorderWidth - aa, -uSurfaceBorderWidth + aa, d);

    // Base border composite (same as the Border pack): content clipped to the
    // inner rounded rect, border band laid over transparency, premultiplied.
    float ba = edge * insideMask * uSurfaceColor.a;
    vec4 contentPx = tex * (1.0 - edge);
    vec4 base = vec4(uSurfaceColor.rgb * ba, ba) + contentPx * (1.0 - ba);

    // ── Glow halo from the blurred surface (iChannel0) ──────────────────────
    // The blurred surface (buffer0.frag), upright via the same sampling
    // convention as uTexture0. This is the multipass demo's visible payload: a
    // bold bloom that bleeds INWARD from the frame edge over a generous radius,
    // brightening the window content near its border. Biased inward (over the
    // content, which always exists) rather than into the off-frame margin, so it
    // is clearly visible even on windows with no drop-shadow expansion — the
    // unmistakable proof that the buffer pass ran and iChannel0 is sampled.
    vec4 glowSrc = texture(iChannel0, vTexCoord);
    // Glow reach: ~12% of the shorter frame dimension, floored so small windows
    // still bloom. -d is depth INTO the content (d < 0 inside the rounded rect).
    float glowRadius = max(0.12 * min(uSurfaceFrameSize.x, uSurfaceFrameSize.y), 24.0);
    float inner = clamp(1.0 - (-d) / glowRadius, 0.0, 1.0) * insideMask;
    inner *= inner; // concentrate the bloom toward the edge

    // Additive bloom: the blurred edge content, tinted halfway toward the host
    // border colour so the glow reads as a coloured halo of the window's own
    // content. Strong (0.8) so the effect is unmistakable vs the plain Border pack.
    vec3 glowColor = mix(glowSrc.rgb, uSurfaceColor.rgb, 0.5);
    vec3 glowAdd = glowColor * (inner * 0.8 * glowSrc.a);

    // Plus a soft OUTER halo straddling the frame edge — visible where the
    // redirected texture has off-frame margin (e.g. a server-side drop shadow).
    float outer = clamp(1.0 - abs(d) / max(uSurfaceBorderWidth * 2.0, 8.0), 0.0, 1.0);
    outer *= outer;
    float outerAmt = outer * glowSrc.a * 0.6;

    // Composite additively over the base, premultiplied.
    fragColor = vec4(base.rgb + glowAdd + glowColor * outerAmt, min(base.a + outerAmt, 1.0));
}
