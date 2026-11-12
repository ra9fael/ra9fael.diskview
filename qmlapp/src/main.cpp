#include "QmlHost.h"

#include <QDir>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTextStream>
#include <QTimer>

#include <windows.h>

namespace {

// The offscreen platform uses the FreeType font database instead of the
// Windows one, and it looks nowhere for fonts unless QT_QPA_FONTDIR points
// at a directory: without it every glyph comes out as an empty box.
void ensureFontDir()
{
    if (!qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
        return;
    }
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM").toLower();
    if (platform != "offscreen" && platform != "minimal") {
        return;
    }
    wchar_t windowsDir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windowsDir, MAX_PATH) == 0) {
        return;
    }
    const QDir fonts(QString::fromWCharArray(windowsDir) + "/Fonts");
    if (fonts.exists()) {
        qputenv("QT_QPA_FONTDIR", fonts.path().toUtf8());
    }
}

} // namespace

int main(int argc, char** argv)
{
    ensureFontDir();
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Disk View QML");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QQuickWindow::setGraphicsApi(qEnvironmentVariable("QT_QUICK_BACKEND") == "software"
                                     ? QSGRendererInterface::Software
                                     : QSGRendererInterface::Direct3D11);

    QmlHost::Options options;
    QString error;
    if (!QmlHost::parseArgs(app.arguments(), &options, &error)) {
        QTextStream(stderr) << "error: " << error << "\n" << QmlHost::usage();
        return 2;
    }
    if (options.help) {
        QTextStream(stdout) << QmlHost::usage();
        return 0;
    }

    QmlHost host(options);
    if (!host.load(&error)) {
        QTextStream(stderr) << "error: " << error << "\n";
        return 3;
    }

    if (options.selfTest || !options.screenshotPath.isEmpty()) {
        QTimer::singleShot(options.screenshotDelayMs, &app, [&] {
            int code = 0;
            if (options.selfTest) {
                QString report;
                const bool passed = host.runSelfTest(&report);
                QTextStream(stdout) << report;
                if (!passed) code = 1;
            }
            if (!options.screenshotPath.isEmpty() && !host.saveScreenshot(options.screenshotPath)) {
                QTextStream(stderr) << "error: could not save " << options.screenshotPath << "\n";
                code = 1;
            }
            app.exit(code);
        });
    }
    return app.exec();
}
