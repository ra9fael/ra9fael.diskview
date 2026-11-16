#include "DockState.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        if (!ok) { std::fprintf(stderr, "FAIL %s\n", name); ++failures; }
    };
    DockState state;
    check(state.valid(state.layout()), "default contains four unique panes");
    const auto defaultLayout = state.layout();
    const auto topTabs = defaultLayout.value("first").toMap();
    check(topTabs.value("type") == "tabs", "default top group is tabbed");
    const QStringList topPaneIds = topTabs.value("panes").toStringList();
    check(topPaneIds == QStringList{"tree", "files", "legend"},
          "default top tabs are tree, files and extensions");
    const auto bottomTabs = defaultLayout.value("second").toMap();
    check(bottomTabs.value("active") == "chart",
          "default visualization is below the list dock");
    const auto placements = state.placements(900, 600);
    bool topGroupAtTop = false;
    bool chartAtBottom = false;
    for (const auto& value : placements) {
        const auto placement = value.toMap();
        if (placement.value("type") != "tabs") continue;
        const auto ids = placement.value("panes").toStringList();
        if (ids == topPaneIds) topGroupAtTop = placement.value("y").toDouble() == 0;
        if (ids == QStringList{"chart"}) chartAtBottom = placement.value("y").toDouble() > 0;
    }
    check(topGroupAtTop && chartAtBottom, "default placements put list tabs above visualization");
    for (const QString pane : {"tree", "files", "chart", "legend"}) {
        for (const QString target : {"tree", "files", "chart", "legend"}) {
            if (pane == target) continue;
            for (const QString edge : {"left", "right", "top", "bottom", "center"}) {
                check(state.movePane(pane, target, edge), "move accepted");
                check(state.valid(state.layout()), "move preserves every pane exactly once");
                check(state.activate(pane), "activate detached or merged tab");
            }
        }
    }
    const auto saved = state.layout();
    check(!state.movePane("unknown", "tree", "left"), "reject unknown pane");
    check(state.layout() == saved, "invalid drop is atomic");
    DockState grouped;
    check(grouped.movePane("files", "tree", "center"), "merge files into tree tabs");
    check(grouped.movePane("chart", "tree", "center"), "merge chart into tree tabs");
    check(grouped.movePane("legend", "tree", "center"), "merge legend into tree tabs");
    check(grouped.movePane("legend", "tree", "left"), "split an extension tab from the list group");
    check(grouped.valid(grouped.layout()), "split tab layout remains valid");
    DockState single;
    check(!single.movePane("tree", "tree", "left"), "reject self drop without another target");
    check(single.movePane("files", "tree", "center"), "group a second pane before self drop");
    check(!single.movePane("tree", "tree", "left"), "reject self drop of an active tab");
    DockState restored;
    restored.setLayout(QJsonDocument::fromJson(QJsonDocument::fromVariant(saved).toJson()).object().toVariantMap());
    check(restored.layout() == saved, "JSON round trip");
    restored.setLayout({{"type", "tabs"}, {"panes", QVariantList{"tree", "tree"}}, {"active", "tree"}});
    check(restored.layout() == saved, "invalid saved layout cannot hide panes");
    state.reset();
    check(state.valid(state.layout()), "reset returns a valid layout");
    check(state.preset() == "lists-top", "the default tick is the lists-top preset");
    for (const QString id : DockState::presetIds()) {
        DockState byPreset;
        check(byPreset.applyPreset(id), "preset applies");
        check(byPreset.valid(byPreset.layout()), "preset is a valid layout");
        check(byPreset.preset() == id, "preset reads back");
    }
    {
        DockState orientation;
        const auto side = DockState::presetLayout(QStringLiteral("lists-left"));
        check(side.value("orientation") == "horizontal", "left preset splits horizontally");
        const auto top = DockState::presetLayout(QStringLiteral("lists-top"));
        check(top.value("orientation") == "vertical", "top preset splits vertically");
        check(!orientation.applyPreset("nope"), "unknown preset is rejected");
        orientation.reset();
        check(orientation.movePane("files", "chart", "center"), "manual drag after a preset");
        check(orientation.preset().isEmpty(), "a dragged layout is custom, not a preset");
    }
    check(state.setRatio("", .7), "resize root");
    check(state.layout().value("ratio").toDouble() == .7, "ratio stored");
    check(!state.setRatio("000", .5), "reject leaf path");
    for (const auto& value : state.placements(900, 600)) {
        const auto rect = value.toMap();
        check(rect.value("width").toDouble() >= 0 && rect.value("height").toDouble() >= 0, "nonnegative placement");
        check(rect.value("x").toDouble() + rect.value("width").toDouble() <= 900, "within workspace width");
        check(rect.value("y").toDouble() + rect.value("height").toDouble() <= 600, "within workspace height");
    }
    std::printf("dock tests: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
