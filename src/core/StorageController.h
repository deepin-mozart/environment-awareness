#pragma once

#include "Event.h"
#include "Config.h"
#include "../utils/Redactor.h"

#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlRecord>
#include <QVariantList>

namespace Awareness {

/**
 * @brief SQLite 存储控制器，负责写入/查询/脱敏/清理/降采样
 *
 * 所有数据库操作在主线程执行（QSqlDatabase 不跨线程安全）。
 * EventBus 收到事件后在主线程调用 insertAction。
 */
class StorageController : public QObject
{
    Q_OBJECT
public:
    explicit StorageController(QObject *parent = nullptr);
    ~StorageController();

    /// 初始化数据库（建表、索引、内置脱敏规则）
    bool init(const QString &dbPath = QString());

    /// 关闭数据库
    void close();

    // --- 写入 ---
    /// 插入一条操作记录（自动脱敏 content_preview）
    qint64 insertAction(const Event &event);

    /// 开始一个 app_session
    qint64 startSession(const QString &appName, qint64 pid, const QString &windowTitle);

    /// 结束一个 app_session
    void endSession(qint64 sessionId);

    /// 插入系统状态快照
    qint64 insertSnapshot(qint64 timestamp, double cpu, double mem, double disk,
                          const QString &network, const QString &ssid,
                          int battery, int brightness, const QVariantMap &metadata);

    /// 插入浏览器访问记录
    qint64 insertBrowserVisit(qint64 timestamp, const QString &url, const QString &title,
                              const QString &browser, const QString &tabId,
                              const QString &visitType, const QString &referrer,
                              int visitCount, const QVariantMap &metadata);

    // --- 查询 ---
    /// 查询操作记录（通用过滤）
    QList<QVariantMap> queryActions(const QVariantMap &filter);

    /// 按应用查询
    QList<QVariantMap> queryActionsByApp(const QString &app, int limit);

    /// 获取最近 N 条操作（内存缓存快速返回）
    QList<QVariantMap> recentActions(int limit);

    /// 统计信息
    QVariantMap actionStats(const QVariantMap &filter);

    /// 去重视图（大时间范围的结构化事实聚合）
    QVariantMap activityDigest(qint64 since, qint64 until);

    /// 搜索操作记录
    QList<QVariantMap> searchActions(const QString &keyword);

    /// 浏览器历史查询
    QList<QVariantMap> queryBrowserHistory(int limit, const QString &keyword);

    /// 浏览器书签查询
    QList<QVariantMap> queryBrowserBookmarks(int limit, const QString &folder);

    /// 搜索浏览器历史
    QList<QVariantMap> searchBrowserHistory(const QString &keyword, int limit);

    // --- 清理 ---
    /// 清理指定时间戳之前的记录
    int clearHistory(qint64 beforeTimestamp);

    /// 清空所有记录
    int clearAll();

    /// 执行定期清理 + 降采样（由定时器调用）
    void cleanup();

private:
    void createTables();
    void loadRedactionRules();

    // 降采样：将 system_snapshots 中 3-7天前的数据聚合为每小时均值
    void downsampleSnapshots();

    QSqlDatabase m_db;
    QString m_connectionName;
};

} // namespace Awareness
