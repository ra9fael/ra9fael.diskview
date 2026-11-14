#include "QmlHost.h"

#include "ChartItem.h"
#include "DiskViewModel.h"
#include "ScanService.h"
#include "ScanSettings.h"
#include "ThemeBridge.h"
#include "Phosphor.h"

#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <windows.h>
#include <shellapi.h>

namespace {
const char* kBuiltInFixture = ":/fixtures/disk-tree.json";
}

QmlHost::QmlHost(Options options, QObject* parent) : QObject(parent), m_options(std::move(options)) {}

bool QmlHost::load(QString* error)
{
    // The host's own QtQuick modules, when the app is a plugin on WinShun's Qt
    // rather than on its own deployment. Later paths win for a module that
    // exists in more than one.
    for (const QString& path : m_options.importPaths) {
        if (!path.isEmpty()) m_engine.addImportPath(path);
    }
    m_engine.rootContext()->setContextProperty("diskViewMode", m_options.viewMode);
    m_engine.rootContext()->setContextProperty("fixtureSource",
                                               m_options.fixture.isEmpty() ? QString::fromLatin1(kBuiltInFixture) : m_options.fixture);

    // The scanner is a plain object owned by the host and handed to the QML as
    // a context property: the same instance drives the window and the command
    // line. The settings decide how the next scan runs.
    m_scanService = new ScanService(this);
    m_scanSettings = new ScanSettings(this);
    m_scanSettings->load();
    // The host's own settings, when this window is a plugin: they win over the
    // local file for the keys the host shows.
    if (!m_options.hostSettings.isEmpty()) {
        applyHostSettings(m_options.hostSettings);
    }
    m_scanService->setSettings(m_scanSettings);
    m_engine.rootContext()->setContextProperty("scanService", m_scanService);
    m_engine.rootContext()->setContextProperty("scanSettings", m_scanSettings);
    // The settings page in WinShun owns these; the window's dialog must not
    // offer a second place to change them.
    m_engine.rootContext()->setContextProperty("hostManagedSettings", !m_options.hostSettings.isEmpty());
    // The window asks the host for the two things QML cannot do on its own.
    m_engine.rootContext()->setContextProperty("appHost", this);
    // The look: one token set the whole window and the chart read. The icon
    // fonts have to be in place before the engine draws its first glyph.
    ws::phosphor::loadFonts();
    m_theme = new ThemeBridge(this);
    m_engine.rootContext()->setContextProperty("theme", m_theme);
    // A path given on the command line wins over the remembered one.
    m_engine.rootContext()->setContextProperty(
        "initialScanPath",
        m_options.scanPath.isEmpty() ? m_scanSettings->lastPath() : m_options.scanPath);

    // The engine only says why a load failed through this signal.
    connect(&m_engine, &QQmlApplicationEngine::warnings, this, [this](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            m_loadWarnings << warning.toString();
        }
    });

    if (m_options.qmlDir.isEmpty()) {
        m_engine.loadFromModule("DiskViewPrototype", "Main");    } else {
        const QString mainQml = QDir(m_options.qmlDir).filePath("Main.qml");
        if (!QFileInfo::exists(mainQml)) {
            *error = QStringLiteral("no Main.qml under %1").arg(m_options.qmlDir);
            return false;
        }
        m_engine.load(QUrl::fromLocalFile(mainQml));
    }

    if (m_engine.rootObjects().isEmpty()) {
        // The engine reports the reasons through its warnings signal, which is
        // the only way to say why the module did not load.
        *error = m_options.qmlDir.isEmpty()
                     ? QStringLiteral("the built-in QML module failed to load")
                     : QStringLiteral("Main.qml failed to load from %1").arg(m_options.qmlDir);
        if (!m_loadWarnings.isEmpty()) {
            *error += QStringLiteral(": ") + m_loadWarnings.join(QStringLiteral("; "));
        }
        return false;
    }
    if (auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0))) {
        // The same icon the native (GDI) edition shows: it embeds app.ico in
        // its rc and picks it up through WM_SETICON. The resource is compiled
        // into this binary, so both editions show one icon.
        window->setIcon(QIcon(QStringLiteral(":/app.ico")));
    }
    return true;
}

