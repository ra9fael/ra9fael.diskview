#pragma once

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

// The window's whole look, as named tokens: every colour the QML and the
// chart use comes from here, so the window follows the host's light/dark
// choice in one place instead of scattered literals. The accent is the
// system's, read the same way WinShun's own SystemTheme reads it (the
// application palette), which is what keeps a plugin's controls and a
// selection the colour the user picked for Windows.
//
// The theme string may carry a style suffix ("light", "dark", but also
// "dark|tactile" once a host sends one); the suffix only nudges the shape
// tokens, the palette follows light/dark and the system accent.
class ThemeBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
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
    Q_PROPERTY(QColor ok READ ok NOTIFY changed)
    Q_PROPERTY(QColor warn READ warn NOTIFY changed)
    Q_PROPERTY(QColor error READ error NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(int controlHeight READ controlHeight NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY changed)

public:
    explicit ThemeBridge(QObject* parent = nullptr);

    bool dark() const { return m_dark; }
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
    QColor ok() const { return m_ok; }
    QColor warn() const { return m_warn; }
    QColor error() const { return m_error; }
    int radius() const { return m_radius; }
    int controlHeight() const { return m_controlHeight; }
    int fontSize() const { return m_fontSize; }

    // "light" or "dark", with an optional "|fluent|brand|tactile|liquid"
    // suffix. Unknown strings keep the current look.
    void setTheme(const QString& theme);
    // The named Phosphor icon, as {text, family}; empty when the name is not
    // one of the bundled set (Glyph.qml then draws nothing, never a wrong
    // character from another font).
    Q_INVOKABLE QVariantMap phosphorIcon(const QString& name, bool filled = false) const;

signals:
    void changed();

private:
    void apply();
    // The system accent, or a calm blue when Windows reports a grey one (the
    // guard is the one SystemTheme keeps: a grey accent would make a
    // selection invisible).
    static QColor systemAccent();

    bool m_dark = false;
    QString m_style;
    QColor m_window, m_surface, m_surfaceAlt, m_handleFill, m_border;
    QColor m_text, m_textMuted, m_selection, m_hoverRow;
    QColor m_accent, m_ok, m_warn, m_error;
    int m_radius = 4;
    int m_controlHeight = 26;
    int m_fontSize = 11;
};
