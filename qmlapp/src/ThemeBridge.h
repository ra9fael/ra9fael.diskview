#pragma once

#include <QColor>
#include <QObject>
#include <QVariantMap>

#include <windows.h>
#include <QtQml/qqmlregistration.h>

// The window's whole look, as named tokens: every colour the QML and the chart
// use comes from here.
//
// WinShun's appearance contract (the same one ra9fael.tio draws from) sends the
// active style's own colours and radii as `nativeLook` inside the `appearance`
// object, together with `uiStyle`, `uiSize`, `corners` and `transparency`.
// `setAppearance` reads that; any field the host did not send falls back to the
// fluent-like table below, so an older host still gets a sane window. Without
// an appearance at all (the standalone launcher) the system palette and the
// light/dark string are used.
class ThemeBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString style READ style NOTIFY changed)
    Q_PROPERTY(bool rounded READ rounded NOTIFY changed)
    // True when the host sent nativeLook and the window is drawing from it.
    Q_PROPERTY(bool native READ native NOTIFY changed)
    // True when this window stands on Mica: the host asked for transparency,
    // the machine can draw a system backdrop, and the window background is
    // see-through. Translucent tokens are then drawn as the host sent them.
    Q_PROPERTY(bool backdrop READ backdrop NOTIFY changed)
    Q_PROPERTY(QColor window READ window NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY changed)
    Q_PROPERTY(QColor page READ page NOTIFY changed)
    Q_PROPERTY(QColor faint READ faint NOTIFY changed)
    Q_PROPERTY(QColor pressed READ pressed NOTIFY changed)
    Q_PROPERTY(QColor control READ control NOTIFY changed)
    Q_PROPERTY(QColor controlHover READ controlHover NOTIFY changed)
    Q_PROPERTY(QColor controlBorder READ controlBorder NOTIFY changed)
    Q_PROPERTY(QColor menuBackground READ menuBackground NOTIFY changed)
    Q_PROPERTY(QColor menuHover READ menuHover NOTIFY changed)
    Q_PROPERTY(QColor menuEdge READ menuEdge NOTIFY changed)
    Q_PROPERTY(QColor windowEdge READ windowEdge NOTIFY changed)
    Q_PROPERTY(QColor handleFill READ handleFill NOTIFY changed)
    Q_PROPERTY(QColor scrollThumb READ scrollThumb NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed)
    Q_PROPERTY(QColor hoverRow READ hoverRow NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentLine READ accentLine NOTIFY changed)
    Q_PROPERTY(QColor textSelection READ textSelection NOTIFY changed)
    Q_PROPERTY(QColor caret READ caret NOTIFY changed)
    // Text/icon colour that reads on accentFill.
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor ok READ ok NOTIFY changed)
    Q_PROPERTY(QColor warn READ warn NOTIFY changed)
    Q_PROPERTY(QColor error READ error NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(int controlHeight READ controlHeight NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY changed)
    Q_PROPERTY(int fontBody READ fontBody NOTIFY changed)
    Q_PROPERTY(int fontCaption READ fontCaption NOTIFY changed)
    Q_PROPERTY(int fontSmall READ fontSmall NOTIFY changed)
    Q_PROPERTY(int rowHeight READ rowHeight NOTIFY changed)
    Q_PROPERTY(int fileRowHeight READ fileRowHeight NOTIFY changed)
    Q_PROPERTY(int iconSize READ iconSize NOTIFY changed)
    Q_PROPERTY(int gap READ gap NOTIFY changed)
    Q_PROPERTY(int pad READ pad NOTIFY changed)
    Q_PROPERTY(double cardRadius READ cardRadius NOTIFY changed)
    Q_PROPERTY(double controlRadius READ controlRadius NOTIFY changed)
    Q_PROPERTY(double smallRadius READ smallRadius NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
    Q_PROPERTY(int nativeTitleBarDark READ nativeTitleBarDark NOTIFY changed)

public:
    explicit ThemeBridge(QObject* parent = nullptr);

    bool dark() const { return m_dark; }
    QString style() const { return m_style; }
    bool rounded() const { return m_rounded; }
    bool native() const { return m_native; }
    bool backdrop() const { return m_backdrop; }
    QColor window() const { return m_window; }
    QColor surface() const { return m_surface; }
    QColor surfaceAlt() const { return m_surfaceAlt; }
    QColor page() const { return m_page; }
    QColor faint() const { return m_faint; }
    QColor pressed() const { return m_pressed; }
    QColor control() const { return m_control; }
    QColor controlHover() const { return m_controlHover; }
    QColor controlBorder() const { return m_controlBorder; }
    QColor menuBackground() const { return m_menuBackground; }
    QColor menuHover() const { return m_menuHover; }
    QColor menuEdge() const { return m_menuEdge; }
    QColor windowEdge() const { return m_windowEdge; }
    QColor handleFill() const { return m_handleFill; }
    QColor scrollThumb() const { return m_scrollThumb; }
    QColor border() const { return m_border; }
    QColor text() const { return m_text; }
    QColor textMuted() const { return m_textMuted; }
    QColor selection() const { return m_selection; }
    QColor hoverRow() const { return m_hoverRow; }
    QColor accent() const { return m_accent; }
    QColor accentLine() const { return m_accentLine; }
    QColor textSelection() const { return m_textSelection; }
    QColor caret() const { return m_caret; }
    QColor accentText() const { return m_accentText; }
    QColor ok() const { return m_ok; }
    QColor warn() const { return m_warn; }
    QColor error() const { return m_error; }
    int radius() const { return m_radius; }
    int controlHeight() const { return m_controlHeight; }
    int fontSize() const { return m_fontSize; }
    int fontBody() const { return m_fontBody; }
    int fontCaption() const { return m_fontCaption; }
    int fontSmall() const { return m_fontSmall; }
    int rowHeight() const { return m_rowHeight; }
    int fileRowHeight() const { return m_fileRowHeight; }
    int iconSize() const { return m_iconSize; }
    int gap() const { return m_gap; }
    int pad() const { return m_pad; }
    double cardRadius() const { return m_cardRadius; }
    double controlRadius() const { return m_controlRadius; }
    double smallRadius() const { return m_smallRadius; }
    QString fontFamily() const { return m_fontFamily; }
    int nativeTitleBarDark() const { return m_dark ? 1 : 0; }

    // "light" or "dark", with an optional "|fluent|brand|tactile|liquid"
    // suffix. Unknown strings keep the current look.
    void setTheme(const QString& theme);
    Q_INVOKABLE void setLocalMode(int mode);
    // The host's appearance object (initialize's `appearance` or an
    // `appearance` notification), as a QVariantMap. Empty clears it.
    void setAppearance(const QVariantMap& appearance);
    // Puts the system backdrop on the main window, or takes it off. Popup
    // windows are left alone: Mica on a small transient window looks wrong.
    void applyWindowBackdrop(HWND window) const;
    // The named Phosphor icon, as {text, family}; empty when the name is not
    // one of the bundled set (Glyph.qml then draws nothing, never a wrong
    // character from another font).
    Q_INVOKABLE QVariantMap phosphorIcon(const QString& name, bool filled = false) const;

signals:
    void changed();

private:
    void apply();
    // A nativeLook colour, or invalid when the host did not send it.
    QColor lookColor(const char* name) const;
    // Native radius, already 0 when the host's corners are square.
    double lookRadius(const char* name, double fallback) const;
    // A translucent token lands on an opaque surface before it is drawn.
    static QColor over(const QColor& translucent, const QColor& base);
    static QColor fallbackColor(const char* name, bool dark);
    // The system accent, or a calm blue when Windows reports a grey one.
    static QColor systemAccent();

    bool m_dark = false;
    QString m_style = QStringLiteral("fluent");
    QString m_uiSize = QStringLiteral("normal");
    bool m_rounded = true;
    bool m_native = false;
    bool m_backdrop = false;
    // What the host asked for. Applied only when the machine can draw it.
    bool m_transparency = false;
    QString m_renderer;
    QVariantMap m_look;
    QColor m_window, m_surface, m_surfaceAlt, m_page, m_faint, m_pressed;
    QColor m_control, m_controlHover, m_controlBorder, m_menuBackground, m_menuHover, m_menuEdge, m_windowEdge;
    QColor m_handleFill, m_border, m_scrollThumb;
    QColor m_text, m_textMuted, m_selection, m_hoverRow;
    QColor m_accent, m_accentLine, m_textSelection, m_caret, m_accentText, m_ok, m_warn, m_error;
    int m_radius = 4;
    int m_controlHeight = 26;
    int m_fontSize = 11;
    int m_fontBody = 14, m_fontCaption = 12, m_fontSmall = 11;
    int m_rowHeight = 32, m_fileRowHeight = 30, m_iconSize = 18, m_gap = 8, m_pad = 12;
    double m_cardRadius = 8, m_controlRadius = 6, m_smallRadius = 4;
    QString m_fontFamily = QStringLiteral("Segoe UI");
    int m_localMode = 0;
    bool m_hostNative = false;
    bool m_hostDark = false;
    QVariantMap m_hostLook;
};