void QmlHost::applyHostSettings(const QVariantMap& settings)
{
    if (!m_scanSettings) return;
    const QString mode = settings.value(QStringLiteral("scanMode")).toString();
    m_scanSettings->setScanMode(mode == QLatin1String("fast") ? 1 : mode == QLatin1String("ordinary") ? 2 : 0);
    const QString workers = settings.value(QStringLiteral("workers")).toString();
    m_scanSettings->setWorkers(workers == QLatin1String("auto") ? 0 : workers.toInt());
    const QString media = settings.value(QStringLiteral("media")).toString();
    m_scanSettings->setMedia(media == QLatin1String("ssd") ? 1 : media == QLatin1String("hdd") ? 2
                             : media == QLatin1String("network") ? 3 : 0);
    const QString preview = settings.value(QStringLiteral("preview")).toString();
    m_scanSettings->setPreviewIntervalMs(preview == QLatin1String("less") ? 5000 : 1500);
}

QQuickWindow* QmlHost::window() const
{
    if (m_engine.rootObjects().isEmpty()) return nullptr;
    return qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
}

bool QmlHost::startScan(const QString& path, QString* error)
{
    if (!m_scanService) {
        *error = QStringLiteral("the scan service is not available");
        return false;
    }
    connect(m_scanService, &ScanService::finished, this, [this] { emit scanFinished(); });
    connect(m_scanService, &ScanService::failed, this, [this](const QString& message) { emit scanFailed(message); });

    if (m_options.pauseOnStart) {
        if (!m_scanService->startPaused(path)) {
            *error = QStringLiteral("a scan is already running");
            return false;
        }
        return true;
    }
    if (m_options.selfTest) {
        // The self-test starts the scan paused and lets it go again: a scan
        // that is held must produce nothing, and it has to go on when it is
        // resumed.
        if (!m_scanService->startPaused(path)) {
            *error = QStringLiteral("a scan is already running");
            return false;
        }
        QTimer::singleShot(400, this, [this] {
            auto* model = m_engine.rootObjects().isEmpty()
                              ? nullptr
                              : m_engine.rootObjects().value(0)->findChild<DiskViewModel*>(
                                    QStringLiteral("diskModel"));
            m_pauseVerified = m_scanService->paused() && (!model || model->rowCount() == 0);
            m_scanService->resume();
        });
        return true;
    }
    if (!m_scanService->start(path)) {
        *error = QStringLiteral("a scan is already running");
        return false;
    }
    return true;
}

QString QmlHost::scanStatusLine() const
{
    if (!m_scanService) {
        return {};
    }
    const bool fast = m_scanService->fast();
    QString line = QStringLiteral("scan\tstate=%1\tmode=%2\tworkers=%3\texcluded=%4\tpartials=%5\tfiles=%6\tdirs=%7\tskipped=%8")
                       .arg(m_scanService->paused() ? QStringLiteral("paused") : m_scanService->state())
                       .arg(fast ? QStringLiteral("fast") : QStringLiteral("ordinary"))
                       .arg(m_scanService->scanWorkers())
                       .arg(m_scanService->excludedItems())
                       .arg(m_scanService->partialCount())
                       .arg(m_scanService->fileCount())
                       .arg(m_scanService->dirCount())
                       .arg(m_scanService->skippedDirs());
    // Whether the process could read the MFT at all: the fast scan exists
    // only for the elevated Win顺 runner's plugins.
    line += QStringLiteral("\televated=%1").arg(m_scanService->elevated() ? QStringLiteral("true") : QStringLiteral("false"));
    if (!m_scanService->fallbackReason().isEmpty()) {
        line += QStringLiteral("\tfallback=%1").arg(m_scanService->fallbackReason());
    }
    return line + QStringLiteral("\n");
}

