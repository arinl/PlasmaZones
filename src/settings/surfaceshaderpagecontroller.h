// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorSurfaceShaders {
class SurfaceShaderRegistry;
}

namespace PlasmaZones {

class ISettings;

/// Q_PROPERTY / Q_INVOKABLE surface for the "Surface" (window-decoration)
/// shader settings page.
///
/// ## Scope: GLOBAL (one pack for all decorated windows)
///
/// Unlike `AnimationsPageController`, which assigns a shader PER EVENT via
/// a `ShaderProfileTree`, the surface page selects exactly ONE surface
/// shader pack that applies to every decorated window, plus a per-pack
/// parameter override map. The selection persists through two
/// `ISettings` Q_PROPERTYs (`surfaceShaderEffectId` /
/// `surfaceShaderParameters`) — no per-event tree, no staging snapshot.
///
/// ## Dirty tracking
///
/// Selection writes go STRAIGHT to `ISettings`. Both Settings properties
/// carry NOTIFY signals, which `SettingsController`'s meta-object loop
/// (over every Settings Q_PROPERTY NOTIFY) routes into
/// `onSettingsPropertyChanged` → the active page's dirty flag. The
/// controller therefore needs NO per-page staged state — `isDirty()` /
/// `apply()` / `discard()` are no-ops, exactly like
/// `GeneralPageController`. Apply / Discard / Defaults are driven globally
/// by `SettingsController::save()` / `load()` / `defaults()`.
class SurfaceShaderPageController : public PhosphorControl::PageController
{
    Q_OBJECT

public:
    /// @param registry Optional — when null, all `*ShaderEffects()` /
    ///        `shaderParameters()` Q_INVOKABLEs return empty results so
    ///        unit tests can construct the controller without a surface
    ///        bootstrap.
    /// @param settings Optional — when null, selection getters return
    ///        defaults and setters are no-ops.
    explicit SurfaceShaderPageController(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry = nullptr,
                                         ISettings* settings = nullptr, QObject* parent = nullptr);
    ~SurfaceShaderPageController() override;

    // ── PhosphorControl::StagingDomain contract ───────────────────────────
    // No per-page staged state — selection writes straight to Settings and
    // the global SettingsController dirty loop tracks them (see class doc).
    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }

    // ── Available packs ───────────────────────────────────────────────────

    /// Installed `SurfaceShaderEffect`s flattened to a QML-friendly list.
    /// Each row: id / name / description / author / version / category /
    /// isUserEffect / previewPath / parameters (QVariantList of
    /// ParameterInfo maps).
    Q_INVOKABLE QVariantList availableShaderEffects() const;

    /// Single-effect lookup. Empty map when @p effectId is unknown.
    Q_INVOKABLE QVariantMap shaderEffectInfo(const QString& effectId) const;

    /// Just the parameters list for @p effectId — convenience for the
    /// parameter editor.
    Q_INVOKABLE QVariantList shaderParameters(const QString& effectId) const;

    // ── Global selection ──────────────────────────────────────────────────

    /// Currently selected surface-shader pack id.
    Q_INVOKABLE QString currentShaderEffectId() const;

    /// Select @p id as the global surface shader pack. Writes straight to
    /// Settings (whose NOTIFY trips the SettingsController dirty loop).
    Q_INVOKABLE void setShaderEffectId(const QString& id);

    /// Current per-pack parameter override map (paramId -> value).
    Q_INVOKABLE QVariantMap currentShaderParams() const;

    /// Set one parameter override. Copies the map, sets @p paramId to
    /// @p value, writes the whole map back through Settings.
    Q_INVOKABLE void setShaderParam(const QString& paramId, const QVariant& value);

    /// Clear every parameter override (back to pack defaults).
    Q_INVOKABLE void resetShaderParams();

    // ── User shader directory ─────────────────────────────────────────────

    /// XDG-writable user surface-shader directory path. Internal helper —
    /// QML surfaces an "Open Folder" button that calls
    /// `openUserShaderDirectory()` rather than displaying the raw path.
    QString userShaderDirectoryPath() const;

    /// Ensure the user surface-shader directory exists; create if missing.
    /// @return true when the directory exists afterwards.
    Q_INVOKABLE bool ensureUserShaderDirectory();

    /// Open the user surface-shader directory in the system file manager,
    /// creating it first if missing.
    Q_INVOKABLE void openUserShaderDirectory();

Q_SIGNALS:
    /// Re-emit of `SurfaceShaderRegistry::effectsChanged` so QML can rebind
    /// without poking at the registry directly.
    void shaderEffectsChanged();

    /// Emitted whenever the global selection (pack id or parameters)
    /// changes so QML rebinds the picker + parameter editor.
    void selectionChanged();

private:
    PhosphorSurfaceShaders::SurfaceShaderRegistry* m_registry = nullptr;
    ISettings* m_settings = nullptr;
};

} // namespace PlasmaZones
