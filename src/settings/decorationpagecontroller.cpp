// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "decorationpagecontroller.h"

#include "decoration_controller_detail.h"

#include "../core/isettings.h"

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorSurface/DecorationSupportedPaths.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QColor>
#include <QLatin1String>

namespace PlasmaZones {

using namespace decoration_controller_detail;
using PhosphorSurfaceShaders::DecorationProfile;
using PhosphorSurfaceShaders::DecorationProfileTree;

namespace {

/// Read the current tree from Settings (empty tree when no settings).
DecorationProfileTree readTree(ISettings* settings)
{
    return settings ? settings->decorationProfileTree() : DecorationProfileTree{};
}

/// Read the DIRECT profile at @p path: the baseline for the empty path,
/// otherwise the per-surface override (default-constructed = all-inherit
/// when there is no override).
DecorationProfile directProfileAt(const DecorationProfileTree& tree, const QString& path)
{
    return path.isEmpty() ? tree.baseline() : tree.directOverride(path);
}

/// Write @p profile back as the DIRECT profile at @p path (baseline for the
/// empty path), then persist the whole tree through Settings.
void writeDirectProfile(ISettings* settings, DecorationProfileTree& tree, const QString& path,
                        const DecorationProfile& profile)
{
    if (!settings)
        return;
    if (path.isEmpty())
        tree.setBaseline(profile);
    else
        tree.setOverride(path, profile);
    // Settings::setDecorationProfileTree short-circuits on an unchanged tree
    // (and only then emits NOTIFY), so a redundant write does not trip the
    // dirty loop. profilesChanged() fires from the decorationProfileTreeChanged
    // connection wired in the ctor, covering both our own writes and reloads.
    settings->setDecorationProfileTree(tree);
}

} // namespace

DecorationPageController::DecorationPageController(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry,
                                                   ISettings* settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("decoration"), parent)
    , m_registry(registry)
    , m_settings(settings)
{
    if (m_registry) {
        connect(m_registry, &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this,
                &DecorationPageController::shaderEffectsChanged);
    }
    if (m_settings) {
        // Re-fire profilesChanged so every visible card rebinds after a
        // global reload (Discard / Settings::load()) AND after our own
        // mutators write the tree back.
        connect(m_settings, &ISettings::decorationProfileTreeChanged, this, &DecorationPageController::profilesChanged);
    }
}

DecorationPageController::~DecorationPageController() = default;

// ── Available packs ───────────────────────────────────────────────────────

QVariantList DecorationPageController::availableShaderEffects() const
{
    QVariantList result;
    if (!m_registry)
        return result;
    const auto effects = m_registry->availableEffects();
    result.reserve(effects.size());
    for (const auto& effect : effects)
        result.append(effectToMap(effect));
    return result;
}

QVariantMap DecorationPageController::shaderEffectInfo(const QString& effectId) const
{
    if (!m_registry || effectId.isEmpty() || !m_registry->hasEffect(effectId))
        return {};
    return effectToMap(m_registry->effect(effectId));
}

QVariantList DecorationPageController::shaderParameters(const QString& effectId) const
{
    if (!m_registry || effectId.isEmpty() || !m_registry->hasEffect(effectId))
        return {};
    const auto effect = m_registry->effect(effectId);
    QVariantList result;
    result.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters)
        result.append(parameterInfoToMap(p));
    return result;
}

// ── Profile readers ─────────────────────────────────────────────────────────

QVariantMap DecorationPageController::resolvedProfile(const QString& path) const
{
    const DecorationProfileTree tree = readTree(m_settings);
    // resolve("") returns the baseline (the walk terminates at "" with the
    // baseline as the accumulator), so the empty-path case is handled
    // directly by the tree's own resolve.
    return profileToResolvedMap(tree.resolve(path));
}

QVariantMap DecorationPageController::rawProfile(const QString& path) const
{
    const DecorationProfileTree tree = readTree(m_settings);
    return profileToSparseMap(directProfileAt(tree, path));
}

