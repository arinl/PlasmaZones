// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_decoration_profile_seed.cpp
 * @brief Unit tests for ConfigMigration::seedDecorationProfileTree — the
 *        per-surface decoration tree seeded from the legacy border/shader
 *        settings as part of the v3 -> v4 migration.
 *
 * seedDecorationProfileTree is a pure in-place QJsonObject transform (it does
 * not touch _version — its caller migrateV3ToV4 stamps the version), so these
 * tests call it directly. Config groups are stored nested by dot-segment
 * (Tiling -> Appearance -> Borders/Colors/Decorations; Surface at the root),
 * matching how the JSON backend persists and how the seed reads them via
 * groupObjectAtPath. The tests assert:
 *   - a config with no customised border/shader keys writes no
 *     Surface.DecorationProfileTree (the read-time default suffices),
 *   - the pack chain is the sole border on/off gate, honouring the legacy
 *     ShowBorder: an explicit (or, since ShowBorder defaults to false, an unset)
 *     OFF seeds an engaged-but-empty chain (no border), while ON engages the
 *     pack,
 *   - customised border width/radius/colours/hide-titlebar seed a `window`
 *     override under parameters["border"] with the exact pack param ids, on an
 *     empty baseline,
 *   - the border-appearance params are preserved even with the chain gated off,
 *     so a later re-enable restores the user's width/colours,
 *   - out-of-range width/radius are clamped to the same bounds the live read
 *     path enforces,
 *   - a corrupt colour string falls back to the default colour,
 *   - the seed is idempotent (a second run produces a byte-identical root),
 *   - a non-border selected pack seeds no stray border parameter block.
 */

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QVariantMap>

#include "../../../src/config/configdefaults.h"
#include "../../../src/config/configkeys.h"
#include "../../../src/config/configmigration.h"

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

using namespace PlasmaZones;
namespace DPS = PhosphorSurfaceShaders;

class TestDecorationProfileSeed : public QObject
{
    Q_OBJECT

private:
    using CD = ConfigDefaults;

    /// All-optional inputs; only the engaged fields are written, mirroring a
    /// real config that only persists keys the user actually touched.
    struct Inputs
    {
        std::optional<int> width;
        std::optional<int> radius;
        std::optional<bool> showBorder;
        std::optional<QString> active;
        std::optional<QString> inactive;
        std::optional<bool> useSystem;
        std::optional<bool> hideTitle;
        std::optional<QString> shaderEffectId;
    };

    /// Assemble a config root from @p in. The Borders/Colors/Decorations leaves
    /// nest under Tiling -> Appearance, and the shader id under the top-level
    /// Surface group — exactly the structure seedDecorationProfileTree walks.
    static QJsonObject buildRoot(const Inputs& in)
    {
        QJsonObject borders;
        if (in.width)
            borders[CD::widthKey()] = *in.width;
        if (in.radius)
            borders[CD::radiusKey()] = *in.radius;
        if (in.showBorder)
            borders[CD::showBorderKey()] = *in.showBorder;

        QJsonObject colors;
        if (in.active)
            colors[CD::activeKey()] = *in.active;
        if (in.inactive)
            colors[CD::inactiveKey()] = *in.inactive;
        if (in.useSystem)
            colors[CD::useSystemKey()] = *in.useSystem;

        QJsonObject decorations;
        if (in.hideTitle)
            decorations[CD::hideTitleBarsKey()] = *in.hideTitle;

        QJsonObject appearance;
        if (!borders.isEmpty())
            appearance[QStringLiteral("Borders")] = borders;
        if (!colors.isEmpty())
            appearance[QStringLiteral("Colors")] = colors;
        if (!decorations.isEmpty())
            appearance[QStringLiteral("Decorations")] = decorations;

        QJsonObject root;
        if (!appearance.isEmpty()) {
            QJsonObject tiling;
            tiling[QStringLiteral("Appearance")] = appearance;
            root[CD::tilingGroup()] = tiling;
        }
        if (in.shaderEffectId) {
            QJsonObject surface;
            surface[CD::surfaceShaderEffectIdKey()] = *in.shaderEffectId;
            root[CD::surfaceGroup()] = surface;
        }
        return root;
    }

    static bool hasTreeKey(const QJsonObject& root)
    {
        return root.value(CD::surfaceGroup()).toObject().contains(CD::surfaceDecorationTreeKey());
    }

    static DPS::DecorationProfileTree treeFromRoot(const QJsonObject& root)
    {
        const QString json = root.value(CD::surfaceGroup()).toObject().value(CD::surfaceDecorationTreeKey()).toString();
        return DPS::DecorationProfileTree::fromJson(QJsonDocument::fromJson(json.toUtf8()).object());
    }

