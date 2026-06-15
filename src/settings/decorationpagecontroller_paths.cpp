// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// DecorationPageController surface-taxonomy helpers — the small
// Q_INVOKABLE readers (surfaceLabel / parentChain) that translate decoration
// surface paths into UI taxonomy. Split out so decorationpagecontroller.cpp
// stays under the project's 800-line cap. Same class, separate TU, no API
// change. (The per-page surface lists live in the QML page models, mirroring
// the animation sub-pages — there is no C++ leaf-path accessor.)

#include "decoration_controller_detail.h"
#include "decorationpagecontroller.h"

#include <PhosphorSurface/DecorationSupportedPaths.h>

#include <QLatin1Char>

namespace PlasmaZones {

QString DecorationPageController::surfaceLabel(const QString& path) const
{
    if (path.isEmpty())
        return QStringLiteral("Global");
    // Category (parent-node) roots get an "All …" label so the parent-node
    // cards read "All Windows" / "All Popups", mirroring the animation
    // pages' "All Window Events" parent nodes. A dotted leaf path falls
    // through to its humanized last segment.
    if (path == QLatin1String("window"))
        return QStringLiteral("All Windows");
    if (path == QLatin1String("popup"))
        return QStringLiteral("All Popups");
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
