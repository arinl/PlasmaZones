// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "surfaceshaderpagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/isettings.h"
#include "../core/logging.h"

#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QDesktopServices>
#include <QDir>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUrl>

namespace PlasmaZones {

namespace {

/// Convert a surface-pack ParameterInfo to a QVariantMap. Keys mirror
/// `animations_controller_detail::parameterInfoToMap` (and
/// `PhosphorRendering::ShaderRegistry::parameterInfoToVariantMap`) so the
/// shared QML editor components consume animation, overlay, and surface
/// packs identically. Optional fields are emitted only when valid/non-empty.
QVariantMap parameterInfoToMap(const PhosphorSurfaceShaders::SurfaceShaderEffect::ParameterInfo& p)
{
    QVariantMap m;
    m.insert(QLatin1String("id"), p.id);
    m.insert(QLatin1String("name"), p.name);
    m.insert(QLatin1String("type"), p.type);
    if (!p.description.isEmpty())
        m.insert(QLatin1String("description"), p.description);
    if (!p.group.isEmpty())
        m.insert(QLatin1String("group"), p.group);
    if (p.defaultValue.isValid())
        m.insert(QLatin1String("default"), p.defaultValue);
    if (p.minValue.isValid())
        m.insert(QLatin1String("min"), p.minValue);
    if (p.maxValue.isValid())
        m.insert(QLatin1String("max"), p.maxValue);
    if (p.stepValue.isValid())
        m.insert(QLatin1String("step"), p.stepValue);
    return m;
}

QVariantMap effectToMap(const PhosphorSurfaceShaders::SurfaceShaderEffect& effect)
{
    QVariantMap m;
    m.insert(QLatin1String("id"), effect.id);
    m.insert(QLatin1String("name"), effect.name);
    m.insert(QLatin1String("description"), effect.description);
    m.insert(QLatin1String("author"), effect.author);
    m.insert(QLatin1String("version"), effect.version);
    m.insert(QLatin1String("category"), effect.category);
    m.insert(QLatin1String("isUserEffect"), effect.isUserEffect);
    // previewPath is resolved to an absolute path by the registry's loader,
    // so QML can pass it to Image.source (with a file:// prefix). Empty when
    // the pack shipped no preview — the page renders a placeholder.
    m.insert(QLatin1String("previewPath"), effect.previewPath);
    QVariantList params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters)
        params.append(parameterInfoToMap(p));
    m.insert(QLatin1String("parameters"), params);
    return m;
}

} // namespace

SurfaceShaderPageController::SurfaceShaderPageController(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry,
                                                         ISettings* settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("surface-shaders"), parent)
    , m_registry(registry)
    , m_settings(settings)
{
    if (m_registry) {
        connect(m_registry, &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this,
                &SurfaceShaderPageController::shaderEffectsChanged);
    }
    if (m_settings) {
        // Re-fire selectionChanged so the QML picker + parameter editor
        // rebind after a global reload (Discard / Settings::load()), not
        // just on this controller's own setters.
        connect(m_settings, &ISettings::surfaceShaderEffectIdChanged, this,
                &SurfaceShaderPageController::selectionChanged);
        connect(m_settings, &ISettings::surfaceShaderParametersChanged, this,
                &SurfaceShaderPageController::selectionChanged);
    }
}

SurfaceShaderPageController::~SurfaceShaderPageController() = default;

QVariantList SurfaceShaderPageController::availableShaderEffects() const
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

QVariantMap SurfaceShaderPageController::shaderEffectInfo(const QString& effectId) const
{
    if (!m_registry || effectId.isEmpty() || !m_registry->hasEffect(effectId))
        return {};
    return effectToMap(m_registry->effect(effectId));
}

QVariantList SurfaceShaderPageController::shaderParameters(const QString& effectId) const
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

QString SurfaceShaderPageController::currentShaderEffectId() const
{
    return m_settings ? m_settings->surfaceShaderEffectId() : ConfigDefaults::surfaceShaderEffectId();
}

void SurfaceShaderPageController::setShaderEffectId(const QString& id)
{
    if (!m_settings)
        return;
    // Settings::setSurfaceShaderEffectId already short-circuits when the
    // value is unchanged (and only then emits NOTIFY), so selectionChanged
    // fires exactly once per real change via the surfaceShaderEffectIdChanged
    // connection in the ctor.
    m_settings->setSurfaceShaderEffectId(id);
}

QVariantMap SurfaceShaderPageController::currentShaderParams() const
{
    return m_settings ? m_settings->surfaceShaderParameters() : QVariantMap{};
}

void SurfaceShaderPageController::setShaderParam(const QString& paramId, const QVariant& value)
{
    if (!m_settings || paramId.isEmpty())
        return;
    QVariantMap next = m_settings->surfaceShaderParameters();
    next.insert(paramId, value);
    // Settings::setSurfaceShaderParameters short-circuits on an unchanged map,
    // so a redundant write (e.g. re-setting an existing identical value) does
    // not trip the dirty loop.
    m_settings->setSurfaceShaderParameters(next);
}

void SurfaceShaderPageController::resetShaderParams()
{
    if (!m_settings)
        return;
    m_settings->setSurfaceShaderParameters(QVariantMap{});
}

QString SurfaceShaderPageController::userShaderDirectoryPath() const
{
    // cleanPath normalises away any stray double-slash so QDir /
    // QDesktopServices don't surface a confusing "directory not found".
    // Mirrors AnimationsPageController::userShaderDirectoryPath() with the
    // surface subdir.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userSurfaceSubdir());
}

bool SurfaceShaderPageController::ensureUserShaderDirectory()
{
    return QDir().mkpath(userShaderDirectoryPath());
}

void SurfaceShaderPageController::openUserShaderDirectory()
{
    const QString dir = userShaderDirectoryPath();
    if (!QDir().mkpath(dir)) {
        qCWarning(lcConfig) << "openUserShaderDirectory: mkpath failed for" << dir;
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

} // namespace PlasmaZones
