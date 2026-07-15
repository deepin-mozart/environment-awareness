#pragma once

#include "../core/EventBus.h"
#include "../core/StorageController.h"

#include "ContextAdaptor.h"
#include "HistoryAdaptor.h"
#include "SystemAdaptor.h"
#include "ConfigAdaptor.h"
#include "EventsAdaptor.h"


#include <QObject>

namespace Awareness {
class WindowSensor;
class ClipboardSensor;

/**
 * @brief D-Bus 服务管理器，注册所有接口和对象路径
 *
 * 负责：
 * - 在 session bus 上注册 org.deepin.EnvironmentAwareness 服务
 * - 挂载所有 Adaptor 到 /org/deepin/EnvironmentAwareness
 * - 将 EventBus 事件信号转发到 D-Bus Events 接口
 */
class DBusManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.EnvironmentAwareness")
public:
    explicit DBusManager(StorageController *storage, EventBus *bus, QObject *parent = nullptr);
    ~DBusManager();

    /// 注册 D-Bus 服务和所有接口，成功返回 true
    bool registerService();

    /// 设置 Sensor 引用到 ContextAdaptor（在 sensor start() 之后调用）
    void setContextSensors(WindowSensor *window, ClipboardSensor *clipboard);

private slots:
    void onEventSignal(const Event &event);

private:
    StorageController *m_storage;
    EventBus *m_bus;

    ContextAdaptor *m_context = nullptr;
    HistoryAdaptor *m_history = nullptr;
    SystemAdaptor *m_system = nullptr;
    ConfigAdaptor *m_config = nullptr;
    EventsAdaptor *m_events = nullptr;

    static constexpr const char *kServiceName = "org.deepin.EnvironmentAwareness";
    static constexpr const char *kObjectPath = "/org/deepin/EnvironmentAwareness";
};

} // namespace Awareness
