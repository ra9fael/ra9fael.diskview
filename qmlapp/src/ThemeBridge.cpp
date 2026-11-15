#include "ThemeBridge.h"

#include "Phosphor.h"

#include <QGuiApplication>
#include <QPalette>

#include <cmath>

namespace {

// A nativeLook colour as a QColor, or invalid when absent.
QColor colorOf(const QVariantMap& map, const char* key)
{
    const QVariant value = map.value(QString::fromLatin1(key));
    if (!value.canConvert<QString>()) return {};
    const QColor color(value.toString());
    return color.isValid() ? color : QColor();
}

// Composite a token that may carry alpha over an opaque surface, the way
// WinShun's own windows do with the transparency setting on.
QColor overColor(const QColor& translucent, const QColor& base)
{
    if (!translucent.isValid() || translucent.alpha() == 255) return translucent;
    const qreal a = translucent.alphaF();
    return QColor(
        static_cast<int>(translucent.red() * a + base.red() * (1 - a)),
        static_cast<int>(translucent.green() * a + base.green() * (1 - a)),
        static_cast<int>(translucent.blue() * a + base.blue() * (1 - a)));
}

} // namespace

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

QColor ThemeBridge::fallbackColor(const char* name, bool dark)
{
    struct Entry { const char* name; const char* light; const char* dark; };
    static const Entry table[] = {
        {"background", "#f4f6f8", "#1e2430"},
        {"page", "#f4f6f8", "#1e2430"},
        {"layer", "#ffffff", "#26303f"},
        {"card", "#ffffff", "#26303f"},
        {"sunken", "#f0f3f9", "#242b38"},
        {"divider", "#dce2e8", "#333d4e"},
        {"hover", "#fbfcfd", "#2c3646"},
        {"pressed", "#e3eefb", "#2a4a6e"},
        {"tagFill", "#e3eefb", "#2a4a6e"},
        {"text", "#243246", "#dbe5f0"},
        {"subtext", "#56657b", "#93a3b8"},
        {"faint", "#8b98a9", "#6f7f93"},
        {"control", "#ffffff", "#26303f"},
        {"controlBorder", "#dce2e8", "#333d4e"},
        {"inputFocused", "#ffffff", "#1e2430"},
        {"accent", "#2f7cd6", "#6aaef7"},
        {"accentFill", "#2f7cd6", "#2f7cd6"},
        {"accentText", "#ffffff", "#ffffff"},
        {"danger", "#b3261e", "#e58a80"},
        {"dangerFill", "#b3261e", "#b3261e"},
    };
    for (const Entry& entry : table) {
        if (QString::fromLatin1(entry.name) == QLatin1String(name)) {
            return QColor(QString::fromLatin1(dark ? entry.dark : entry.light));
        }
    }
    return {};
}

QColor ThemeBridge::lookColor(const char* name) const
{
    return colorOf(m_look, name);
}

double ThemeBridge::lookRadius(const char* name, double fallback) const
{
    if (!m_rounded) return 0.0;
    const QVariant value = m_look.value(QString::fromLatin1(name));
    return value.isValid() ? value.toDouble() : fallback;
}

QColor ThemeBridge::over(const QColor& translucent, const QColor& base)
{
    return overColor(translucent, base);
}

void ThemeBridge::setTheme(const QString& theme)
{
    // "dark", optionally with a style suffix after a "|". A nativeLook's own
    // `dark` wins; this only covers hosts that send the plain string.
    const int bar = theme.indexOf('|');
    const QString base = (bar >= 0 ? theme.left(bar) : theme).trimmed().toLower();
    const QString style = (bar >= 0 ? theme.mid(bar + 1) : QString()).trimmed().toLower();
    const bool dark = base == QStringLiteral("dark");
    bool dirty = false;
    if (m_dark != dark) { m_dark = dark; dirty = true; }
    if (!style.isEmpty() && m_style != style) { m_style = style; dirty = true; }
    if (!dirty) return;
    apply();
    emit changed();
}

