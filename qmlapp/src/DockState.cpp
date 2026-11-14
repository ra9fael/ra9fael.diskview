#include "DockState.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
const QSet<QString> panes{QStringLiteral("tree"), QStringLiteral("files"),
                          QStringLiteral("legend"), QStringLiteral("chart")};
QVariantMap tabs(const QString& id) {
    return {{"type", "tabs"}, {"panes", QVariantList{id}}, {"active", id}};
}
QVariantMap split(const QVariantMap& a, const QVariantMap& b, const QString& orientation, double ratio) {
    return {{"type", "split"}, {"first", a}, {"second", b},
            {"orientation", orientation}, {"ratio", ratio}};
}
bool inspect(const QVariantMap& node, QSet<QString>& seen, int depth) {
    if (depth > 7) return false;
    if (node.value("type") == "tabs") {
        const auto ids = node.value("panes").toList();
        if (ids.isEmpty() || ids.size() > 4) return false;
        bool active = false;
        for (const auto& value : ids) {
            const QString id = value.toString();
            if (!panes.contains(id) || seen.contains(id)) return false;
            seen.insert(id);
            active |= id == node.value("active").toString();
        }
        return active;
    }
    if (node.value("type") != "split") return false;
    const auto orientation = node.value("orientation").toString();
    const double ratio = node.value("ratio").toDouble();
    return (orientation == "horizontal" || orientation == "vertical")
        && std::isfinite(ratio) && ratio >= .1 && ratio <= .9
        && inspect(node.value("first").toMap(), seen, depth + 1)
        && inspect(node.value("second").toMap(), seen, depth + 1);
}
QVariantMap remove(QVariantMap node, const QString& pane) {
    if (node.value("type") == "tabs") {
        auto ids = node.value("panes").toList();
        ids.removeAll(pane);
        if (ids.isEmpty()) return {};
        node["panes"] = ids;
        if (node.value("active") == pane) node["active"] = ids.first();
        return node;
    }
    const auto a = remove(node.value("first").toMap(), pane);
    const auto b = remove(node.value("second").toMap(), pane);
    if (a.isEmpty()) return b;
    if (b.isEmpty()) return a;
    node["first"] = a;
    node["second"] = b;
    return node;
}
QVariantMap insert(QVariantMap node, const QString& pane, const QString& target,
                   const QString& edge, bool& found) {
    if (node.value("type") == "tabs") {
        auto ids = node.value("panes").toList();
        if (!ids.contains(target)) return node;
        found = true;
        if (edge == "center") {
            ids.append(pane);
            node["panes"] = ids;
            node["active"] = pane;
            return node;
        }
        const bool before = edge == "left" || edge == "top";
        return split(before ? tabs(pane) : node, before ? node : tabs(pane),
                     edge == "left" || edge == "right" ? "horizontal" : "vertical", .5);
    }
    node["first"] = insert(node.value("first").toMap(), pane, target, edge, found);
    node["second"] = insert(node.value("second").toMap(), pane, target, edge, found);
    return node;
}
QVariantMap editAt(QVariantMap node, const QString& path,
                   const std::function<void(QVariantMap&)>& edit) {
    if (path.isEmpty()) { edit(node); return node; }
    const QString key = path.front() == '0' ? "first" : "second";
    node[key] = editAt(node.value(key).toMap(), path.mid(1), edit);
    return node;
}
QVariantMap activateIn(QVariantMap node, const QString& pane) {
    if (node.value("type") == "tabs") {
        if (node.value("panes").toList().contains(pane)) node["active"] = pane;
    } else {
        node["first"] = activateIn(node.value("first").toMap(), pane);
        node["second"] = activateIn(node.value("second").toMap(), pane);
    }
    return node;
}
void place(const QVariantMap& node, double x, double y, double w, double h,
           const QString& path, QVariantList& out) {
    QVariantMap rect{{"x", x}, {"y", y}, {"width", w}, {"height", h}, {"path", path}};
    if (node.value("type") == "tabs") {
        rect["type"] = "tabs";
        rect["panes"] = node.value("panes");
        rect["active"] = node.value("active");
        out.append(rect);
        return;
    }
    const bool horizontal = node.value("orientation") == "horizontal";
    const double total = horizontal ? w : h;
    const double handle = std::min(5., total);
    const double available = std::max(0., total - handle);
    const double first = std::round(available * node.value("ratio").toDouble());
    rect["type"] = "handle";
    rect["horizontal"] = horizontal;
    rect["origin"] = horizontal ? x : y;
    rect["available"] = available;
    rect["ratio"] = node.value("ratio");
    rect[horizontal ? "x" : "y"] = (horizontal ? x : y) + first;
    rect[horizontal ? "width" : "height"] = handle;
    out.append(rect);
    place(node.value("first").toMap(), x, y, horizontal ? first : w,
          horizontal ? h : first, path + '0', out);
    place(node.value("second").toMap(), horizontal ? x + first + handle : x,
          horizontal ? y : y + first + handle, horizontal ? available - first : w,
          horizontal ? h : available - first, path + '1', out);
}
}

DockState::DockState(QObject* parent) : QObject(parent), m_layout(defaultLayout()) {}
QVariantMap DockState::defaultLayout() {
    return split(split(tabs("tree"), tabs("files"), "vertical", .62),
                 split(tabs("chart"), tabs("legend"), "vertical", .68), "horizontal", .46);
}
bool DockState::valid(const QVariantMap& value) {
    QSet<QString> seen;
    return inspect(value, seen, 0) && seen == panes;
}
void DockState::setLayout(const QVariantMap& value) {
    if (!valid(value) || value == m_layout) return;
    m_layout = value;
    emit changed();
}
void DockState::reset() { setLayout(defaultLayout()); }
bool DockState::movePane(const QString& pane, const QString& target, const QString& edge) {
    if (!panes.contains(pane) || !panes.contains(target) || pane == target
        || !QStringList{"left", "right", "top", "bottom", "center"}.contains(edge)) return false;
    auto next = remove(m_layout, pane);
    bool found = false;
    next = insert(next, pane, target, edge, found);
    if (!found || !valid(next)) return false;
    setLayout(next);
    return true;
}
bool DockState::setRatio(const QString& path, double ratio) {
    if (!std::isfinite(ratio)) return false;
    for (const auto c : path) if (c != '0' && c != '1') return false;
    auto current = m_layout;
    for (const auto c : path) {
        if (current.value("type") != "split") return false;
        current = current.value(c == '0' ? "first" : "second").toMap();
    }
    if (current.value("type") != "split") return false;
    setLayout(editAt(m_layout, path, [ratio](QVariantMap& node) { node["ratio"] = std::clamp(ratio, .1, .9); }));
    return true;
}
bool DockState::activate(const QString& pane) {
    if (!panes.contains(pane)) return false;
    setLayout(activateIn(m_layout, pane));
    return true;
}
QVariantList DockState::placements(double width, double height) const {
    QVariantList result;
    if (std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0)
        place(m_layout, 0, 0, width, height, {}, result);
    return result;
}
