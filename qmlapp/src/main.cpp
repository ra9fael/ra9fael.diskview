#include "ChartItem.h"
#include "DiskViewModel.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QUrl>
#include <QQmlContext>
#include <QDir>
#include <QTimer>
#include <QFile>
#include <QTextStream>

int main(int argc,char**argv)
{
    QGuiApplication app(argc,argv);
    QGuiApplication::setApplicationName("DiskView QML Prototype");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QQuickWindow::setGraphicsApi(qEnvironmentVariable("QT_QUICK_BACKEND") == "software"
        ? QSGRendererInterface::Software : QSGRendererInterface::Direct3D11);
    qmlRegisterType<DiskViewModel>("DiskViewPrototype",1,0,"DiskViewModel");
    qmlRegisterType<ChartItem>("DiskViewPrototype",1,0,"ChartItem");
    qputenv("DISKVIEW_VIEW_MODE", qEnvironmentVariable("DISKVIEW_VIEW_MODE", "0").toUtf8());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("diskViewMode", qEnvironmentVariableIntValue("DISKVIEW_VIEW_MODE"));
    const QString qmlDir = qEnvironmentVariable("DISKVIEW_QML_DIR", QCoreApplication::applicationDirPath()+"/../qml");
    const QUrl url=QUrl::fromLocalFile(QDir(qmlDir).filePath("Main.qml"));
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreated,&app,[url](QObject*object,const QUrl&objUrl){if(!object&&objUrl==url)QCoreApplication::exit(1);},Qt::QueuedConnection);
    engine.load(url);
    if (qEnvironmentVariableIsSet("DISKVIEW_SCREENSHOT")) {
        QTimer::singleShot(1400, &app, [&engine, &app] {
            if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0))) {
                auto* chart=qobject_cast<ChartItem*>(window->property("chartItem").value<QObject*>());
                QString hit=chart?chart->firstRegionHit():QString();
                bool hitRegions=chart&&chart->validateHitTest();
                bool transformedHit=false;
                if(chart&&!hit.isEmpty()) {
                    transformedHit=chart->validateHitTransform();
                }
                bool modelNavigation=false,modelReturn=false;
                auto* modelObject=qobject_cast<DiskViewModel*>(window->property("diskModelItem").value<QObject*>());
                if(modelObject) {
                    const QString rootPath=modelObject->currentPath();
                    modelObject->navigate(QStringLiteral("2"));
                    modelNavigation=modelObject->currentPath().contains(QStringLiteral("WinShun"));
                    modelObject->goUp();
                    modelReturn=modelObject->currentPath()==rootPath;
                }
                const bool checksPassed=hitRegions&&transformedHit&&modelNavigation&&modelReturn;
                const QString resultPath=qEnvironmentVariable("DISKVIEW_HIT_RESULT");
                if(!resultPath.isEmpty()) {
                    QFile result(resultPath);
                    if(result.open(QIODevice::WriteOnly|QIODevice::Text)) {
                        QTextStream out(&result);
                        out<<(chart?chart->viewMode():-1)<<"\t"<<hit<<"\n"<<(hitRegions?"regions":"no-regions")<<"\t"<<(transformedHit?"transform":"bad-transform")<<"\t"<<(modelNavigation?"navigate":"bad-navigate")<<"\t"<<(modelReturn?"return":"bad-return")<<"\t"<<(chart&&checksPassed?"PASS":"FAIL")<<"\n";
                    }
                }
                if(!chart) qWarning("diskview prototype chart object not found");
                else if(hit.isEmpty()) qWarning("diskview prototype hit-test found no item");
                window->grabWindow().save(qEnvironmentVariable("DISKVIEW_SCREENSHOT"));
            }
            app.quit();
        });
    }
    return app.exec();
}
