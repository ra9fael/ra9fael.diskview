#include "QmlHost.h"

#include <QDir>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
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

// That font database resolves the generic "Sans Serif" to a font whose Latin
// glyphs are wrong, so the family is named outright. The list is the fallback
// chain: a CJK font takes over the characters the first one does not have.
void ensureApplicationFont()
{
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM").toLower();
    if (platform != "offscreen" && platform != "minimal") {
        return;
    }
    QFont font;
    font.setFamilies({"Segoe UI", "Microsoft YaHei", "Noto Sans SC", "SimHei", "Arial"});
    font.setStyleStrategy(QFont::PreferAntialias);
    QGuiApplication::setFont(font);
}

} // namespace

int main(int argc, char** argv)
{
    ensureFontDir();
    QGuiApplication app(argc, argv);
    ensureApplicationFont();
    QGuiApplication::setApplicationName("Disk View QML");
    // Dialogs and secondary windows take it from here; the taskbar icon comes
    // from the .rc resource compiled into the executable.
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/app.ico")));
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

    bool checksDone = false;
    const auto runChecks = [&] {
        if (checksDone) {
            return;
        }
        checksDone = true;
        int code = 0;
        if (!options.scanPath.isEmpty()) {
            QTextStream(stdout) << host.scanStatusLine();
        }
        if (options.selfTest && options.pauseOnStart) {
            // A held scan has no tree to interact with yet.
            QTextStream(stdout) << "self-test skipped: the scan is held\n";
        } else if (options.selfTest) {
            QString report;
            const bool passed = host.runSelfTest(&report);
            QTextStream(stdout) << report;
            if (!passed) code = 1;
        }
        if (options.benchNodes > 0) {
            QString report;
            if (!host.runChartBenchmark(options.benchNodes, &report)) {
                code = 1;
            }
            QTextStream(stdout) << report;
        }
        if (!options.screenshotPath.isEmpty() && !host.saveScreenshot(options.screenshotPath)) {
            QTextStream(stderr) << "error: could not save " << options.screenshotPath << "\n";
            code = 1;
        }
        app.exit(code);
    };

    if (options.openSettings) {
        if (!host.openSettingsDialog(&error)) {
            QTextStream(stderr) << "error: " << error << "\n";
            return 5;
        }
    }

    if (!options.scanPath.isEmpty()) {
        // The scan decides when the window is ready: a fixed delay would be
        // either too short or a waste.
        if (!host.startScan(options.scanPath, &error)) {
            QTextStream(stderr) << "error: " << error << "\n";
            return 4;
        }
        QObject::connect(&host, &QmlHost::scanFinished, &app, [&] {
            QTimer::singleShot(400, &app, runChecks);
        });
        QObject::connect(&host, &QmlHost::scanFailed, &app, [&](const QString& message) {
            QTextStream(stderr) << "error: scan failed: " << message << "\n";
            app.exit(4);
        });
        if (options.pauseOnStart) {
            // A held scan never finishes on its own, so the checks run on the
            // delay instead.
            QTimer::singleShot(options.screenshotDelayMs, &app, runChecks);
        }
    } else if (options.selfTest || options.benchNodes > 0 || !options.screenshotPath.isEmpty()) {
        QTimer::singleShot(options.screenshotDelayMs, &app, runChecks);
    }
    return app.exec();
}
