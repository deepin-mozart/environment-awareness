#include "core/Config.h"
#include "core/EventBus.h"
#include "core/StorageController.h"
#include "dbus/DBusManager.h"
#include "utils/Logger.h"

#include <QCoreApplication>
#include <QTimer>

using namespace Awareness;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("deepin-environment-awareness"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    // 初始化日志
    initLogging();
    awLogInfo() << "Starting deepin-environment-awareness daemon" << app.applicationVersion();

    // 初始化配置
    if (!Config::instance().load()) {
        awLogWarning() << "Failed to load config, using defaults";
    }

    // 初始化存储
    StorageController storage;
    if (!storage.init()) {
        awLogCritical() << "Failed to initialize storage, exiting";
        return 1;
    }

    // 事件总线
    EventBus bus;

    // EventBus → StorageController 持久化
    QObject::connect(&bus, &EventBus::eventReceived, &storage,
                     [&storage](const Event &event) { storage.insertAction(event); });

    // D-Bus 服务
    DBusManager dbus(&storage, &bus);
    if (!dbus.registerService()) {
        awLogCritical() << "Failed to register D-Bus service, exiting";
        return 1;
    }

    // 定时清理（每小时执行一次）
    QTimer cleanupTimer;
    cleanupTimer.setInterval(60 * 60 * 1000); // 1 hour
    cleanupTimer.start();
    QObject::connect(&cleanupTimer, &QTimer::timeout, &storage, &StorageController::cleanup);

    // 启动时执行一次清理
    QTimer::singleShot(5000, &storage, &StorageController::cleanup);

    // Sensor 初始化点：
    // 各 Sensor 在后续实现后，在此处创建并 start()
    // Sensor → EventBus::publish() → StorageController + D-Bus 信号
    //
    // 示例（框架阶段注释掉）：
    //   WindowSensor windowSensor(&bus);
    //   if (Config::instance().sensorEnabled("window") && windowSensor.start())
    //       awLogInfo() << "WindowSensor started";
    //   else
    //       awLogWarning() << "WindowSensor failed to start";

    awLogInfo() << "Daemon started, entering event loop";
    return app.exec();
}
