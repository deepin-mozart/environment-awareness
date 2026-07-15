#include "ContextAdaptor.h"
#include "../utils/Logger.h"
namespace Awareness {

ContextAdaptor::ContextAdaptor(StorageController *storage, QObject *parent)
    : QDBusAbstractAdaptor(parent), m_storage(storage)
{
}

QVariantMap ContextAdaptor::GetActiveWindow()
{
    // 框架阶段返回空结构，WindowSensor 实现后填充
    QVariantMap map;
    map.insert(QStringLiteral("title"), QString());
    map.insert(QStringLiteral("app_name"), QString());
    map.insert(QStringLiteral("pid"), 0);
    map.insert(QStringLiteral("wm_class"), QString());
    map.insert(QStringLiteral("window_id"), 0);
    map.insert(QStringLiteral("is_minimized"), false);
    return map;
}

QList<QVariantMap> ContextAdaptor::GetRecentFiles(int limit)
{
    Q_UNUSED(limit)
    // 框架阶段返回空列表，FileSensor 实现后填充
    return {};
}

QVariantMap ContextAdaptor::GetClipboardContent()
{
    QVariantMap map;
    map.insert(QStringLiteral("type"), QStringLiteral("text"));
    map.insert(QStringLiteral("content"), QString());
    return map;
}

QList<QVariantMap> ContextAdaptor::GetRecentActions(int limit)
{
    return m_storage->recentActions(limit);
}

QList<QVariantMap> ContextAdaptor::GetBrowserTabs()
{
    // 框架阶段返回空列表，BrowserSensor 实现后填充
    return {};
}

QVariantMap ContextAdaptor::GetUserFocus()
{
    QVariantMap map;
    // 框架阶段返回空，各 Sensor 实现后聚合填充
    map.insert(QStringLiteral("active_app"), QString());
    map.insert(QStringLiteral("active_file"), QString());
    map.insert(QStringLiteral("input_state"), QStringLiteral("unknown"));
    return map;
}

} // namespace Awareness