namespace {
// A tree shaped like a disk: a dozen top folders, each holding one flat level
// of files, so the benchmark's node count is exact and its layout is realistic.
QVariantMap syntheticTree(int nodes)
{
    const int topCount = 12;
    const int perTop = std::max(1, (nodes - 1) / topCount);
    std::uint64_t state = 88172645463325252ull;
    const auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    QVariantList top;
    double total = 0;
    for (int t = 0; t < topCount; ++t) {
        QVariantList files;
        double folderTotal = 0;
        for (int i = 0; i < perTop; ++i) {
            const double size = double(1024 + (next() % (8u << 20)));
            folderTotal += size;
            files.push_back(QVariantMap{{"name", QStringLiteral("f_%1_%2.dat").arg(t).arg(i)},
                                        {"size", size}, {"dir", false}});
        }
        total += folderTotal;
        top.push_back(QVariantMap{{"name", QStringLiteral("dir_%1").arg(t)},
                                  {"size", folderTotal}, {"dir", true}, {"children", files}});
    }
    return QVariantMap{{"name", QStringLiteral("bench")}, {"size", total},
                       {"dir", true}, {"children", top}};
}

// The same shape as a flat tree, so the two loaders can be compared.
FlatTree flattenSynthetic(int nodes)
{
    const int topCount = 12;
    const int perTop = std::max(1, (nodes - 1) / topCount);
    FlatTree out;
    out.names.append(QStringLiteral("bench"));
    out.parents.append(-1);
    out.sizes.append(0);
    out.allocs.append(0);
    out.modified.append(0);
    out.attributes.append(0x10);
    out.fileCounts.append(perTop * topCount);
    out.dirCounts.append(topCount);
    out.flags.append(1);
    for (int t = 0; t < topCount; ++t) {
        const int topIndex = out.names.size();
        out.names.append(QStringLiteral("dir_%1").arg(t));
        out.parents.append(0);
        out.sizes.append(perTop * 4096);
        out.allocs.append(perTop * 4096);
        out.modified.append(0);
        out.attributes.append(0x10);
        out.fileCounts.append(perTop);
        out.dirCounts.append(0);
        out.flags.append(1);
        for (int i = 0; i < perTop; ++i) {
            out.names.append(QStringLiteral("f_%1_%2.dat").arg(t).arg(i));
            out.parents.append(topIndex);
            out.sizes.append(4096);
            out.allocs.append(4096);
            out.modified.append(0);
            out.attributes.append(0x20);
            out.fileCounts.append(0);
            out.dirCounts.append(0);
            out.flags.append(0);
        }
    }
    return out;
}
} // namespace

bool QmlHost::runChartBenchmark(int nodes, QString* report)
{
    auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
    auto* chart = window ? window->findChild<ChartItem*>(QStringLiteral("diskViewChart")) : nullptr;
    auto* model = window ? window->findChild<DiskViewModel*>(QStringLiteral("diskModel")) : nullptr;
    if (!chart || !model) {
        *report = QStringLiteral("bench: the chart or the model is missing\n");
        return false;
    }
    const QVariantMap tree = syntheticTree(nodes);
    const FlatTree flat = flattenSynthetic(nodes);
    const int runs = 3;
    QElapsedTimer timer;

    timer.start();
    for (int i = 0; i < runs; ++i) model->loadFlat(flat, false);
    const double modelMs = double(timer.nsecsElapsed()) / 1e6 / runs;

    timer.restart();
    for (int i = 0; i < runs; ++i) model->loadTree(tree);
    const double nestedMs = double(timer.nsecsElapsed()) / 1e6 / runs;

    timer.restart();
    QVariantMap payload;
    for (int i = 0; i < runs; ++i) payload = model->chartTree();
    const double exportMs = double(timer.nsecsElapsed()) / 1e6 / runs;

    chart->setTreeData(payload);
    // Both drawing depths: one level is the default, two is what the 2 层
    // button shows, and the paint cost only shows up there.
    const QString shallow = chart->benchmark(runs);
    chart->setDepth(2);
    const QString deep = chart->benchmark(runs);
    chart->setDepth(1);
    *report = QStringLiteral("bench model flat=%1ms nested=%2ms export=%3ms\nbench depth=1 %4\nbench depth=2 %5\n")
                  .arg(modelMs, 0, 'f', 1)
                  .arg(nestedMs, 0, 'f', 1)
                  .arg(exportMs, 0, 'f', 1)
                  .arg(shallow, deep);
    return true;
}