    static QVariantMap borderParamsOf(const DPS::DecorationProfile& profile)
    {
        return profile.effectiveParameters().value(CD::surfaceShaderEffectId()).toMap();
    }

private Q_SLOTS:
    void testNoCustom_writesNoTree()
    {
        QJsonObject root;
        ConfigMigration::seedDecorationProfileTree(root);
        QVERIFY2(!hasTreeKey(root), "a clean config must not write a redundant DecorationProfileTree");
    }

    void testCustomBorder_seedsWindowOverrideOnEmptyBaseline()
    {
        Inputs in;
        in.width = 6;
        in.radius = 8;
        in.showBorder = true;
        in.active = QStringLiteral("#ff112233");
        in.inactive = QStringLiteral("#ff445566");
        in.useSystem = false;
        in.hideTitle = true;
        in.shaderEffectId = CD::surfaceShaderEffectId();
        QJsonObject root = buildRoot(in);

        ConfigMigration::seedDecorationProfileTree(root);
        QVERIFY(hasTreeKey(root));

        const DPS::DecorationProfileTree tree = treeFromRoot(root);
        // Window decoration is a `window`-path override, not the baseline, so
        // daemon surfaces (osd/popup) inherit no window decoration.
        QCOMPARE(tree.baseline(), DPS::DecorationProfile{});
        QVERIFY(tree.hasOverride(QStringLiteral("window")));
        const DPS::DecorationProfile window = tree.directOverride(QStringLiteral("window"));

        QCOMPARE(window.effectiveChain(), QStringList{CD::surfaceShaderEffectId()});
        QVERIFY(window.hideTitlebar.has_value());
        QCOMPARE(*window.hideTitlebar, true);

        const QVariantMap params = borderParamsOf(window);
        QCOMPARE(params.value(QStringLiteral("borderWidth")).toInt(), 6);
        QCOMPARE(params.value(QStringLiteral("cornerRadius")).toInt(), 8);
        QCOMPARE(params.value(QStringLiteral("useSystemAccent")).toBool(), false);
        QCOMPARE(params.value(QStringLiteral("activeColor")).toString(),
                 QColor(QStringLiteral("#ff112233")).name(QColor::HexArgb));
        QCOMPARE(params.value(QStringLiteral("inactiveColor")).toString(),
                 QColor(QStringLiteral("#ff445566")).name(QColor::HexArgb));
    }

    void testOutOfRange_clamped()
    {
        Inputs in;
        in.width = 99999;
        in.radius = -50;
        in.showBorder = true;
        in.shaderEffectId = CD::surfaceShaderEffectId();
        QJsonObject root = buildRoot(in);

        ConfigMigration::seedDecorationProfileTree(root);

        const QVariantMap params = borderParamsOf(treeFromRoot(root).directOverride(QStringLiteral("window")));
        QCOMPARE(params.value(QStringLiteral("borderWidth")).toInt(), CD::autotileBorderWidthMax());
        QCOMPARE(params.value(QStringLiteral("cornerRadius")).toInt(), CD::autotileBorderRadiusMin());
    }

    void testInvalidColor_fallsBackToDefault()
    {
        Inputs in;
        in.showBorder = true;
        in.active = QStringLiteral("totally-not-a-colour");
        in.inactive = QStringLiteral("#ff445566");
        QJsonObject root = buildRoot(in);

        ConfigMigration::seedDecorationProfileTree(root);

        const QVariantMap params = borderParamsOf(treeFromRoot(root).directOverride(QStringLiteral("window")));
        QCOMPARE(params.value(QStringLiteral("activeColor")).toString(),
                 CD::autotileBorderColor().name(QColor::HexArgb));
        // The valid sibling colour is preserved (the fallback is per-field).
        QCOMPARE(params.value(QStringLiteral("inactiveColor")).toString(),
                 QColor(QStringLiteral("#ff445566")).name(QColor::HexArgb));
    }

    void testIdempotent_secondRunByteIdentical()
    {
        Inputs in;
        in.width = 6;
        in.showBorder = true;
        in.shaderEffectId = CD::surfaceShaderEffectId();
        QJsonObject root = buildRoot(in);

        ConfigMigration::seedDecorationProfileTree(root);
        const QJsonObject afterFirst = root;

        // The seed reads the (still-present) source keys and overwrites the tree
        // deterministically, so a second run reproduces the same root exactly.
        ConfigMigration::seedDecorationProfileTree(root);
        QCOMPARE(root, afterFirst);
    }

    void testNonBorderPack_noStrayBorderParams()
    {
        Inputs in;
        in.width = 6; // border-appearance customisation present...
        in.showBorder = true; // ...border is ON so the chain engages the pack...
        in.shaderEffectId = QStringLiteral("glow"); // ...but a non-border pack is selected.
        QJsonObject root = buildRoot(in);

        ConfigMigration::seedDecorationProfileTree(root);

        const DPS::DecorationProfile window = treeFromRoot(root).directOverride(QStringLiteral("window"));
        QCOMPARE(window.effectiveChain(), QStringList{QStringLiteral("glow")});
        QVERIFY2(!window.effectiveParameters().contains(CD::surfaceShaderEffectId()),
                 "border params must not be filed under a chain that does not include the border pack");
    }

