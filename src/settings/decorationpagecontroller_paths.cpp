// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// DecorationPageController surface-taxonomy helpers — the small
// Q_INVOKABLE readers (surfaceLeafPaths / surfaceLabel / parentChain)
// that translate decoration surface paths into UI taxonomy. Split out so
// decorationpagecontroller.cpp stays under the project's 800-line cap.
// Same class, separate TU, no API change.

#include "decoration_controller_detail.h"
#include "decorationpagecontroller.h"

#include <PhosphorSurface/DecorationSupportedPaths.h>

#include <QLatin1Char>

namespace PlasmaZones {

QStringList DecorationPageController::surfaceLeafPaths() const
{
    return PhosphorSurfaceShaders::decorationLeafSurfacePaths();
}

QString DecorationPageController::surfaceLabel(const QString& path) const
{
    if (path.isEmpty())
        return QStringLiteral("Global");
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const QString segment = dot < 0 ? path : path.mid(dot + 1);
    return decoration_controller_detail::humanizeSegment(segment);
}

QStringList DecorationPageController::parentChain(const QString& path) const
{
    // Self + ancestors, deepest first, terminating at (but excluding) the
    // empty baseline. e.g. "window.tiled" -> ["window.tiled", "window"].
    QStringList chain;
    QString cur = path;
    while (!cur.isEmpty()) {
        chain.append(cur);
        const int dot = cur.lastIndexOf(QLatin1Char('.'));
        cur = (dot < 0) ? QString() : cur.left(dot);
    }
    return chain;
}

} // namespace PlasmaZones
