// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glow buffer pass — box-blur the captured window surface into a downscaled
// glow source. Sampled by effect.frag as iChannel0 to lay a soft halo in and
// around the border band.
//
// This pass sees only uTexture0 (the captured surface) — the host binds it to
// unit 0; the per-frame surface contract uniforms (uSurfaceSize / frame rect)
// are NOT pushed to buffer passes, so the blur uses a fixed UV-space step
// rather than a pixel step. The pass renders at bufferScale (0.5) so the step
// already covers ~2 surface texels per buffer texel — a cheap, soft blur.
//
// Output is the blurred surface, premultiplied (surfaceTexel returns
// premultiplied alpha from the compositor's redirected FBO), so effect.frag can
// composite it directly.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // Fixed UV step (no uSurfaceSize available in a buffer pass). A 9-tap
    // separable-style box blur over a small neighbourhood; the downscaled
    // buffer resolution does most of the softening, this widens the halo.
    const float step = 1.0 / 256.0;

    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            vec2 uv = vTexCoord + vec2(float(dx), float(dy)) * step;
            // Weight by inverse Chebyshev distance so the centre dominates —
            // a soft, rounded blur kernel rather than a hard box.
            float wgt = 1.0 / (1.0 + float(abs(dx) + abs(dy)));
            acc += surfaceTexel(uv) * wgt;
            wsum += wgt;
        }
    }
    fragColor = acc / max(wsum, 1e-4);
}