    void testPartialCustom_omittedKeysUseDefaults()
    {
        // Width + ShowBorder are customised (border ON); every other field is
        // absent and must fall back to its ConfigDefaults value (not to a
        // zero/empty value).
        Inputs in;
        in.width = 6;
        in.showBorder = true;
        QJsonObject root = buildRoot(in);

        ConfigMigration::seedDecorationProfileTree(root);

        const DPS::DecorationProfile window = treeFromRoot(root).directOverride(QStringLiteral("window"));
        // Omitted ShaderEffectId → the default pack id; omitted HideTitleBars →
        // the autotile default.
        QCOMPARE(window.effectiveChain(), QStringList{CD::surfaceShaderEffectId()});
        QVERIFY(window.hideTitlebar.has_value());
        QCOMPARE(*window.hideTitlebar, CD::autotileHideTitleBars());

        const QVariantMap params = borderParamsOf(window);
        QCOMPARE(params.value(QStringLiteral("borderWidth")).toInt(), 6);
        QCOMPARE(params.value(QStringLiteral("cornerRadius")).toInt(), CD::autotileBorderRadius());
        QCOMPARE(params.value(QStringLiteral("useSystemAccent")).toBool(), CD::autotileUseSystemBorderColors());
        QCOMPARE(params.value(QStringLiteral("activeColor")).toString(),
                 CD::autotileBorderColor().name(QColor::HexArgb));
        QCOMPARE(params.value(QStringLiteral("inactiveColor")).toString(),
                 CD::autotileInactiveBorderColor().name(QColor::HexArgb));
    }

    void testEmptyShaderId_fallsBackToBorder()
    {
        // An engaged-but-empty ShaderEffectId must resolve to the default pack id
        // rather than seeding an empty chain (border is ON here).
        Inputs in;
        in.width = 6;
        in.showBorder = true;
        in.shaderEffectId = QString();
        QJsonObject root = buildRoot(in);

        ConfigMigration::seedDecorationProfileTree(root);

        const DPS::DecorationProfile window = treeFromRoot(root).directOverride(QStringLiteral("window"));
        QCOMPARE(window.effectiveChain(), QStringList{CD::surfaceShaderEffectId()});
        // The resolved chain IS the border pack, so its params are filed.
        QVERIFY(window.effectiveParameters().contains(CD::surfaceShaderEffectId()));
    }

    void testShowBorderExplicitFalse_emptyChainButParamsPreserved()
    {
        // The pack chain is the sole border gate: a v3 user who explicitly
        // turned the border OFF must NOT gain a border on upgrade. The chain is
        // engaged-but-empty, yet the (customised) appearance params survive so a
        // later re-enable restores width/colours instead of pack defaults.
        Inputs in;
        in.width = 6;
        in.showBorder = false;
        in.active = QStringLiteral("#ff112233");
        QJsonObject root = buildRoot(in);

        ConfigMigration::seedDecorationProfileTree(root);
        QVERIFY(hasTreeKey(root));

        const DPS::DecorationProfile window = treeFromRoot(root).directOverride(QStringLiteral("window"));
        QVERIFY2(window.effectiveChain().isEmpty(), "explicit ShowBorder=false must seed an empty chain (no border)");

        const QVariantMap params = borderParamsOf(window);
        QCOMPARE(params.value(QStringLiteral("borderWidth")).toInt(), 6);
        QCOMPARE(params.value(QStringLiteral("activeColor")).toString(),
                 QColor(QStringLiteral("#ff112233")).name(QColor::HexArgb));
    }

    void testShowBorderUnset_defaultsOff_emptyChain()
    {
        // ShowBorder defaults to false, so a config that customised some other
        // border field but never set ShowBorder keeps the legacy no-border
        // behaviour: a tree IS seeded (a field was customised) but with an empty
        // chain. (Guards against the regression where any customisation forced a
        // border on.)
        Inputs in;
        in.width = 6; // a customised field triggers the seed...
        // ...but ShowBorder is left unset.
        QJsonObject root = buildRoot(in);

        ConfigMigration::seedDecorationProfileTree(root);
        QVERIFY(hasTreeKey(root));

        const DPS::DecorationProfile window = treeFromRoot(root).directOverride(QStringLiteral("window"));
        QVERIFY2(window.effectiveChain().isEmpty(),
                 "unset ShowBorder (default false) must seed an empty chain (no border)");
    }
};

QTEST_GUILESS_MAIN(TestDecorationProfileSeed)
#include "test_decoration_profile_seed.moc"
