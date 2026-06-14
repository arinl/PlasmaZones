// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glow surface shader — the Border pack's rounded-corner + border clip, plus a
// refined edge glow built from a separable Gaussian blur of the window surface
// (iChannel1, from buffer0.frag → buffer1.frag). A multipass demo for the
// compositor surface buffer chain.
//
// Three composited layers make it read as a designed light rather than a flat
// brighten:
//   • Inner glow — the blurred edge content, accent-tinted, SCREEN-blended into
//     the content near the border with an exponential falloff. Screen blend
//     lifts the edges without blowing bright windows out to white.
//   • Outer glow — a soft halo bleeding outward into any off-frame margin (the
//     drop-shadow region), additive premultiplied, exponential falloff. Shows on
//     windows whose redirected texture has shadow expansion.
//   • Rim highlight — a thin near-white accent line hugging the rounded edge for
//     a glass-edge feel.
// Reach scales with window size so the look is consistent. Static (no iTime).

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 tex = surfaceTexel(vTexCoord);

    // Rounded-rect SDF over the frame (same as the Border pack); d < 0 inside.
    vec2 p = surfacePixel(vTexCoord);
    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 cen = uSurfaceFrameTopLeft + halfSz;
    float r = clamp(uSurfaceRadius, 0.0, min(halfSz.x, halfSz.y));
    vec2 q = abs(p - cen) - halfSz + r;
    float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;

    float aa = 0.7;
    float insideMask = 1.0 - smoothstep(-aa, aa, d);
    float edge = smoothstep(-uSurfaceBorderWidth - aa, -uSurfaceBorderWidth + aa, d);

    // Base border composite: content clipped to the inner rounded rect, border
    // band laid over transparency, premultiplied.
    float ba = edge * insideMask * uSurfaceColor.a;
    vec4 contentPx = tex * (1.0 - edge);
    vec4 base = vec4(uSurfaceColor.rgb * ba, ba) + contentPx * (1.0 - ba);

    // Smooth Gaussian-blurred surface (separable H then V). Accent-tinted toward
    // the host border colour so the glow reads as a designed light, not just a
    // brighter copy of the window content.
    vec3 blur = texture(iChannel1, vTexCoord).rgb;
    vec3 glowTint = mix(blur, uSurfaceColor.rgb, 0.55);

    // Reach scales with the window (8% of the shorter side, clamped) so a small
    // dialog and a maximised window glow proportionally the same.
    float minDim = max(min(uSurfaceFrameSize.x, uSurfaceFrameSize.y), 1.0);
    float reach = clamp(0.08 * minDim, 16.0, 96.0);

    // ── Inner glow (screen-blended, over the content) ───────────────────────
    float depthIn = max(-d, 0.0); // distance inward from the frame edge
    float inner = exp(-depthIn / (reach * 0.5)) * insideMask;
    vec3 innerGlow = glowTint * (inner * 0.85);
    vec3 lit = 1.0 - (1.0 - clamp(base.rgb, 0.0, 1.0)) * (1.0 - clamp(innerGlow, 0.0, 1.0));
    base.rgb = mix(base.rgb, lit, insideMask);

    // ── Outer glow (additive, into the off-frame margin) ────────────────────
    float depthOut = max(d, 0.0); // distance outward (only exists where margin does)
    float outerA = exp(-depthOut / (reach * 0.6)) * (1.0 - insideMask) * 0.7;
    vec3 outerGlow = glowTint * outerA;

    // ── Rim highlight (thin bright accent on the edge) ──────────────────────
    float rimW = max(uSurfaceBorderWidth * 0.6, 1.5);
    float rim = exp(-(d * d) / (2.0 * rimW * rimW));
    vec3 rimColor = mix(uSurfaceColor.rgb, vec3(1.0), 0.35);
    float rimA = rim * 0.5 * uSurfaceColor.a;

    vec3 outRgb = base.rgb + outerGlow + rimColor * rimA;
    float outA = clamp(base.a + outerA + rimA, 0.0, 1.0);
    fragColor = vec4(outRgb, outA);
}
