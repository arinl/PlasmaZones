// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/phosphorsurface_export.h>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace PhosphorSurfaceShaders {

/**
 * @brief Metadata for a single surface shader effect.
 *
 * Distinct from `PhosphorAnimationShaders::AnimationShaderEffect` (which
 * describes a finite-duration *transition* driven by a 0..1 timeline)
 * and from `PhosphorRendering::ShaderEffect` (which renders persistent
 * zone backgrounds). A `SurfaceShaderEffect` describes a persistent
 * per-window *surface layer* — the decoration band / border, rounded
 * corners, focus tint, etc. — composited over the live window content
 * for as long as the window is mapped.
 *
 * ## Identity
 *
 * Each effect is keyed by a stable `id` string (e.g. "border",
 * "rounded-corners"). Plugin-authored effects use reverse-domain or
 * namespaced ids ("myplugin.glow") to avoid collisions with built-in
 * effects.
 *
 * ## Shader source
 *
 * Each effect carries a single `fragmentShaderPath` — the same GLSL
 * source runs on both the compositor (KWin) and daemon (Qt RHI) surface-
 * layer paint paths; the runtime handles texture binding and coordinate
 * conventions, so separate per-backend shader variants are not needed.
 *
 * ## Parameters
 *
 * Each effect declares named parameters with type, default, min/max.
 * Field names mirror `AnimationShaderEffect::ParameterInfo` so surface
 * packs and animation packs can share QML editor components.
 *
 * ## Trimmed vs AnimationShaderEffect
 *
 * Surface shaders are single-pass by construction — they composite one
 * layer onto the live surface and never run intermediate buffer passes.
 * This struct therefore OMITS the multipass / buffer / wallpaper / depth
 * / fboExtent / event fields that `AnimationShaderEffect` carries, while
 * keeping the identical identity, shader-path, preview, parameter, and
 * texture-slot shape.
 */
struct PHOSPHORSURFACE_EXPORT SurfaceShaderEffect
{
    /// Stable identifier — lookup key in the registry.
    QString id;

    /// Human-readable display name (localizable by the consumer).
    QString name;

    /// One-line description for settings UI tooltips.
    QString description;

    /// Author attribution.
    QString author;

    /// Semantic version of this effect pack.
    QString version;

    /// Category for settings-UI grouping (e.g. "Border", "Corners", "Tint").
    QString category;

    /// Path to the fragment shader (relative to the effect dir). The same
    /// source is used on both the compositor and daemon paths — the
    /// runtime handles backend differences.
    QString fragmentShaderPath;

    /// Path to the vertex shader (relative to the effect dir). Empty = use
    /// the runtime's built-in fullscreen-quad vertex shader.
    QString vertexShaderPath;

    /// Resolved absolute directory containing this effect's assets.
    QString sourceDir;

    /// Whether this effect was loaded from a user-local directory.
    bool isUserEffect = false;

    /// Preview image path (relative to the effect dir). For settings UI.
    QString previewPath;

    /// Declared shader inputs beyond the standard surface set
    /// (uTexture0, uSurfaceSize, uSurfaceColor, etc.). Each entry maps
    /// `parameterId → { type, default, min, max, ... }`. Field names
    /// mirror the regular shader pack format
    /// (`AnimationShaderEffect::ParameterInfo` /
    /// `PhosphorRendering::ShaderRegistry::ParameterInfo`) so surface
    /// packs and overlay/animation packs can share QML editor components.
    ///
    /// **C++ field name vs JSON key asymmetry**: the QVariant fields are
    /// suffixed `Value` because `default` is a C++ keyword; the wire-
    /// format and QML-facing keys are the bare forms
    /// (`default`/`min`/`max`/`step`). See `toJson()` / `fromJson()` in
    /// `surfaceshadereffect.cpp` for the mapping.
    struct ParameterInfo
    {
        QString id;
        QString name;
        QString type; ///< "float", "int", "bool", "color"
        QString description; ///< Optional one-line tooltip for the settings UI.
        QString group; ///< Optional accordion group name for the settings UI.
        QVariant defaultValue; ///< JSON/QML key: `default`.
        QVariant minValue; ///< JSON/QML key: `min`.
        QVariant maxValue; ///< JSON/QML key: `max`.
        QVariant stepValue; ///< Optional slider step; JSON/QML key: `step`. QML falls back to (max-min)/200.
    };
    QList<ParameterInfo> parameters;

    /// User texture slot. Each entry binds an asset file to one of the
    /// runtime's user-texture samplers (slot 0 / 1 / 2 here). The
    /// captured surface (`uTexture0`) is never user-declared.
    ///
    /// `path` is resolved relative to the effect's `sourceDir`. Loading
    /// failures are non-fatal — the effect still installs and the
    /// affected sampler reads transparent black; a `qCWarning` logs the
    /// missing file. `wrap` accepts only `"clamp"`, `"repeat"`,
    /// `"mirror"`, and the empty string (which selects the runtime
    /// default of clamp). Any other value is rejected by `fromJson` with
    /// a `qCWarning` and stored as empty. Up to three textures per
    /// effect; surplus entries are silently dropped at parse time.
    struct TextureSlot
    {
        QString path; ///< Filename relative to the effect's sourceDir.
        QString
            wrap; ///< "clamp" / "repeat" / "mirror"; empty = runtime default. Other values are rejected by fromJson.

        bool operator==(const TextureSlot& other) const
        {
            return path == other.path && wrap == other.wrap;
        }
        bool operator!=(const TextureSlot& other) const
        {
            return !(*this == other);
        }
    };
    QList<TextureSlot> textures;

    bool isValid() const
    {
        return !id.isEmpty() && !fragmentShaderPath.isEmpty();
    }

    QJsonObject toJson() const;
    static SurfaceShaderEffect fromJson(const QJsonObject& obj);

    bool operator==(const SurfaceShaderEffect& other) const;
    bool operator!=(const SurfaceShaderEffect& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorSurfaceShaders

// Mark TextureSlot as relocatable so QList can move-construct entries
// in-place during reallocation rather than running the copy/move ctor
// per element. The struct is two QStrings; QString itself is already
// Q_RELOCATABLE_TYPE, so the aggregate is safely bitwise-relocatable.
// Must sit at file scope outside the namespace per Qt convention.
Q_DECLARE_TYPEINFO(PhosphorSurfaceShaders::SurfaceShaderEffect::TextureSlot, Q_RELOCATABLE_TYPE);
