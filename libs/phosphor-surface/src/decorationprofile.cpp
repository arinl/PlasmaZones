// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/DecorationProfile.h>

#include <QJsonArray>
#include <QJsonValue>

namespace PhosphorSurfaceShaders {

DecorationProfile DecorationProfile::withDefaults() const
{
    DecorationProfile out = *this;
    if (!out.chain)
        out.chain = QStringList();
    if (!out.parameters)
        out.parameters = QVariantMap();
    if (!out.borderWidth)
        out.borderWidth = 0;
    if (!out.borderRadius)
        out.borderRadius = 0;
    if (!out.activeColor)
        out.activeColor = QColor();
    if (!out.inactiveColor)
        out.inactiveColor = QColor();
    if (!out.useSystemColors)
        out.useSystemColors = false;
    if (!out.showBorder)
        out.showBorder = false;
    if (!out.hideTitlebar)
        out.hideTitlebar = false;
    return out;
}

QJsonObject DecorationProfile::toJson() const
{
    QJsonObject obj;
    if (chain) {
        QJsonArray chainArr;
        for (const QString& packId : *chain)
            chainArr.append(packId);
        obj.insert(QLatin1String(JsonFieldChain), chainArr);
    }
    if (parameters) {
        QJsonObject paramsObj;
        for (auto it = parameters->constBegin(); it != parameters->constEnd(); ++it)
            paramsObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
        obj.insert(QLatin1String(JsonFieldParameters), paramsObj);
    }
    if (borderWidth)
        obj.insert(QLatin1String(JsonFieldBorderWidth), *borderWidth);
    if (borderRadius)
        obj.insert(QLatin1String(JsonFieldBorderRadius), *borderRadius);
    if (activeColor)
        obj.insert(QLatin1String(JsonFieldActiveColor), activeColor->name(QColor::HexArgb));
    if (inactiveColor)
        obj.insert(QLatin1String(JsonFieldInactiveColor), inactiveColor->name(QColor::HexArgb));
    if (useSystemColors)
        obj.insert(QLatin1String(JsonFieldUseSystemColors), *useSystemColors);
    if (showBorder)
        obj.insert(QLatin1String(JsonFieldShowBorder), *showBorder);
    if (hideTitlebar)
        obj.insert(QLatin1String(JsonFieldHideTitlebar), *hideTitlebar);
    return obj;
}

DecorationProfile DecorationProfile::fromJson(const QJsonObject& obj)
{
    DecorationProfile p;

    if (obj.contains(QLatin1String(JsonFieldChain))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldChain));
        if (v.isArray()) {
            QStringList chain;
            const QJsonArray arr = v.toArray();
            for (const QJsonValue& entry : arr)
                chain.append(entry.toString());
            p.chain = std::move(chain);
        }
    }

    if (obj.contains(QLatin1String(JsonFieldParameters))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldParameters));
        if (v.isObject()) {
            QVariantMap params;
            const QJsonObject paramsObj = v.toObject();
            for (auto it = paramsObj.constBegin(); it != paramsObj.constEnd(); ++it)
                params.insert(it.key(), it.value().toVariant());
            p.parameters = std::move(params);
        }
    }

    if (obj.contains(QLatin1String(JsonFieldBorderWidth))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldBorderWidth));
        if (v.isDouble())
            p.borderWidth = v.toInt();
    }

    if (obj.contains(QLatin1String(JsonFieldBorderRadius))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldBorderRadius));
        if (v.isDouble())
            p.borderRadius = v.toInt();
    }

    if (obj.contains(QLatin1String(JsonFieldActiveColor))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldActiveColor));
        if (v.isString())
            p.activeColor = QColor(v.toString());
    }

    if (obj.contains(QLatin1String(JsonFieldInactiveColor))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldInactiveColor));
        if (v.isString())
            p.inactiveColor = QColor(v.toString());
    }

    if (obj.contains(QLatin1String(JsonFieldUseSystemColors))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldUseSystemColors));
        if (v.isBool())
            p.useSystemColors = v.toBool();
    }

    if (obj.contains(QLatin1String(JsonFieldShowBorder))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldShowBorder));
        if (v.isBool())
            p.showBorder = v.toBool();
    }

    if (obj.contains(QLatin1String(JsonFieldHideTitlebar))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldHideTitlebar));
        if (v.isBool())
            p.hideTitlebar = v.toBool();
    }

    return p;
}

void DecorationProfile::overlay(DecorationProfile& dst, const DecorationProfile& src)
{
    if (src.chain)
        dst.chain = src.chain;
    if (src.parameters)
        dst.parameters = src.parameters;
    if (src.borderWidth)
        dst.borderWidth = src.borderWidth;
    if (src.borderRadius)
        dst.borderRadius = src.borderRadius;
    if (src.activeColor)
        dst.activeColor = src.activeColor;
    if (src.inactiveColor)
        dst.inactiveColor = src.inactiveColor;
    if (src.useSystemColors)
        dst.useSystemColors = src.useSystemColors;
    if (src.showBorder)
        dst.showBorder = src.showBorder;
    if (src.hideTitlebar)
        dst.hideTitlebar = src.hideTitlebar;
}

bool DecorationProfile::operator==(const DecorationProfile& other) const
{
    return chain == other.chain && parameters == other.parameters && borderWidth == other.borderWidth
        && borderRadius == other.borderRadius && activeColor == other.activeColor
        && inactiveColor == other.inactiveColor && useSystemColors == other.useSystemColors
        && showBorder == other.showBorder && hideTitlebar == other.hideTitlebar;
}

} // namespace PhosphorSurfaceShaders
