// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/phosphorsurface_export.h>

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace PhosphorSurfaceShaders {

/**
 * @brief Per-surface decoration shader-pack chain and border/titlebar appearance.
 *
 * Parallel to `PhosphorAnimationShaders::ShaderProfile` which configures a
 * single transition *effect* per event, `DecorationProfile` configures the
 * persistent *decoration* of a surface: an ordered chain of surface shader
 * packs (border, glow, …) plus the border/titlebar appearance that frames
 * the surface. Both are resolved through a separate tree
 * (`DecorationProfileTree`) over a dot-path surface namespace
 * (window.tiled, popup.snapAssist, …) so the two concerns evolve
 * independently.
 *
 * ## "Set" vs "unset" semantics
 *
 * Same contract as `ShaderProfile`: `std::optional` fields distinguish
 * "inherit from parent" (nullopt) from "I explicitly chose this value"
 * (engaged). A leaf with `borderWidth = 4` wins over a category that says
 * `borderWidth = 2`. A leaf with `borderWidth = std::nullopt` inherits
 * whatever the category or baseline says.
 *
 * An explicitly-empty `chain` (engaged optional containing an empty list)
 * means "no decoration shader packs for this surface" — the surface keeps
 * its border/titlebar appearance but runs no surface shader. This lets a
 * child disable a parent's pack chain without disabling the parent's
 * border styling.
 */
class PHOSPHORSURFACE_EXPORT DecorationProfile
{
public:
    /// Ordered surface-pack ids, resolved by id from `SurfaceShaderRegistry`,
    /// e.g. {"border", "glow"}. `std::nullopt` = inherit. Engaged-but-empty
    /// list = explicitly no packs.
    std::optional<QStringList> chain;

    /// Per-pack parameter overrides. Shape: { packId -> { paramId -> value } }.
    /// Keys are pack ids from `chain`; inner keys are parameter ids declared
    /// in the pack's `SurfaceShaderEffect::parameters`. `std::nullopt` =
    /// inherit. Engaged-but-empty map = explicitly use all defaults.
    std::optional<QVariantMap> parameters;

    /// Border thickness in device-independent px. `std::nullopt` = inherit.
    std::optional<int> borderWidth;

    /// Corner radius in device-independent px. `std::nullopt` = inherit.
    std::optional<int> borderRadius;

    /// Active (focused) border/frame color. `std::nullopt` = inherit.
    std::optional<QColor> activeColor;

    /// Inactive (unfocused) border/frame color. `std::nullopt` = inherit.
    std::optional<QColor> inactiveColor;

    /// Pull active/inactive colors from the system color scheme instead of
    /// the explicit active/inactive colors. `std::nullopt` = inherit.
    std::optional<bool> useSystemColors;

    /// Whether to draw a border at all. `std::nullopt` = inherit.
    std::optional<bool> showBorder;

    /// Whether to hide the surface's titlebar. `std::nullopt` = inherit.
    std::optional<bool> hideTitlebar;

    // ─────── Effective getters ───────

    QStringList effectiveChain() const
    {
        return chain.value_or(QStringList());
    }
    QVariantMap effectiveParameters() const
    {
        return parameters.value_or(QVariantMap());
    }
    int effectiveBorderWidth() const
    {
        return borderWidth.value_or(0);
    }
    int effectiveBorderRadius() const
    {
        return borderRadius.value_or(0);
    }
    QColor effectiveActiveColor() const
    {
        return activeColor.value_or(QColor());
    }
    QColor effectiveInactiveColor() const
    {
        return inactiveColor.value_or(QColor());
    }
    bool effectiveUseSystemColors() const
    {
        return useSystemColors.value_or(false);
    }
    bool effectiveShowBorder() const
    {
        return showBorder.value_or(false);
    }
    bool effectiveHideTitlebar() const
    {
        return hideTitlebar.value_or(false);
    }

    DecorationProfile withDefaults() const;

    // ─────── Serialization ───────

    static constexpr auto JsonFieldChain = "chain";
    static constexpr auto JsonFieldParameters = "parameters";
    static constexpr auto JsonFieldBorderWidth = "borderWidth";
    static constexpr auto JsonFieldBorderRadius = "borderRadius";
    static constexpr auto JsonFieldActiveColor = "activeColor";
    static constexpr auto JsonFieldInactiveColor = "inactiveColor";
    static constexpr auto JsonFieldUseSystemColors = "useSystemColors";
    static constexpr auto JsonFieldShowBorder = "showBorder";
    static constexpr auto JsonFieldHideTitlebar = "hideTitlebar";

    QJsonObject toJson() const;
    static DecorationProfile fromJson(const QJsonObject& obj);

    // ─────── Overlay ───────

    /// Overlay @p src onto @p dst: every engaged field in src replaces
    /// the corresponding field in dst. Unset fields in src are skipped.
    static void overlay(DecorationProfile& dst, const DecorationProfile& src);

    // ─────── Equality ───────

    bool operator==(const DecorationProfile& other) const;
    bool operator!=(const DecorationProfile& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorSurfaceShaders
