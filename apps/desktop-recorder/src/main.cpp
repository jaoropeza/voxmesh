#include "backend_factory.hpp"
#include "recorder_controller.hpp"

#include "voxmesh/core/version.hpp"
#include "voxmesh/stt/testing/mock_stt_server.hpp"

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

    // Phase 2 (§31): the app hosts the mock STT server in-process on
    // localhost; the real media gateway replaces this endpoint in Phase 3.
    voxmesh::stt::testing::MockSttStreamServer mockSttServer;
    if (mockSttServer.start()) {
        controller.setSttEndpoint(QStringLiteral("127.0.0.1:%1").arg(mockSttServer.port()));
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"), QGuiApplication::applicationVersion());
    engine.rootContext()->setContextProperty(QStringLiteral("recorder"), &controller);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule("VoxMesh.Recorder", "Main");

    return QGuiApplication::exec();
}
