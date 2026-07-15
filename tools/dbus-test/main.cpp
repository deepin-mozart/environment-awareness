#include "DBusTestWindow.h"

#include <QApplication>
#include <QDBusMetaType>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("deepin-awareness-dbus-test"));

    // 注册 D-Bus 元类型
    qDBusRegisterMetaType<QList<QVariantMap>>();

    DBusTestWindow window;
    window.show();

    return app.exec();
}
