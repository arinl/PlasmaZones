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
    // The blurred surface, upright via the same sampling convention as
    // uTexture0. Concentrate the halo in a band straddling the border: peak at
    // the border's outer edge (d ~ 0) and falling off both inward and outward
    // over a glow radius proportional to the border width (with a floor so a
    // thin border still glows).
    vec4 glowSrc = texture(iChannel0, vTexCoord);
    float glowRadius = max(uSurfaceBorderWidth * 2.0, 6.0);
    // Triangular falloff centred on the frame edge (d == 0).
    float halo = clamp(1.0 - abs(d) / glowRadius, 0.0, 1.0);
    halo *= halo; // sharpen the peak

    // Tint the halo toward the border colour, modulated by the blurred surface
    // luminance so the glow carries the window's own edge content. Additive,
    // premultiplied, and kept outside the opaque interior (1 - insideMask pushed
    // partly back in so the band itself also glows).
    float lum = dot(glowSrc.rgb, vec3(0.299, 0.587, 0.114));
    float glowAmt = halo * (0.35 + 0.65 * lum) * glowSrc.a;
    vec3 glowColor = mix(glowSrc.rgb, uSurfaceColor.rgb, 0.5);
    vec3 glowAdd = glowColor * glowAmt;

    // Composite the glow additively over the base. Add to both rgb and alpha so
    // the halo is visible over the desktop outside the window, premultiplied.
    fragColor = vec4(base.rgb + glowAdd, min(base.a + glowAmt, 1.0));
}
