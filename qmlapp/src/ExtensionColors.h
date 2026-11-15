#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringView>

#include <iterator>

// The one place that decides what colour an extension gets. The chart fills
// the tiles with it and the legend shows the very same colour, so a tile can
// always be traced back to its row.
namespace diskviewqml {

// A categorical palette with similar chroma and lightness: Okabe–Ito's
// colour-blind-safe core first, then a second ring at the same weight. Adjacent
// entries differ in hue, and the grey is reserved for folders so a big tile is
// never mistaken for one.
inline const QColor kExtensionPalette[] = {
    QColor("#0072B2"), QColor("#E69F00"), QColor("#009E73"), QColor("#CC79A7"),
    QColor("#56B4E9"), QColor("#D55E00"), QColor("#4D4D4D"), QColor("#8C6D31"),
    QColor("#5F4690"), QColor("#1C7C54"), QColor("#B03A5B"), QColor("#2E86AB"),
    QColor("#C58B00"), QColor("#6B8E23"), QColor("#A0522D"), QColor("#3D5A80"),
    QColor("#EE6C4D"), QColor("#7B2CBF"), QColor("#2A9D8F"), QColor("#E9C46A"),
};

inline QColor extensionColor(const QString& extension)
{
    // A stable hash, so the colour of an extension does not move between
    // scans or between the chart and the legend.
    quint32 hash = 0;
    for (const QChar ch : extension) {
        hash = hash * 31u + ch.unicode();
    }
    return kExtensionPalette[hash % std::size(kExtensionPalette)];
}

inline QColor textOnColor(const QColor& fill)
{
    const double luminance = 0.2126 * fill.redF() + 0.7152 * fill.greenF() + 0.0722 * fill.blueF();
    return luminance > 0.55 ? QColor("#1b2b3a") : QColor("#ffffff");
}

// The human name of the common types, the way Windows names them.
inline QString extensionLabel(const QString& extension)
{
    static const QHash<QString, QString> labels{
        {QString(), QStringLiteral("无扩展名")},
        {QStringLiteral(".dll"), QStringLiteral("应用程序扩展")},
        {QStringLiteral(".exe"), QStringLiteral("应用程序")},
        {QStringLiteral(".sys"), QStringLiteral("系统文件")},
        {QStringLiteral(".mp4"), QStringLiteral("MP4 视频")},
        {QStringLiteral(".mkv"), QStringLiteral("Matroska 视频")},
        {QStringLiteral(".avi"), QStringLiteral("AVI 视频")},
        {QStringLiteral(".zip"), QStringLiteral("ZIP 压缩包")},
        {QStringLiteral(".7z"), QStringLiteral("7z 压缩包")},
        {QStringLiteral(".rar"), QStringLiteral("RAR 压缩包")},
        {QStringLiteral(".gz"), QStringLiteral("压缩文件")},
        {QStringLiteral(".iso"), QStringLiteral("光盘映像")},
        {QStringLiteral(".png"), QStringLiteral("PNG 图像")},
        {QStringLiteral(".jpg"), QStringLiteral("JPEG 图像")},
        {QStringLiteral(".jpeg"), QStringLiteral("JPEG 图像")},
        {QStringLiteral(".pdf"), QStringLiteral("PDF 文档")},
        {QStringLiteral(".docx"), QStringLiteral("Word 文档")},
        {QStringLiteral(".xlsx"), QStringLiteral("Excel 表格")},
        {QStringLiteral(".py"), QStringLiteral("Python 源文件")},
        {QStringLiteral(".h"), QStringLiteral("C++ 头文件")},
        {QStringLiteral(".cpp"), QStringLiteral("C++ 源文件")},
        {QStringLiteral(".json"), QStringLiteral("JSON 文件")},
        {QStringLiteral(".txt"), QStringLiteral("文本文件")},
        {QStringLiteral(".log"), QStringLiteral("日志文件")},
        {QStringLiteral(".bin"), QStringLiteral("二进制文件")},
        {QStringLiteral(".dat"), QStringLiteral("数据文件")},
        {QStringLiteral(".pak"), QStringLiteral("资源包")},
    };
    return labels.value(extension, QStringLiteral("其他"));
}

inline QString extensionOf(QStringView name)
{
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot <= 0 ? QString() : name.sliced(dot).toString().toLower();
}

} // namespace diskviewqml
