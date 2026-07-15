#include "core/Config.h"
#include "core/EventBus.h"
#include "core/StorageController.h"
#include "dbus/DBusManager.h"
#include "sensors/WindowSensor.h"
#include "sensors/FileSensor.h"
#include "sensors/ClipboardSensor.h"
#include "sensors/InputSensor.h"
#include "sensors/SystemSensor.h"
#include "sensors/BrowserSensor.h"
#include "utils/Logger.h"

#include <QApplication>
#include <QDBusMetaType>
#include <QTimer>



using namespace Awareness;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("deepin-environment-awareness"));

    // 注册 D-Bus 元类型（QList<QVariantMap> 用于返回列表数据）
    qDBusRegisterMetaType<QList<QVariantMap>>();
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

    // ========== Sensor 启动 ==========
    // Sensor → EventBus::publish() → StorageController 持久化 + D-Bus 信号转发
    // 每个 Sensor 独立管理生命周期，通过 Config 控制启用/禁用

    QList<ISensor*> sensors;

    auto tryStartSensor = [&](ISensor *sensor) -> bool {
        if (sensor->start()) {
            awLogInfo() << sensor->name() << "sensor started";
            return true;
        }
        awLogWarning() << sensor->name() << "sensor failed to start or disabled";
        return false;
    };

    // 窗口/应用切换监控
    auto *windowSensor = new WindowSensor(&bus);
    sensors.append(windowSensor);
    tryStartSensor(windowSensor);

    // 文件操作监控
    auto *fileSensor = new FileSensor(&bus);
    sensors.append(fileSensor);
    tryStartSensor(fileSensor);

    // 剪贴板监控
    auto *clipboardSensor = new ClipboardSensor(&bus);
    sensors.append(clipboardSensor);
    tryStartSensor(clipboardSensor);

    // 将 sensor 引用传递给 ContextAdaptor
    dbus.setContextSensors(windowSensor, clipboardSensor);

    // 键鼠输入模式监控
    auto *inputSensor = new InputSensor(&bus);
    sensors.append(inputSensor);
    tryStartSensor(inputSensor);

    // 系统状态快照
    auto *systemSensor = new SystemSensor(&bus);
    sensors.append(systemSensor);
    tryStartSensor(systemSensor);

    // 浏览器活动监控
    auto *browserSensor = new BrowserSensor(&bus);
    sensors.append(browserSensor);
    tryStartSensor(browserSensor);

    awLogInfo() << "All sensors initialized, entering event loop";

    return app.exec();
}
