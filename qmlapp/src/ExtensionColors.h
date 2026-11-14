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

inline const QColor kExtensionPalette[] = {
    QColor("#2f7ed8"), QColor("#16a085"), QColor("#e67e22"), QColor("#8e67c7"),
    QColor("#d65b6b"), QColor("#0f9fb4"), QColor("#c4932d"), QColor("#6d9d3b"),
    QColor("#c45b96"), QColor("#547ec3"), QColor("#e0a52d"), QColor("#2b9b91"),
    QColor("#b85e5e"), QColor("#78a83e"), QColor("#526fae"), QColor("#c17a43"),
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
