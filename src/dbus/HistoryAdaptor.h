#pragma once

#include "../core/StorageController.h"

#include <QDBusAbstractAdaptor>
#include <QVariantMap>

namespace Awareness {

/**
 * @brief D-Bus History 接口适配器：历史操作查询
 */
class HistoryAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.EnvironmentAwareness.History")
public:
    explicit HistoryAdaptor(StorageController *storage, QObject *parent = nullptr);

public slots:
    QList<QVariantMap> QueryActions(const QVariantMap &filter);
    QList<QVariantMap> QueryActionsByApp(const QString &app, int limit);
    QVariantMap GetActionStats(const QVariantMap &filter);
    QList<QVariantMap> GetTimeline(qint64 since, qint64 until);
    QList<QVariantMap> GetRecentFile(int limit);
    QList<QVariantMap> GetBrowserHistory(int limit, const QString &keyword);
    QList<QVariantMap> GetBrowserBookmarks(int limit, const QString &folder);
    QList<QVariantMap> SearchBrowserHistory(const QString &keyword, int limit);
    QList<QVariantMap> SearchActions(const QString &keyword);

private:
    StorageController *m_storage;
};

} // namespace Awareness
