#include <auroraapp.h>
#include <QtQuick>
#include <QQmlContext>
#include "audiocontroller.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> application(Aurora::Application::application(argc, argv));
    application->setOrganizationName(QStringLiteral("ru.auroraos"));
    application->setApplicationName(QStringLiteral("AudioHub"));

    QScopedPointer<QQuickView> view(Aurora::Application::createView());

    AudioController audioController;
    // Пока брокера нет — строку с подключением можно закомментировать:
    // audioController.connectToBroker("192.168.1.100", 1883);

    view->rootContext()->setContextProperty("audioController", &audioController);

    // Загружаем корневой файл приложения Авроры
    view->setSource(Aurora::Application::pathTo(QStringLiteral("qml/mp3_player_aurora.qml")));
    view->show();

    return application->exec();
}
