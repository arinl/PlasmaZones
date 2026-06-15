// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared fullscreen-quad vertex stage for the SURFACE shader category. This is
// the LGPL shared contract for data/surface (same license as the sibling
// surface_uniforms.glsl) so it ships alongside the include and resolves from the
// SurfaceShaderItem / kwin-effect include paths with no per-pack vertex shader.
//
// Layer-composited (NOT direct-to-window), like data/shaders/shared/zone.vert.
// The daemon hosts a surface pack by sampling a rendered surface's own layer
// texture into uTexture0 and re-rendering it through this quad inside a Qt Quick
// scene graph; Qt's compositor applies its own NDC/orientation correction when
// it composites this item's node, so this stage must NOT multiply by qt_Matrix.
// Emitting the bare clip-space position keeps the quad upright on every backend
// (a qt_Matrix multiply here would double-correct and flip the result on
// Y-up-NDC backends like OpenGL). The fragment stage's surfaceTexel() helper
// (surface_uniforms.glsl) owns the per-runtime uTexture0 Y-flip, so the sampled
// content lands upright regardless of this pass-through.
//
// Unlike zone.vert this does NOT #include <common.glsl> and emits NO vFragCoord:
// the surface fragment contract reads pixel coordinates via surfacePixel(uv)
// (driven by uSurfaceSize), not a vertex-supplied vFragCoord varying.

#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = texCoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
