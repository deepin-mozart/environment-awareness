#include "DBusManager.h"
#include "../utils/Logger.h"
#include <QDBusConnection>
#include <QDBusError>
namespace Awareness {

DBusManager::DBusManager(StorageController *storage, EventBus *bus, QObject *parent)
    : QObject(parent)
    , m_storage(storage)
    , m_bus(bus)
{
    // 创建所有适配器
    m_context = new ContextAdaptor(m_storage, this);
    m_history = new HistoryAdaptor(m_storage, this);
    m_system = new SystemAdaptor(this);
    m_config = new ConfigAdaptor(m_storage, this);
    m_events = new EventsAdaptor(this);

    // EventBus 事件 → EventsAdaptor D-Bus 信号
    connect(m_bus, &EventBus::eventSignal, this, &DBusManager::onEventSignal);
}

DBusManager::~DBusManager() = default;

bool DBusManager::registerService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    // 注册 DBusManager 作为 D-Bus 对象，所有 Adaptor 自动暴露接口
    if (!bus.registerObject(QString::fromUtf8(kObjectPath), this)) {
        awLogCritical() << "Failed to register object:" << bus.lastError().message();
        return false;
    }

    // 注册服务名
    if (!bus.registerService(QString::fromUtf8(kServiceName))) {
        awLogCritical() << "Failed to register D-Bus service:" << bus.lastError().message();
        return false;
    }

    awLogInfo() << "D-Bus service registered:" << kServiceName << "at" << kObjectPath;
    return true;
}

void DBusManager::onEventSignal(const Event &event)
{
    m_events->emitEvent(event);
}

} // namespace Awareness
