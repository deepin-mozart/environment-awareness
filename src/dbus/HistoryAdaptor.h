#pragma once

#include "../core/StorageController.h"

#include <QDBusAbstractAdaptor>
#include <QVariantMap>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>

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
    QList<QVariantMap> queryChromiumHistory(int limit, const QString &keyword);
    QList<QVariantMap> queryFirefoxHistory(int limit, const QString &keyword);
    QString findBrowserHistoryPath(const QString &browserName);

    /// 尝试打开 SQLite（可能被浏览器锁定），失败时复制临时副本再打开
    QSqlDatabase openBrowserDb(const QString &path, const QString &prefix, QString &tmpPath);
    void closeBrowserDb(const QString &connName, const QString &tmpPath);

    StorageController *m_storage;
};

} // namespace Awareness