bool DecorationPageController::hasOverride(const QString& path) const
{
    if (path.isEmpty())
        return false;
    if (!PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return false;
    return readTree(m_settings).hasOverride(path);
}

// ── Chain mutators ───────────────────────────────────────────────────────────

QStringList DecorationPageController::chainAt(const QString& path) const
{
    return readTree(m_settings).resolve(path).effectiveChain();
}

void DecorationPageController::setChain(const QString& path, const QStringList& chain)
{
    if (!m_settings)
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = readTree(m_settings);
    DecorationProfile profile = directProfileAt(tree, path);
    profile.chain = chain;
    writeDirectProfile(m_settings, tree, path, profile);
}

void DecorationPageController::setChainParam(const QString& path, const QString& packId, const QString& paramId,
                                             const QVariant& value)
{
    if (!m_settings || packId.isEmpty() || paramId.isEmpty())
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = readTree(m_settings);
    DecorationProfile profile = directProfileAt(tree, path);
    // Copy-mutate the {packId -> {paramId -> value}} two-level map, engaging
    // the parameters optional if it was inherited.
    QVariantMap params = profile.parameters.value_or(QVariantMap());
    QVariantMap packParams = params.value(packId).toMap();
    packParams.insert(paramId, value);
    params.insert(packId, packParams);
    profile.parameters = params;
    writeDirectProfile(m_settings, tree, path, profile);
}

void DecorationPageController::clearChainParams(const QString& path)
{
    if (!m_settings)
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = readTree(m_settings);
    DecorationProfile profile = directProfileAt(tree, path);
    // Engaged-but-empty map = explicitly "use all pack defaults" (distinct
    // from nullopt = inherit the parent's parameter overrides).
    profile.parameters = QVariantMap();
    writeDirectProfile(m_settings, tree, path, profile);
}

// ── Border/titlebar field mutators ────────────────────────────────────────────

void DecorationPageController::setBorderField(const QString& path, const QString& fieldName, const QVariant& value)
{
    if (!m_settings || fieldName.isEmpty())
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    using DP = DecorationProfile;
    DecorationProfileTree tree = readTree(m_settings);
    DecorationProfile profile = directProfileAt(tree, path);

    bool engaged = true;
    if (fieldName == QLatin1String(DP::JsonFieldBorderWidth))
        profile.borderWidth = value.toInt();
    else if (fieldName == QLatin1String(DP::JsonFieldBorderRadius))
        profile.borderRadius = value.toInt();
    else if (fieldName == QLatin1String(DP::JsonFieldActiveColor))
        profile.activeColor = value.value<QColor>();
    else if (fieldName == QLatin1String(DP::JsonFieldInactiveColor))
        profile.inactiveColor = value.value<QColor>();
    else if (fieldName == QLatin1String(DP::JsonFieldUseSystemColors))
        profile.useSystemColors = value.toBool();
    else if (fieldName == QLatin1String(DP::JsonFieldShowBorder))
        profile.showBorder = value.toBool();
    else if (fieldName == QLatin1String(DP::JsonFieldHideTitlebar))
        profile.hideTitlebar = value.toBool();
    else
        engaged = false; // unknown field — ignore

    if (engaged)
        writeDirectProfile(m_settings, tree, path, profile);
}

void DecorationPageController::clearBorderField(const QString& path, const QString& fieldName)
{
    if (!m_settings || fieldName.isEmpty())
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    using DP = DecorationProfile;
    DecorationProfileTree tree = readTree(m_settings);
    DecorationProfile profile = directProfileAt(tree, path);

    bool known = true;
    if (fieldName == QLatin1String(DP::JsonFieldBorderWidth))
        profile.borderWidth.reset();
    else if (fieldName == QLatin1String(DP::JsonFieldBorderRadius))
        profile.borderRadius.reset();
    else if (fieldName == QLatin1String(DP::JsonFieldActiveColor))
        profile.activeColor.reset();
    else if (fieldName == QLatin1String(DP::JsonFieldInactiveColor))
        profile.inactiveColor.reset();
    else if (fieldName == QLatin1String(DP::JsonFieldUseSystemColors))
        profile.useSystemColors.reset();
    else if (fieldName == QLatin1String(DP::JsonFieldShowBorder))
        profile.showBorder.reset();
    else if (fieldName == QLatin1String(DP::JsonFieldHideTitlebar))
        profile.hideTitlebar.reset();
    else
        known = false;

    if (known)
        writeDirectProfile(m_settings, tree, path, profile);
}

// ── Whole-override mutator ────────────────────────────────────────────────────

bool DecorationPageController::clearOverride(const QString& path)
{
    // The baseline can't be "inherited away" — reject the empty path. QML
    // disables the reset affordance for the global card, but guard here too.
    if (!m_settings || path.isEmpty())
        return false;
    DecorationProfileTree tree = readTree(m_settings);
    if (!tree.clearOverride(path))
        return false;
    m_settings->setDecorationProfileTree(tree);
    return true;
}

} // namespace PlasmaZones
