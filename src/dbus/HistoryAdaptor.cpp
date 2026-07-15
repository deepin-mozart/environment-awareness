#include "HistoryAdaptor.h"
#include "../utils/Logger.h"
namespace Awareness {

HistoryAdaptor::HistoryAdaptor(StorageController *storage, QObject *parent)
    : QDBusAbstractAdaptor(parent), m_storage(storage)
{
}

QList<QVariantMap> HistoryAdaptor::QueryActions(const QVariantMap &filter)
{
    return m_storage->queryActions(filter);
}

QList<QVariantMap> HistoryAdaptor::QueryActionsByApp(const QString &app, int limit)
{
    return m_storage->queryActionsByApp(app, limit);
}

QVariantMap HistoryAdaptor::GetActionStats(const QVariantMap &filter)
{
    return m_storage->actionStats(filter);
}

QList<QVariantMap> HistoryAdaptor::GetTimeline(qint64 since, qint64 until)
{
    return m_storage->timeline(since, until);
}

QList<QVariantMap> HistoryAdaptor::GetRecentFile(int limit)
{
    Q_UNUSED(limit)
    // 框架阶段返回空列表，FileSensor 实现后实时获取
    return {};
}

QList<QVariantMap> HistoryAdaptor::GetBrowserHistory(int limit, const QString &keyword)
{
    return m_storage->queryBrowserHistory(limit, keyword);
}

QList<QVariantMap> HistoryAdaptor::GetBrowserBookmarks(int limit, const QString &folder)
{
    return m_storage->queryBrowserBookmarks(limit, folder);
}

QList<QVariantMap> HistoryAdaptor::SearchBrowserHistory(const QString &keyword, int limit)
{
    return m_storage->searchBrowserHistory(keyword, limit);
}

QList<QVariantMap> HistoryAdaptor::SearchActions(const QString &keyword)
{
    return m_storage->searchActions(keyword);
}

} // namespace Awareness
