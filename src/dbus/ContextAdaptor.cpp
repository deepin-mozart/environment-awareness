#include "ContextAdaptor.h"
#include "HistoryAdaptor.h"
#include "../sensors/WindowSensor.h"
#include "../sensors/ClipboardSensor.h"
#include "../utils/Logger.h"
namespace Awareness {

ContextAdaptor::ContextAdaptor(StorageController *storage, QObject *parent)
    : QDBusAbstractAdaptor(parent), m_storage(storage)
{
}

void ContextAdaptor::setHistoryAdaptor(HistoryAdaptor *history)
{
    m_history = history;
}

 void ContextAdaptor::setSensors(WindowSensor *window, ClipboardSensor *clipboard)
{
    m_windowSensor = window;
    m_clipboardSensor = clipboard;
}

QVariantMap ContextAdaptor::GetActiveWindow()
{
    if (m_windowSensor) {
        return m_windowSensor->activeWindowInfo();
    }
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
    QVariantMap filter;
    filter.insert(QStringLiteral("type"), QStringLiteral("file"));
    return m_storage->queryActions(filter).mid(0, limit);
}

QVariantMap ContextAdaptor::GetClipboardContent()
{
    if (m_clipboardSensor) {
        return m_clipboardSensor->currentContent();
    }
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
    if (m_history) {
        return m_history->GetBrowserHistory(20, QString());
    }
    return {};
}

QVariantMap ContextAdaptor::GetUserFocus()
{
    QVariantMap map;
    if (m_windowSensor) {
        auto winInfo = m_windowSensor->activeWindowInfo();
        map.insert(QStringLiteral("active_app"), winInfo.value("app_name").toString());
        map.insert(QStringLiteral("active_file"), QString());
        map.insert(QStringLiteral("input_state"), QStringLiteral("unknown"));
    } else {
        map.insert(QStringLiteral("active_app"), QString());
        map.insert(QStringLiteral("active_file"), QString());
        map.insert(QStringLiteral("input_state"), QStringLiteral("unknown"));
    }
    return map;
}

} // namespace Awareness