bool QmlHost::runListChecks(DiskViewModel* model, QString* detail)
{
    if (!model) {
        *detail = QStringLiteral("no-model");
        return false;
    }
    const QString rootPath = model->currentPath();
    const auto value = [&](int row, int role) {
        return model->data(model->index(row, 0), role);
    };

    // The list starts with the biggest entries first.
    bool sizeSorted = true;
    for (int row = 1; row < model->rowCount(); ++row) {
        if (value(row - 1, DiskViewModel::SizeRole).toULongLong() < value(row, DiskViewModel::SizeRole).toULongLong()) {
            sizeSorted = false;
            break;
        }
    }
    // The name head sorts by name and flips the direction when it is clicked
    // again.
    model->toggleSort(DiskViewModel::SortByName);
    bool nameSorted = true;
    for (int row = 1; row < model->rowCount(); ++row) {
        const QString previous = value(row - 1, DiskViewModel::NameRole).toString();
        const QString current = value(row, DiskViewModel::NameRole).toString();
        if (QString::compare(previous, current, Qt::CaseInsensitive) > 0) {
            nameSorted = false;
            break;
        }
    }
    model->toggleSort(DiskViewModel::SortByName);
    model->toggleSort(DiskViewModel::SortBySize);

    // The filter looks at the whole path; a row survives when it contains the
    // text or when it is a folder on the way to one that does.
    const int allRows = model->rowCount();
    const QString sample = allRows > 0 ? value(0, DiskViewModel::NameRole).toString() : QString();
    model->setFilter(sample);
    const int filteredRows = model->rowCount();
    QSet<QString> onTheWay;
    for (int row = 0; row < filteredRows; ++row) {
        const QString path = value(row, DiskViewModel::PathRole).toString();
        if (!path.contains(sample, Qt::CaseInsensitive)) continue;
        for (int cut = path.indexOf(QLatin1Char('\\'), 3); cut > 0; cut = path.indexOf(QLatin1Char('\\'), cut + 1)) {
            onTheWay.insert(path.left(cut));
        }
    }
    bool filterWorks = !sample.isEmpty() && filteredRows >= 1;
    for (int row = 0; row < filteredRows && filterWorks; ++row) {
        const QString path = value(row, DiskViewModel::PathRole).toString();
        if (!path.contains(sample, Qt::CaseInsensitive) && !onTheWay.contains(path)) filterWorks = false;
    }
    model->setFilter(QString());

    // The file view lists the files of the subtree, nothing else.
    model->setViewMode(DiskViewModel::FileView);
    bool filesOnly = model->rowCount() > 0;
    for (int row = 0; row < model->rowCount() && filesOnly; ++row) {
        if (value(row, DiskViewModel::DirectoryRole).toBool()) filesOnly = false;
    }
    model->setViewMode(DiskViewModel::TreeView);

    // Expanding a folder adds its children, collapsing takes them away.
    bool expand = false;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (!value(row, DiskViewModel::DirectoryRole).toBool()) continue;
        if (!value(row, DiskViewModel::HasChildrenRole).toBool()) continue;
        const QString id = value(row, DiskViewModel::IdRole).toString();
        const int before = model->rowCount();
        model->toggleExpanded(id);
        const int after = model->rowCount();
        model->toggleExpanded(id);
        expand = after > before && model->rowCount() == before;
        break;
    }

    // The history goes back and forward again.
    bool history = false;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (!value(row, DiskViewModel::DirectoryRole).toBool()) continue;
        model->navigate(value(row, DiskViewModel::IdRole).toString());
        const QString inside = model->currentPath();
        model->goBack();
        const bool back = model->currentPath() == rootPath;
        model->goForward();
        history = back && inside != rootPath && model->currentPath() == inside;
        model->goBack();
        break;
    }

    QStringList passed;
    if (sizeSorted) passed << QStringLiteral("sort");
    if (nameSorted) passed << QStringLiteral("name");
    if (filterWorks) passed << QStringLiteral("filter");
    if (filesOnly) passed << QStringLiteral("files");
    if (expand) passed << QStringLiteral("expand");
    if (history) passed << QStringLiteral("history");
    *detail = passed.join(QLatin1Char(','));
    return passed.size() == 6;
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
    // A click has to end: after the button is released, moving the mouse over
    // the chart must not keep panning it. The same events first prove that a
    // real drag still pans, so the check cannot pass by doing nothing.
    bool drag = false;
    {
        const QPointF before = chart->pan();
        const QPointF start(10, 10);
        const QPointF moved(60, 60);
        QMouseEvent press(QEvent::MouseButtonPress, start, chart->mapToGlobal(start), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(chart, &press);
        QMouseEvent move(QEvent::MouseMove, moved, chart->mapToGlobal(moved), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(chart, &move);
        const bool dragged = chart->pan() != before;
        QMouseEvent release(QEvent::MouseButtonRelease, moved, chart->mapToGlobal(moved), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(chart, &release);
        const QPointF afterRelease = chart->pan();
        const QPointF hover(200, 200);
        QHoverEvent hoverMove(QEvent::HoverMove, hover, chart->mapToGlobal(hover), hover);
        QCoreApplication::sendEvent(chart, &hoverMove);
        drag = dragged && chart->pan() == afterRelease;
        chart->setPan(before);
    }
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
    // The chart has to answer a real double click: once on a folder tile and,
    // in the sunburst, once in the hole that returns to the parent.
    QStringList chartDetail;
    {
        const auto doubleClick = [&](const QPointF& point) {
            const QPointF global = chart->mapToGlobal(point);
            QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(chart, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(chart, &release);
            QMouseEvent again(QEvent::MouseButtonDblClick, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(chart, &again);
            QMouseEvent done(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(chart, &done);
        };
        // A double click on a folder tile enters the folder, the way the
        // native chart does; the tree's triangle is the in-place opener.
        const QPointF folder = chart->firstDirectoryCenter();
        const QString chartRoot = model->currentPath();
        if (!folder.isNull()) {
            doubleClick(folder);
            QCoreApplication::processEvents();
            if (model->currentPath() != chartRoot) chartDetail << QStringLiteral("double");
        }
        // The centre of the sunburst returns to the parent, so the check goes
        // into a folder and clicks the hole. It runs in the sunburst mode
        // only; the treemap has no hole.
        if (m_options.viewMode == 1) {
            // The folder the chart's double click just entered is the parent
            // the hole returns from.
            const QPointF center(chart->width() / 2, chart->height() / 2);
            QCoreApplication::processEvents();
            doubleClick(center);
            QCoreApplication::processEvents();
            if (model->currentPath() == rootPath) chartDetail << QStringLiteral("center");
        }
        while (model->canGoBack()) model->goBack();
    }
    // The list has to follow the model: opening a folder changes the rows the
    // model reports, and the view only learns about it through the model's own
    // reset. Reading the view's count back is the only way to see that.
    // The interaction checks above open folders; the checks below expect the
    // top level closed, so close whatever was opened.
    const auto collapseAll = [model] {
        QVector<QString> opened;
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            if (model->data(index, DiskViewModel::ExpandedRole).toBool()) {
                opened.push_back(model->data(index, DiskViewModel::IdRole).toString());
            }
        }
        for (const QString& id : opened) model->toggleExpanded(id);
    };
    collapseAll();
    // The list has to follow the model: opening a folder changes the rows the
    // model reports, and the view only learns about it through the model's own
    // reset. Reading the view's count back is the only way to see that.
    bool viewFollows = false;
    if (auto* view = window->findChild<QObject*>(QStringLiteral("diskViewList"))) {
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            if (!model->data(index, DiskViewModel::DirectoryRole).toBool()) continue;
            if (!model->data(index, DiskViewModel::HasChildrenRole).toBool()) continue;
            if (model->data(index, DiskViewModel::ExpandedRole).toBool()) continue;
            const QString id = model->data(index, DiskViewModel::IdRole).toString();
            const int before = model->rowCount();
            model->toggleExpanded(id);
            QCoreApplication::processEvents();
            viewFollows = model->rowCount() > before
                          && view->property("count").toInt() == model->rowCount();
            model->toggleExpanded(id);
            QCoreApplication::processEvents();
            break;
        }
    }
    const bool chartOk = chartDetail.contains(QStringLiteral("double"))
                         && (m_options.viewMode != 1 || chartDetail.contains(QStringLiteral("center")));
    // A scan that could be held and then let go is part of what the scan mode
    // has to show.
    const bool paused = m_options.scanPath.isEmpty() || m_pauseVerified;
    QString listDetail;
    const bool list = runListChecks(model, &listDetail);
    if (viewFollows) {
        if (!listDetail.isEmpty()) listDetail += QStringLiteral(",view");
    } else {
        listDetail = listDetail.isEmpty() ? QStringLiteral("bad-view") : listDetail + QStringLiteral(",bad-view");
    }
    const bool pass = !hit.isEmpty() && regions && transform && navigate && back && paused && list
                      && viewFollows && drag && chartOk;


    *report = QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t%10\n")
                  .arg(m_options.viewMode)
                  .arg(hit)
                  .arg(regions ? QStringLiteral("regions") : QStringLiteral("no-regions"))
                  .arg(transform ? QStringLiteral("transform") : QStringLiteral("bad-transform"))
                  .arg(navigate ? QStringLiteral("navigate") : QStringLiteral("bad-navigate"))
                  .arg(back ? QStringLiteral("return") : QStringLiteral("bad-return"))
                  .arg(listDetail.isEmpty() ? QStringLiteral("bad-list")
                                             : QStringLiteral("list=%1").arg(listDetail))
                  .arg(drag ? QStringLiteral("drag") : QStringLiteral("bad-drag"))
                  .arg(chartOk ? QStringLiteral("chart=%1").arg(chartDetail.join(QLatin1Char(',')))
                               : QStringLiteral("bad-chart:%1").arg(chartDetail.join(QLatin1Char(','))))
                  .arg(pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    if (!m_options.scanPath.isEmpty() && m_scanService) {
        // What the scan made of the settings, and how much of it was published
        // before it was over.
        *report += QStringLiteral("scan\tworkers=%1\texcluded=%2\tpartials=%3\tfiles=%4\tdirs=%5\tskipped=%6\tpause=%7\n")
                      .arg(m_scanService->scanWorkers())
                      .arg(m_scanService->excludedItems())
                      .arg(m_scanService->partialCount())
                      .arg(m_scanService->fileCount())
                      .arg(m_scanService->dirCount())
                      .arg(m_scanService->skippedDirs())
                      .arg(m_pauseVerified ? QStringLiteral("verified") : QStringLiteral("bad"));
    }
    return pass;
}

void QmlHost::setTheme(const QString& theme) const
{
    // The tokens land in one object the whole window reads; the chart keeps
    // its plain light/dark string for its greys, and the accent reaches it
    // through the QML binding on the tokens.
    if (m_theme) m_theme->setTheme(theme);
    if (auto* chart = window()->findChild<ChartItem*>(QStringLiteral("diskViewChart"))) {
        chart->setTheme(theme);
    }
}

void QmlHost::copyToClipboard(const QString& text) const
{
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

void QmlHost::revealInExplorer(const QString& path) const
{
    // A quote in the name would break out of the argument, so such a path is
    // left alone rather than handed to the shell.
    if (path.isEmpty() || path.contains(QLatin1Char('"'))) {
        return;
    }
    const QString argument = QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path));
    ::ShellExecuteW(nullptr, L"open", L"explorer.exe",
                    reinterpret_cast<const wchar_t*>(argument.utf16()), nullptr, SW_SHOWNORMAL);
}

bool QmlHost::openSettingsDialog(QString* error){
    auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
    if (!window) {
        *error = QStringLiteral("the window is not available");
        return false;
    }
    auto* dialog = window->findChild<QObject*>(QStringLiteral("scanSettingsDialog"));
    if (!dialog) {
        *error = QStringLiteral("the scan settings dialog is not part of the window");
        return false;
    }
    if (!QMetaObject::invokeMethod(dialog, "open")) {
        *error = QStringLiteral("the scan settings dialog did not open");
        return false;
    }
    return true;
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
        } else if (argument == QStringLiteral("--settings")) {
            options->openSettings = true;
        } else if (argument == QStringLiteral("--pause")) {
            options->pauseOnStart = true;
        } else if (argument == QStringLiteral("--bench-chart")) {
            bool ok = false;
            const int nodes = value(argument).toInt(&ok);
            if (!ok || nodes < 10) {
                *error = QStringLiteral("--bench-chart takes the number of nodes (at least 10)");
                return false;
            }
            options->benchNodes = nodes;
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
        "  --settings         open the scan settings dialog on startup\n"
        "  --pause            start the scan and hold it there\n"
        "  --bench-chart <n>  build a tree of n nodes and time the chart pipeline\n"
        "  --self-test        run the interaction checks and print the verdict\n"
        "  -h, --help         show this help\n");
}
