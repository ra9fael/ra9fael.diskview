#pragma once

#include <QColor>
#include <QObject>
#include <QVariantMap>
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
    Q_PROPERTY(QColor window READ window NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY changed)
    Q_PROPERTY(QColor handleFill READ handleFill NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed)
    Q_PROPERTY(QColor hoverRow READ hoverRow NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    // Text/icon colour that reads on accentFill.
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor ok READ ok NOTIFY changed)
    Q_PROPERTY(QColor warn READ warn NOTIFY changed)
    Q_PROPERTY(QColor error READ error NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(int controlHeight READ controlHeight NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY changed)

public:
    explicit ThemeBridge(QObject* parent = nullptr);

    bool dark() const { return m_dark; }
    QString style() const { return m_style; }
    bool rounded() const { return m_rounded; }
    bool native() const { return m_native; }
    QColor window() const { return m_window; }
    QColor surface() const { return m_surface; }
    QColor surfaceAlt() const { return m_surfaceAlt; }
    QColor handleFill() const { return m_handleFill; }
    QColor border() const { return m_border; }
    QColor text() const { return m_text; }
    QColor textMuted() const { return m_textMuted; }
    QColor selection() const { return m_selection; }
    QColor hoverRow() const { return m_hoverRow; }
    QColor accent() const { return m_accent; }
    QColor accentText() const { return m_accentText; }
    QColor ok() const { return m_ok; }
    QColor warn() const { return m_warn; }
    QColor error() const { return m_error; }
    int radius() const { return m_radius; }
    int controlHeight() const { return m_controlHeight; }
    int fontSize() const { return m_fontSize; }

    // "light" or "dark", with an optional "|fluent|brand|tactile|liquid"
    // suffix. Unknown strings keep the current look.
    void setTheme(const QString& theme);
    // The host's appearance object (initialize's `appearance` or an
    // `appearance` notification), as a QVariantMap. Empty clears it.
    void setAppearance(const QVariantMap& appearance);
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
    QVariantMap m_look;
    QColor m_window, m_surface, m_surfaceAlt, m_handleFill, m_border;
    QColor m_text, m_textMuted, m_selection, m_hoverRow;
    QColor m_accent, m_accentText, m_ok, m_warn, m_error;
    int m_radius = 4;
    int m_controlHeight = 26;
    int m_fontSize = 11;
};
