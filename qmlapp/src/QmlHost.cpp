#include "QmlHost.h"

#include "ChartItem.h"
#include "DiskViewModel.h"
#include "ScanService.h"

#include <QDir>
#include <QFileInfo>
#include <QQmlContext>
#include <QQuickWindow>
#include <QUrl>

namespace {
const char* kBuiltInFixture = ":/fixtures/disk-tree.json";
}

QmlHost::QmlHost(Options options, QObject* parent) : QObject(parent), m_options(std::move(options)) {}

bool QmlHost::load(QString* error)
{
    m_engine.rootContext()->setContextProperty("diskViewMode", m_options.viewMode);
    m_engine.rootContext()->setContextProperty("fixtureSource",
                                               m_options.fixture.isEmpty() ? QString::fromLatin1(kBuiltInFixture) : m_options.fixture);
    m_engine.rootContext()->setContextProperty("initialScanPath", m_options.scanPath);

    // The scanner is a plain object owned by the host and handed to the QML as
    // a context property: the same instance drives the window and the command
    // line.
    m_scanService = new ScanService(this);
    m_engine.rootContext()->setContextProperty("scanService", m_scanService);

    if (m_options.qmlDir.isEmpty()) {
        m_engine.loadFromModule("DiskViewPrototype", "Main");
    } else {
        const QString mainQml = QDir(m_options.qmlDir).filePath("Main.qml");
        if (!QFileInfo::exists(mainQml)) {
            *error = QStringLiteral("no Main.qml under %1").arg(m_options.qmlDir);
            return false;
        }
        m_engine.load(QUrl::fromLocalFile(mainQml));
    }

    if (m_engine.rootObjects().isEmpty()) {
        *error = m_options.qmlDir.isEmpty()
                     ? QStringLiteral("the built-in QML module failed to load")
                     : QStringLiteral("Main.qml failed to load from %1").arg(m_options.qmlDir);
        return false;
    }
    return true;
}

bool QmlHost::startScan(const QString& path, QString* error)
{
    if (!m_scanService) {
        *error = QStringLiteral("the scan service is not available");
        return false;
    }
    connect(m_scanService, &ScanService::finished, this, [this] { emit scanFinished(); });
    connect(m_scanService, &ScanService::failed, this, [this](const QString& message) { emit scanFailed(message); });
    if (!m_scanService->start(path)) {
        *error = QStringLiteral("a scan is already running");
        return false;
    }
    return true;
}

bool QmlHost::runSelfTest(QString* report)
{
    auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
    auto* chart = window ? window->findChild<ChartItem*>(QStringLiteral("diskViewChart")) : nullptr;
    auto* model = window ? window->findChild<DiskViewModel*>(QStringLiteral("diskModel")) : nullptr;
    if (!chart || !model) {
        *report = QStringLiteral("FAIL\tmissing chart or model\n");
        return false;
    }

    const QString hit = chart->firstRegionHit();
    const bool regions = chart->validateHitTest();
    const bool transform = !hit.isEmpty() && chart->validateHitTransform();
    const QString rootPath = model->currentPath();
    bool navigate = false;
    bool back = false;
    if (m_options.scanPath.isEmpty()) {
        model->navigate(QStringLiteral("2"));
        navigate = model->currentPath().contains(QStringLiteral("WinShun"));
        model->goUp();
        back = model->currentPath() == rootPath;
    } else {
        // A scanned tree has no fixed layout: enter the first directory of the
        // list and come back.
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            if (!model->data(index, DiskViewModel::DirectoryRole).toBool()) {
                continue;
            }
            model->navigate(model->data(index, DiskViewModel::IdRole).toString());
            navigate = model->currentPath() != rootPath;
            model->goUp();
            back = model->currentPath() == rootPath;
            break;
        }
    }
    const bool pass = !hit.isEmpty() && regions && transform && navigate && back;

    *report = QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7\n")
                  .arg(m_options.viewMode)
                  .arg(hit)
                  .arg(regions ? QStringLiteral("regions") : QStringLiteral("no-regions"))
                  .arg(transform ? QStringLiteral("transform") : QStringLiteral("bad-transform"))
                  .arg(navigate ? QStringLiteral("navigate") : QStringLiteral("bad-navigate"))
                  .arg(back ? QStringLiteral("return") : QStringLiteral("bad-return"))
                  .arg(pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    return pass;
}

bool QmlHost::saveScreenshot(const QString& path)
{
    auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
    if (!window) return false;
    return window->grabWindow().save(path);
}

bool QmlHost::parseArgs(const QStringList& arguments, Options* options, QString* error)
{
    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        const auto value = [&](const QString& name) -> QString {
            if (i + 1 >= arguments.size()) {
                *error = QStringLiteral("%1 needs a value").arg(name);
                return {};
            }
            return arguments.at(++i);
        };

        if (argument == QStringLiteral("--help") || argument == QStringLiteral("-h")) {
            options->help = true;
        } else if (argument == QStringLiteral("--qml-dir")) {
            options->qmlDir = value(argument);
        } else if (argument == QStringLiteral("--fixture")) {
            options->fixture = value(argument);
        } else if (argument == QStringLiteral("--scan")) {
            options->scanPath = value(argument);
        } else if (argument == QStringLiteral("--view-mode")) {
            bool ok = false;
            const int mode = value(argument).toInt(&ok);
            if (!ok || (mode != 0 && mode != 1)) {
                *error = QStringLiteral("--view-mode takes 0 (treemap) or 1 (sunburst)");
                return false;
            }
            options->viewMode = mode;
        } else if (argument == QStringLiteral("--screenshot")) {
            options->screenshotPath = value(argument);
        } else if (argument == QStringLiteral("--delay")) {
            bool ok = false;
            const int delay = value(argument).toInt(&ok);
            if (!ok || delay < 0) {
                *error = QStringLiteral("--delay takes milliseconds");
                return false;
            }
            options->screenshotDelayMs = delay;
        } else if (argument == QStringLiteral("--self-test")) {
            options->selfTest = true;
        } else {
            *error = QStringLiteral("unknown option %1").arg(argument);
            return false;
        }

        if (!error->isEmpty()) return false;
    }
    return true;
}

QString QmlHost::usage()
{
    return QStringLiteral(
        "Usage: diskview-qml [options]\n"
        "  --qml-dir <dir>    load Main.qml from <dir> instead of the built-in module\n"
        "  --fixture <file>   disk tree JSON; the built-in fixture is used by default\n"
        "  --scan <path>      scan <path> for real and show the result\n"
        "  --view-mode <0|1>  0 = treemap, 1 = sunburst\n"
        "  --screenshot <png> save a window grab and exit\n"
        "  --delay <ms>       wait before the grab (default 1200)\n"
        "  --self-test        run the interaction checks and print the verdict\n"
        "  -h, --help         show this help\n");
}
