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
    check(DockState::valid(state.layout()), "default contains four unique panes");
    for (const QString pane : {"tree", "files", "chart", "legend"}) {
        for (const QString target : {"tree", "files", "chart", "legend"}) {
            if (pane == target) continue;
            for (const QString edge : {"left", "right", "top", "bottom", "center"}) {
                check(state.movePane(pane, target, edge), "move accepted");
                check(DockState::valid(state.layout()), "move preserves every pane exactly once");
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
    check(grouped.movePane("legend", "legend", "left"), "split active tab from all-in-one group");
    check(DockState::valid(grouped.layout()), "split tab layout remains valid");
    DockState single;
    check(!single.movePane("tree", "tree", "left"), "reject self drop from single-pane group");
    DockState restored;
    restored.setLayout(QJsonDocument::fromJson(QJsonDocument::fromVariant(saved).toJson()).object().toVariantMap());
    check(restored.layout() == saved, "JSON round trip");
    restored.setLayout({{"type", "tabs"}, {"panes", QVariantList{"tree", "tree"}}, {"active", "tree"}});
    check(restored.layout() == saved, "invalid saved layout cannot hide panes");
    state.reset();
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
