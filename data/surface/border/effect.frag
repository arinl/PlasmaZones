// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Border surface shader — rounded corners + window border, the first surface
// pack. One analytic rounded-rect SDF over the content/frame rect drives BOTH
// the corner rounding and the outline (the KDE-Rounded-Corners / shapecorners
// model), identically for decorated and borderless surfaces:
//
//   * Corner clip: the surface content is clipped to the INNER rounded rect
//     (inset by the border thickness), transparent outside. The redirected
//     texture includes any server-side decoration, so a visible titlebar's
//     corners round too.
//
//   * Outline: the border band [-thickness, 0] is laid OVER the background, not
//     over the content, so the content sits INSIDE the border and nothing from
//     the surface leaks through it — a TRANSLUCENT border blends with what is
//     behind the surface, not with its content.
//
// All geometry + decoration state arrives through the surface contract uniforms
// (host-resolved): uSurfaceSize / uSurfaceFrameTopLeft / uSurfaceFrameSize and
// uSurfaceRadius / uSurfaceBorderWidth / uSurfaceColor. The pack declares no
// parameters of its own.

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

    // FIXED device-pixel AA half-width (all length uniforms are device px). NOT
    // fwidth(d): an SDF's fwidth is ~1 on straight edges but ~1.4 at corners
    // (diagonal gradient), which widens the AA at corners — softening the clip
    // and thinning the outline band there. A constant keeps clip + band width
    // uniform everywhere (KDE-Rounded-Corners does the same).
    float aa = 0.7;

    // 1 inside the rounded rect, 0 outside, AA across the boundary.
    float insideMask = 1.0 - smoothstep(-aa, aa, d);

    // Border edge factor: 0 deep inside the content, ramps to 1 from the
    // border's INNER edge outward (d > -thickness).
    float edge = smoothstep(-uSurfaceBorderWidth - aa, -uSurfaceBorderWidth + aa, d);

    // Clip content to the INNER rounded rect and lay the border band over
    // transparency, premultiplied throughout. (1 - edge) is the inner content
    // mask; edge * insideMask is the band clipped to the outer rounded rect.
    float ba = edge * insideMask * uSurfaceColor.a;  // border coverage * its alpha
    vec4 contentPx = tex * (1.0 - edge);             // content, clipped to the inner rect
    fragColor = vec4(uSurfaceColor.rgb * ba, ba) + contentPx * (1.0 - ba);
}
