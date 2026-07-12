#include "backend_factory.hpp"
#include "recorder_controller.hpp"

#include "voxmesh/core/version.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("VoxMesh Recorder"));
    QGuiApplication::setOrganizationName(QStringLiteral("VoxMesh"));
    QGuiApplication::setApplicationVersion(QString::fromStdString(voxmesh::core::projectVersion().toString()));

    const auto backend = voxmesh::app::createPlatformCaptureBackend();
    voxmesh::app::RecorderController controller(*backend);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"), QGuiApplication::applicationVersion());
    engine.rootContext()->setContextProperty(QStringLiteral("recorder"), &controller);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule("VoxMesh.Recorder", "Main");

    return QGuiApplication::exec();
}
