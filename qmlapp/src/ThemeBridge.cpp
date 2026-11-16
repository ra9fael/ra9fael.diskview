#include "ThemeBridge.h"

#include "Phosphor.h"

#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QPalette>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QWindow>

#include <dwmapi.h>

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

// Windows 11 22H2 is the first release that draws a system backdrop.
bool systemBackdropSupported()
{
    static const bool supported = QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11_22H2;
    return supported;
}

// The same two checks WinShun makes: high contrast has no materials, and the
// user can switch transparency off in the system settings.
bool systemMaterialsEnabled()
{
    HIGHCONTRASTW contrast {sizeof contrast};
    if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof contrast, &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON))
        return false;
    DWORD transparency = 1;
    DWORD size = sizeof transparency;
    ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &size);
    return transparency != 0;
}

} // namespace

ThemeBridge::ThemeBridge(QObject* parent) : QObject(parent)
{
    apply();
    for (QWindow* window : QGuiApplication::allWindows()) {
        const BOOL darkAttribute = m_dark ? TRUE : FALSE;
        ::DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), 20, &darkAttribute, sizeof(darkAttribute));
    }
}

QColor ThemeBridge::systemAccent()
{
    const QColor accent = QGuiApplication::palette().color(QPalette::Accent);
    if (!accent.isValid() || accent.hsvSaturationF() < 0.25) {
        return QColor(QStringLiteral("#2f7cd6"));
    }
    return accent;
}

// WinShun's default fluent Look (themes/fluent/Look.qml). The standalone
// launcher, local light/dark modes and hosts that omit tokens all draw with
// the host's own defaults, so the pages match WinShun exactly.
QColor ThemeBridge::fallbackColor(const char* name, bool dark)
{
    struct Entry { const char* name; const char* light; const char* dark; };
    static const Entry table[] = {
        {"background", "#f9f9f9", "#202020"},
        {"page", "#f3f3f3", "#202020"},
        {"layer", "#ffffff", "#272727"},
        {"card", "#fbfbfb", "#2b2b2b"},
        {"divider", "#eaeaea", "#2e2e2e"},
        {"hover", "#f1f1f1", "#292929"},
        {"pressed", "#e8e8e8", "#323232"},
        {"tagFill", "#0d000000", "#12ffffff"},
        {"text", "#1b1b1b", "#f3f3f3"},
        {"subtext", "#666666", "#b8b8b8"},
        {"faint", "#8a8a8a", "#8a8a8a"},
        {"control", "#ffffff", "#373737"},
        {"controlBorder", "#d5d5d5", "#444444"},
        {"inputFocused", "#ffffff", "#1f1f1f"},
        {"accent", "#0078d4", "#66b8ff"},
        {"controlHover", "#f6f6f6", "#3c3c3c"},
        {"accentFill", "#2f7cd6", "#2f7cd6"},
        {"accentText", "#ffffff", "#ffffff"},
        {"danger", "#c42b1c", "#ff99a4"},
        {"dangerFill", "#c42b1c", "#c42b1c"},
        {"menuBackground", "#f9f9f9", "#2c2c2c"},
        {"menuHover", "#ececec", "#3a3a3a"},
        {"menuEdge", "#1a000000", "#17ffffff"},
        {"windowEdge", "#b4b4b4", "#6a6a6a"},
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
    for (QWindow* window : QGuiApplication::allWindows()) {
        const BOOL darkAttribute = m_dark ? TRUE : FALSE;
        ::DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), 20, &darkAttribute, sizeof(darkAttribute));
    }
    emit changed();
}

void ThemeBridge::setAppearance(const QVariantMap& appearance)
{
    // The initialize form nests the tokens under nativeLook; an appearance
    // notification may carry them at the top level (the contract allows both).
    QVariantMap look = appearance.value(QStringLiteral("nativeLook")).toMap();
    if (look.isEmpty()) look = appearance;
    m_hostLook = look;
    m_hostNative = look.contains(QStringLiteral("background"))
                   || look.contains(QStringLiteral("layer"))
                   || look.contains(QStringLiteral("text"));
    m_look = m_hostLook;
    m_native = m_hostNative;

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
    const QString family = appearance.value(QStringLiteral("resolvedUiFont")).toString();
    if (!family.trimmed().isEmpty()) m_fontFamily = family.trimmed();
    const QString corners = appearance.value(QStringLiteral("corners")).toString();
    if (!corners.isEmpty()) m_rounded = corners != QStringLiteral("square");
    const QString transparency = appearance.value(QStringLiteral("transparency")).toString();
    if (!transparency.isEmpty()) m_transparency = transparency == QStringLiteral("on");
    const QString renderer = appearance.value(QStringLiteral("renderer")).toString();
    if (!renderer.isEmpty()) m_renderer = renderer;
    if (m_look.contains(QStringLiteral("dark"))) m_dark = m_look.value(QStringLiteral("dark")).toBool();
    if (m_look.contains(QStringLiteral("rounded"))) m_rounded = m_look.value(QStringLiteral("rounded")).toBool();
    m_hostDark = m_dark;
    if (m_localMode != 0) {
        m_look.clear();
        m_native = false;
        m_dark = m_localMode == 2;
    }

    apply();
    for (QWindow* window : QGuiApplication::allWindows()) {
        const BOOL darkAttribute = m_dark ? TRUE : FALSE;
        ::DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), 20, &darkAttribute, sizeof(darkAttribute));
    }
    emit changed();
}