void ThemeBridge::setAppearance(const QVariantMap& appearance)
{
    // The initialize form nests the tokens under nativeLook; an appearance
    // notification may carry them at the top level (the contract allows both).
    QVariantMap look = appearance.value(QStringLiteral("nativeLook")).toMap();
    if (look.isEmpty()) look = appearance;
    m_look = look;
    m_native = look.contains(QStringLiteral("background"));

    const QString style = appearance.value(QStringLiteral("uiStyle")).toString();
    if (style == QStringLiteral("fluent") || style == QStringLiteral("brand")
        || style == QStringLiteral("tactile") || style == QStringLiteral("liquid")) {
        m_style = style;
    }
    const QString size = appearance.value(QStringLiteral("uiSize")).toString();
    if (size == QStringLiteral("compact") || size == QStringLiteral("standard")
        || size == QStringLiteral("normal") || size == QStringLiteral("roomy")) {
        m_uiSize = size;
    }
    const QString corners = appearance.value(QStringLiteral("corners")).toString();
    if (!corners.isEmpty()) m_rounded = corners != QStringLiteral("square");
    if (m_look.contains(QStringLiteral("dark"))) m_dark = m_look.value(QStringLiteral("dark")).toBool();
    if (m_look.contains(QStringLiteral("rounded"))) m_rounded = m_look.value(QStringLiteral("rounded")).toBool();

    apply();
    emit changed();
}

void ThemeBridge::apply()
{
    if (!m_native) {
        // No host tokens (the standalone launcher, or an old host): keep the
        // window's own light/dark table and the system accent.
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
        m_accent = systemAccent();
        if (m_dark) m_accent = m_accent.lighter(135);
        m_accentText = m_accent.lightnessF() > 0.6 ? QColor(QStringLiteral("#1b2b3a"))
                                                   : QColor(QStringLiteral("#ffffff"));
        m_radius = m_style == QStringLiteral("tactile") ? 6 : 4;
        m_controlHeight = m_style == QStringLiteral("liquid") ? 28 : 26;
        m_fontSize = 11;
        return;
    }

    // The host's own tokens, from the back of the window to the front.
    QColor background = lookColor("background");
    if (!background.isValid()) background = fallbackColor("background", m_dark);
    QColor page = lookColor("page");
    if (!page.isValid()) page = background;
    m_window = over(background, page);

    QColor layer = lookColor("layer");
    if (!layer.isValid()) layer = fallbackColor("layer", m_dark);
    m_surface = over(layer, m_window);

    QColor control = lookColor("control");
    if (!control.isValid()) control = fallbackColor("control", m_dark);
    m_surfaceAlt = over(control, m_window);

    QColor hover = lookColor("hover");
    if (!hover.isValid()) hover = fallbackColor("hover", m_dark);
    m_handleFill = over(hover, m_surface);
    m_hoverRow = over(hover, m_surface);

    QColor divider = lookColor("divider");
    if (!divider.isValid()) divider = fallbackColor("divider", m_dark);
    m_border = over(divider, m_surface);

    m_text = lookColor("text");
    if (!m_text.isValid()) m_text = fallbackColor("text", m_dark);
    m_textMuted = lookColor("subtext");
    if (!m_textMuted.isValid()) m_textMuted = fallbackColor("subtext", m_dark);

    QColor tag = lookColor("tagFill");
    if (!tag.isValid()) tag = lookColor("pressed");
    if (!tag.isValid()) tag = fallbackColor("tagFill", m_dark);
    m_selection = over(tag, m_surface);

    QColor accent = lookColor("accent");
    if (!accent.isValid()) accent = lookColor("accentFill");
    if (!accent.isValid()) accent = systemAccent();
    m_accent = accent;
    m_accentText = lookColor("accentText");
    if (!m_accentText.isValid()) {
        m_accentText = m_accent.lightnessF() > 0.6 ? QColor(QStringLiteral("#1b2b3a"))
                                                   : QColor(QStringLiteral("#ffffff"));
    }

    QColor danger = lookColor("danger");
    if (!danger.isValid()) danger = fallbackColor("danger", m_dark);
    m_error = danger;
    // nativeLook has no success/warning pair; WinShun's own windows keep these.
    m_ok = m_dark ? QColor(QStringLiteral("#6ccb5f")) : QColor(QStringLiteral("#0f7b0f"));
    m_warn = m_dark ? QColor(QStringLiteral("#fce100")) : QColor(QStringLiteral("#9d5d00"));

    // Radii and sizes come from the same contract.
    m_radius = static_cast<int>(std::lround(lookRadius("controlRadius", 6.0)));
    const bool compact = m_uiSize == QStringLiteral("compact");
    const bool roomy = m_uiSize == QStringLiteral("roomy");
    m_controlHeight = compact ? 26 : roomy ? 32 : 28;
    m_fontSize = compact ? 10 : roomy ? 12 : 11;
}

QVariantMap ThemeBridge::phosphorIcon(const QString& name, bool filled) const
{
    const auto icon = ws::phosphor::forName(
        name, filled ? ws::phosphor::Weight::Fill : ws::phosphor::Weight::Regular);
    if (!icon.has_value()) return {};
    return {{"text", icon->text}, {"family", icon->family}};
}
