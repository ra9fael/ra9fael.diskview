#include "ThemeBridge.h"

#include "Phosphor.h"

#include <QGuiApplication>
#include <QPalette>
#include <QVariantMap>

ThemeBridge::ThemeBridge(QObject* parent) : QObject(parent)
{
    apply();
}

QColor ThemeBridge::systemAccent()
{
    const QColor accent = QGuiApplication::palette().color(QPalette::Accent);
    if (!accent.isValid() || accent.hsvSaturationF() < 0.25) {
        return QColor(QStringLiteral("#2f7cd6"));
    }
    return accent;
}

void ThemeBridge::setTheme(const QString& theme)
{
    // The protocol's "light" / "dark", with a style suffix after a "|" when a
    // host sends one. Anything else keeps what is showing.
    const int bar = theme.indexOf('|');
    const QString base = (bar >= 0 ? theme.left(bar) : theme).trimmed().toLower();
    const QString style = (bar >= 0 ? theme.mid(bar + 1) : QString()).trimmed().toLower();
    const bool dark = base == QStringLiteral("dark");
    if (m_dark == dark && m_style == style) return;
    m_dark = dark;
    m_style = style;
    apply();
    emit changed();
}

void ThemeBridge::apply()
{
    // The palette: the values the window already showed, now named, with the
    // dark halves the literals never had (a dark host used to get dark text
    // on dark rows).
    if (m_dark) {
        m_window = QColor(QStringLiteral("#1e2430"));
        m_surface = QColor(QStringLiteral("#26303f"));
        m_surfaceAlt = QColor(QStringLiteral("#242b38"));
        m_handleFill = QColor(QStringLiteral("#2a3342"));
        m_border = QColor(QStringLiteral("#333d4e"));
        m_text = QColor(QStringLiteral("#dbe5f0"));
        m_textMuted = QColor(QStringLiteral("#93a3b8"));
        m_selection = QColor(QStringLiteral("#2a4a6e"));
        m_hoverRow = QColor(QStringLiteral("#2c3646"));
        m_ok = QColor(QStringLiteral("#7cc38a"));
        m_warn = QColor(QStringLiteral("#d8a25a"));
        m_error = QColor(QStringLiteral("#e58a80"));
    } else {
        m_window = QColor(QStringLiteral("#f4f6f8"));
        m_surface = QColor(QStringLiteral("#ffffff"));
        m_surfaceAlt = QColor(QStringLiteral("#f0f3f9"));
        m_handleFill = QColor(QStringLiteral("#e6ebf2"));
        m_border = QColor(QStringLiteral("#dce2e8"));
        m_text = QColor(QStringLiteral("#243246"));
        m_textMuted = QColor(QStringLiteral("#56657b"));
        m_selection = QColor(QStringLiteral("#e3eefb"));
        m_hoverRow = QColor(QStringLiteral("#fbfcfd"));
        m_ok = QColor(QStringLiteral("#2c7a3f"));
        m_warn = QColor(QStringLiteral("#a76414"));
        m_error = QColor(QStringLiteral("#b3261e"));
    }
    // The accent is the system's in both looks; on dark it takes a step up so
    // strokes and text on it keep their contrast, the way WinShun's own
    // Look.qml lightens it.
    m_accent = systemAccent();
    if (m_dark) m_accent = m_accent.lighter(135);
    // The shape tokens: a rounder, softer style widens the corners and the
    // controls a little; the exact numbers follow the style's name once a
    // host sends one, until then the plugin's own look holds.
    m_radius = m_style == QStringLiteral("tactile") ? 6 : 4;
    m_controlHeight = m_style == QStringLiteral("liquid") ? 28 : 26;
    m_fontSize = 11;
}

QVariantMap ThemeBridge::phosphorIcon(const QString& name, bool filled) const
{
    const auto icon = ws::phosphor::forName(
        name, filled ? ws::phosphor::Weight::Fill : ws::phosphor::Weight::Regular);
    if (!icon.has_value()) return {};
    return {{"text", icon->text}, {"family", icon->family}};
}