void ThemeBridge::setLocalMode(int mode)
{
    m_localMode = std::clamp(mode, 0, 2);
    if (m_localMode == 0) {
        m_look = m_hostLook;
        m_native = m_hostNative;
        m_dark = m_hostDark;
    } else {
        m_look.clear();
        m_native = false;
        m_dark = m_localMode == 2;
    }
    apply();
    for (QWindow* window : QGuiApplication::allWindows()) {
        const BOOL darkAttribute = m_dark ? TRUE : FALSE;
        ::DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), 20, &darkAttribute, sizeof(darkAttribute));
    }
    emit changed();
}

void ThemeBridge::applyWindowBackdrop(HWND window) const
{
    if (!window) return;
    // DWMSBT_MAINWINDOW is Mica; the liquid style stands on acrylic, the way
    // WinShun's own windows do. 1 is no backdrop.
    const int material = !m_backdrop ? 1 : (m_style == QStringLiteral("liquid") ? 3 : 2);
    ::DwmSetWindowAttribute(window, 38, &material, sizeof material);
}

void ThemeBridge::apply()
{
    // Mica needs the host to ask for it, a graphics card (the software
    // renderer has no alpha swap chain), Windows 11 22H2 and the system's own
    // transparency left on. A local light/dark override drops the host look,
    // so it drops the backdrop too.
    m_backdrop = m_native && m_transparency && m_renderer == QStringLiteral("d3d11")
                 && QQuickWindow::graphicsApi() == QSGRendererInterface::Direct3D11
                 && systemBackdropSupported() && systemMaterialsEnabled();

    if (!m_native) {
        // No host tokens (the standalone launcher, or an old host): keep the
        // window's own light/dark table and the system accent.
        if (m_dark) {
            m_window = QColor(QStringLiteral("#202020"));
            m_surface = QColor(QStringLiteral("#202020"));
            m_surfaceAlt = QColor(QStringLiteral("#272727"));
            m_handleFill = QColor(QStringLiteral("#292929"));
            m_border = QColor(QStringLiteral("#2e2e2e"));
            m_scrollThumb = QColor(QStringLiteral("#8a8a8a"));
            m_text = QColor(QStringLiteral("#f3f3f3"));
            m_textMuted = QColor(QStringLiteral("#b8b8b8"));
            m_selection = QColor(255, 255, 255, 18);
            m_hoverRow = QColor(QStringLiteral("#292929"));
            m_ok = QColor(QStringLiteral("#6ccb5f"));
            m_warn = QColor(QStringLiteral("#fce100"));
            m_error = QColor(QStringLiteral("#ff99a4"));
            m_page = m_window;
            m_faint = QColor(QStringLiteral("#8a8a8a"));
            m_pressed = QColor(QStringLiteral("#323232"));
            m_control = QColor(QStringLiteral("#373737"));
            m_controlHover = QColor(QStringLiteral("#3c3c3c"));
            m_controlBorder = QColor(QStringLiteral("#444444"));
            m_menuBackground = QColor(QStringLiteral("#2c2c2c"));
            m_menuHover = QColor(QStringLiteral("#3a3a3a"));
            m_menuEdge = QColor(255, 255, 255, 23);
            m_windowEdge = QColor(QStringLiteral("#6a6a6a"));
        } else {
            m_window = QColor(QStringLiteral("#f9f9f9"));
            m_surface = QColor(QStringLiteral("#f9f9f9"));
            m_surfaceAlt = QColor(QStringLiteral("#ffffff"));
            m_handleFill = QColor(QStringLiteral("#f1f1f1"));
            m_border = QColor(QStringLiteral("#eaeaea"));
            m_scrollThumb = QColor(QStringLiteral("#8a8a8a"));
            m_text = QColor(QStringLiteral("#1b1b1b"));
            m_textMuted = QColor(QStringLiteral("#666666"));
            m_selection = QColor(0, 0, 0, 13);
            m_hoverRow = QColor(QStringLiteral("#f1f1f1"));
            m_ok = QColor(QStringLiteral("#0f7b0f"));
            m_warn = QColor(QStringLiteral("#9d5d00"));
            m_error = QColor(QStringLiteral("#c42b1c"));
            m_page = QColor(QStringLiteral("#f3f3f3"));
            m_faint = QColor(QStringLiteral("#8a8a8a"));
            m_pressed = QColor(QStringLiteral("#e8e8e8"));
            m_control = QColor(QStringLiteral("#ffffff"));
            m_controlHover = QColor(QStringLiteral("#f6f6f6"));
            m_controlBorder = QColor(QStringLiteral("#d5d5d5"));
            m_menuBackground = QColor(QStringLiteral("#f9f9f9"));
            m_menuHover = QColor(QStringLiteral("#ececec"));
            m_menuEdge = QColor(0, 0, 0, 26);
            m_windowEdge = QColor(QStringLiteral("#b4b4b4"));
        }
        m_accent = systemAccent();
        if (m_dark) m_accent = m_accent.lighter(155);
        m_accentLine = m_accent;
        m_textSelection = m_selection;
        m_caret = m_accentLine;
        m_controlHover = m_hoverRow;
        m_accentText = QColor(QStringLiteral("#ffffff"));
        m_radius = m_style == QStringLiteral("tactile") ? 6 : 4;
        m_cardRadius = m_rounded ? 6 : 0;
        m_controlRadius = m_rounded ? 4 : 0;
        m_smallRadius = m_rounded ? 3 : 0;
        m_controlHeight = m_style == QStringLiteral("liquid") ? 28 : 26;
        m_fontSize = 11;
        m_fontBody = 14; m_fontCaption = 12; m_fontSmall = 11;
        m_rowHeight = 32; m_fileRowHeight = 30; m_iconSize = 18; m_gap = 8; m_pad = 12;
        return;
    }

    // The host's own tokens, from the back of the window to the front. On
    // Mica the window itself is see-through and the translucent tokens are
    // painted as the host sent them, so the material shows through; otherwise
    // they are flattened onto an opaque surface, which is what the contract
    // asks of a plugin that draws its own window.
    QColor background = lookColor("background");
    if (!background.isValid()) background = fallbackColor("background", m_dark);
    QColor page = lookColor("page");
    if (!page.isValid()) page = fallbackColor("page", m_dark);
    m_page = m_backdrop ? page : over(page, background);
    m_window = m_backdrop ? QColor(0, 0, 0, 0) : over(background, m_page);

    QColor layer = lookColor("layer");
    if (!layer.isValid()) layer = fallbackColor("layer", m_dark);
    // WinShun paints its page/body with background and its chrome with layer.
    const auto place = [this](const QColor& color, const QColor& onto) {
        return m_backdrop ? color : over(color, onto);
    };
    // The body shows the material; chrome (toolbars, headers, the status
    // bar) is the host's layer, translucent on Mica.
    m_surface = m_backdrop ? background : m_window;
    m_surfaceAlt = place(layer, m_window);
    m_control = lookColor("control");
    if (!m_control.isValid()) m_control = fallbackColor("control", m_dark);
    m_control = place(m_control, m_surface);
    m_controlHover = lookColor("controlHover");
    if (!m_controlHover.isValid()) m_controlHover = lookColor("hover");
    if (!m_controlHover.isValid()) m_controlHover = fallbackColor("control", m_dark);
    m_controlHover = place(m_controlHover, m_control);
    m_controlBorder = lookColor("controlBorder");
    if (!m_controlBorder.isValid()) m_controlBorder = fallbackColor("controlBorder", m_dark);
    m_controlBorder = place(m_controlBorder, m_surface);
    QColor hover = lookColor("hover");
    if (!hover.isValid()) hover = fallbackColor("hover", m_dark);
    m_handleFill = place(hover, m_surfaceAlt);
    m_hoverRow = place(hover, m_surface);
    QColor divider = lookColor("divider");
    if (!divider.isValid()) divider = fallbackColor("divider", m_dark);
    m_border = place(divider, m_surface);
    m_faint = lookColor("faint");
    if (!m_faint.isValid()) m_faint = fallbackColor("faint", m_dark);
    m_pressed = lookColor("pressed");
    if (!m_pressed.isValid()) m_pressed = fallbackColor("pressed", m_dark);
    m_pressed = place(m_pressed, m_surface);
    // Menus and the drive popup are their own windows and never get Mica, so
    // they stay flattened onto an opaque surface either way.
    m_menuBackground = lookColor("menuBackground");
    if (!m_menuBackground.isValid()) m_menuBackground = fallbackColor("menuBackground", m_dark);
    m_menuBackground = over(m_menuBackground, background);
    m_menuHover = lookColor("menuHover");
    if (!m_menuHover.isValid()) m_menuHover = hover;
    if (!m_menuHover.isValid()) m_menuHover = fallbackColor("menuHover", m_dark);
    m_menuHover = over(m_menuHover, m_menuBackground);
    m_menuEdge = lookColor("menuEdge");
    if (!m_menuEdge.isValid()) m_menuEdge = divider;
    if (!m_menuEdge.isValid()) m_menuEdge = fallbackColor("menuEdge", m_dark);
    m_menuEdge = over(m_menuEdge, m_menuBackground);
    m_windowEdge = lookColor("windowEdge");
    if (!m_windowEdge.isValid()) m_windowEdge = divider;
    if (!m_windowEdge.isValid()) m_windowEdge = fallbackColor("windowEdge", m_dark);
    m_windowEdge = over(m_windowEdge, background);

    m_text = lookColor("text");
    if (!m_text.isValid()) m_text = fallbackColor("text", m_dark);
    m_textMuted = lookColor("subtext");
    if (!m_textMuted.isValid()) m_textMuted = fallbackColor("subtext", m_dark);
    m_scrollThumb = m_faint;

    QColor tag = lookColor("tagFill");
    if (!tag.isValid()) tag = lookColor("pressed");
    if (!tag.isValid()) tag = fallbackColor("tagFill", m_dark);
    m_selection = m_backdrop ? tag : over(tag, m_surface);

    QColor accent = lookColor("accent");
    if (!accent.isValid()) accent = lookColor("accentFill");
    if (!accent.isValid()) accent = systemAccent();
    m_accent = accent;
    m_accentLine = lookColor("accentLine");
    if (!m_accentLine.isValid()) m_accentLine = m_accent;
    m_textSelection = lookColor("textSelection");
    if (!m_textSelection.isValid()) m_textSelection = m_selection;
    m_caret = lookColor("caret");
    if (!m_caret.isValid()) m_caret = m_accentLine;
    m_scrollThumb = m_faint;
    m_accentText = lookColor("accentText");
    if (!m_accentText.isValid()) m_accentText = QColor(QStringLiteral("#ffffff"));

    QColor danger = lookColor("danger");
    if (!danger.isValid()) danger = fallbackColor("danger", m_dark);
    m_error = danger;
    // nativeLook has no success/warning pair; WinShun's own windows keep these.
    m_ok = m_dark ? QColor(QStringLiteral("#6ccb5f")) : QColor(QStringLiteral("#0f7b0f"));
    m_warn = m_dark ? QColor(QStringLiteral("#fce100")) : QColor(QStringLiteral("#9d5d00"));

    // Radii and sizes come from the same contract.
    m_radius = static_cast<int>(std::lround(lookRadius("controlRadius", 4.0)));
    m_cardRadius = lookRadius("cardRadius", 6.0);
    m_controlRadius = lookRadius("controlRadius", 4.0);
    m_smallRadius = lookRadius("smallControlRadius", 3.0);
    const bool compact = m_uiSize == QStringLiteral("compact");
    const bool roomy = m_uiSize == QStringLiteral("roomy");
    m_controlHeight = compact ? 28 : roomy ? 36 : 32;
    m_fontBody = compact ? 14 : roomy ? 18 : 16;
    m_fontCaption = compact ? 12 : roomy ? 16 : 14;
    m_fontSmall = compact ? 11 : roomy ? 14 : 12;
    m_fontSize = m_fontCaption;
    m_rowHeight = compact ? 30 : roomy ? 40 : 32;
    m_fileRowHeight = compact ? 36 : roomy ? 48 : 40;
    m_iconSize = compact ? 16 : roomy ? 22 : 18;
    m_gap = compact ? 6 : roomy ? 12 : 8;
    m_pad = compact ? 10 : roomy ? 18 : 14;
}

QVariantMap ThemeBridge::phosphorIcon(const QString& name, bool filled) const
{
    const auto icon = ws::phosphor::forName(
        name, filled ? ws::phosphor::Weight::Fill : ws::phosphor::Weight::Regular);
    if (!icon.has_value()) return {};
    return {{"text", icon->text}, {"family", icon->family}};
}
