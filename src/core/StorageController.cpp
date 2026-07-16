#include "StorageController.h"
#include "../utils/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariantMap>

namespace Awareness {

static const char *kCreateActionsSql = R"(
CREATE TABLE IF NOT EXISTS actions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,
    type TEXT NOT NULL,
    action TEXT NOT NULL,
    app_name TEXT,
    app_pid INTEGER,
    window_title TEXT,
    file_path TEXT,
    content_preview TEXT,
    metadata TEXT
))";

static const char *kCreateSessionsSql = R"(
CREATE TABLE IF NOT EXISTS app_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    app_name TEXT NOT NULL,
    pid INTEGER,
    window_title TEXT,
    start_time INTEGER NOT NULL,
    end_time INTEGER,
    duration_ms INTEGER
))";

static const char *kCreateSnapshotsSql = R"(
CREATE TABLE IF NOT EXISTS system_snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,
    cpu_usage REAL,
    memory_usage REAL,
    disk_usage REAL,
    network_status TEXT,
    network_ssid TEXT,
    battery_level INTEGER,
    screen_brightness INTEGER,
    metadata TEXT
))";

static const char *kCreateBrowserVisitsSql = R"(
CREATE TABLE IF NOT EXISTS browser_visits (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,
    url TEXT NOT NULL,
    title TEXT,
    browser TEXT,
    tab_id TEXT,
    visit_type TEXT,
    referrer TEXT,
    visit_count INTEGER DEFAULT 1,
    metadata TEXT
))";

static const char *kCreateRedactionRulesSql = R"(
CREATE TABLE IF NOT EXISTS redaction_rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE NOT NULL,
    pattern TEXT NOT NULL,
    replacement TEXT NOT NULL,
    enabled INTEGER DEFAULT 1
))";

StorageController::StorageController(QObject *parent)
    : QObject(parent)
{
    m_connectionName = QStringLiteral("awareness_connection_") +
                       QString::number(reinterpret_cast<quintptr>(this));
}

StorageController::~StorageController()
{
    close();
}

bool StorageController::init(const QString &dbPath)
{
    QString path = dbPath;
    if (path.isEmpty()) {
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                                + QStringLiteral("/deepin/environment-awareness");
        QDir().mkpath(dataDir);
        path = dataDir + QStringLiteral("/awareness.db");
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);

    if (!m_db.open()) {
        awLogCritical() << "Cannot open database:" << m_db.lastError().text();
        return false;
    }

    // 启用 WAL 模式，提升并发读性能
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

    createTables();
    loadRedactionRules();

    awLogInfo() << "StorageController initialized:" << path;
    return true;
}

void StorageController::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

void StorageController::createTables()
{
    QStringList statements = {
        kCreateActionsSql, kCreateSessionsSql, kCreateSnapshotsSql,
        kCreateBrowserVisitsSql, kCreateRedactionRulesSql,
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_actions_type_time ON actions(type, timestamp)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_actions_app_time ON actions(app_name, timestamp)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_actions_timestamp ON actions(timestamp)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sessions_app ON app_sessions(app_name)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sessions_time ON app_sessions(start_time)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_snapshots_time ON system_snapshots(timestamp)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_bvisits_time ON browser_visits(timestamp)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_bvisits_url ON browser_visits(url)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_bvisits_title ON browser_visits(title)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_bvisits_browser ON browser_visits(browser)")
    };

    for (const auto &sql : statements) {
        QSqlQuery q(m_db);
        if (!q.exec(sql)) {
            awLogCritical() << "SQL error:" << q.lastError().text() << "SQL:" << sql;
        }
    }
}

void StorageController::loadRedactionRules()
{
    auto &redactor = Redactor::instance();
    redactor.initBuiltinRules();

    // 从数据库加载自定义规则
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT name, pattern, replacement, enabled FROM redaction_rules"))) {
        while (q.next()) {
            redactor.addRule(q.value(0).toString(),
                             q.value(1).toString(),
                             q.value(2).toString(),
                             q.value(3).toInt() != 0);
        }
    }
}

// --- 写入 ---

qint64 StorageController::insertAction(const Event &event)
{
    // 脱敏处理
    QString preview = event.contentPreview;
    if (Config::instance().redactionEnabled() && !preview.isEmpty()) {
        preview = Redactor::instance().redact(preview);
        preview = Redactor::truncate(preview, 200);
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO actions (timestamp, type, action, app_name, app_pid, "
        "window_title, file_path, content_preview, metadata) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    q.addBindValue(event.timestamp);
    q.addBindValue(eventTypeToString(event.type));
    q.addBindValue(event.action);
    q.addBindValue(event.appName);
    q.addBindValue(event.appPid);
    q.addBindValue(event.windowTitle);
    q.addBindValue(event.filePath);
    q.addBindValue(preview);
    q.addBindValue(event.metadata.isEmpty() ? QString()
                  : event.metadataToJson());

    if (!q.exec()) {
        awLogWarning() << "insertAction failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

qint64 StorageController::startSession(const QString &appName, qint64 pid, const QString &windowTitle)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO app_sessions (app_name, pid, window_title, start_time) VALUES (?, ?, ?, ?)"));
    q.addBindValue(appName);
    q.addBindValue(pid);
    q.addBindValue(windowTitle);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!q.exec()) {
        awLogWarning() << "startSession failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

void StorageController::endSession(qint64 sessionId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE app_sessions SET end_time = ?, duration_ms = ? - start_time WHERE id = ?"));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    q.addBindValue(now);
    q.addBindValue(now);
    q.addBindValue(sessionId);
    if (!q.exec()) {
        awLogWarning() << "endSession failed:" << q.lastError().text();
    }
}

qint64 StorageController::insertSnapshot(qint64 timestamp, double cpu, double mem, double disk,
                                         const QString &network, const QString &ssid,
                                         int battery, int brightness, const QVariantMap &metadata)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO system_snapshots (timestamp, cpu_usage, memory_usage, disk_usage, "
        "network_status, network_ssid, battery_level, screen_brightness, metadata) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(timestamp);
    q.addBindValue(cpu);
    q.addBindValue(mem);
    q.addBindValue(disk);
    q.addBindValue(network);
    q.addBindValue(ssid);
    q.addBindValue(battery);
    q.addBindValue(brightness);
    q.addBindValue(metadata.isEmpty() ? QString()
                  : QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(metadata)).toJson(QJsonDocument::Compact)));

    if (!q.exec()) {
        awLogWarning() << "insertSnapshot failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

qint64 StorageController::insertBrowserVisit(qint64 timestamp, const QString &url, const QString &title,
                                             const QString &browser, const QString &tabId,
                                             const QString &visitType, const QString &referrer,
                                             int visitCount, const QVariantMap &metadata)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO browser_visits (timestamp, url, title, browser, tab_id, "
        "visit_type, referrer, visit_count, metadata) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(timestamp);
    q.addBindValue(url);
    q.addBindValue(title);
    q.addBindValue(browser);
    q.addBindValue(tabId);
    q.addBindValue(visitType);
    q.addBindValue(referrer);
    q.addBindValue(visitCount);
    q.addBindValue(metadata.isEmpty() ? QString()
                  : QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(metadata)).toJson(QJsonDocument::Compact)));

    if (!q.exec()) {
        awLogWarning() << "insertBrowserVisit failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

// --- 查询 ---

static QVariantMap queryRowToMap(const QSqlQuery &q)
{
    QVariantMap map;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i) {
        map.insert(rec.fieldName(i), q.value(i));
    }
    return map;
}

QList<QVariantMap> StorageController::queryActions(const QVariantMap &filter)
{
    QStringList clauses;
    QVariantList binds;
    if (filter.contains(QStringLiteral("type"))) {
        clauses << QStringLiteral("type = ?");
        binds << filter.value(QStringLiteral("type"));
    }
    if (filter.contains(QStringLiteral("app"))) {
        clauses << QStringLiteral("app_name = ?");
        binds << filter.value(QStringLiteral("app"));
    }
    if (filter.contains(QStringLiteral("keyword"))) {
        clauses << QStringLiteral("(window_title LIKE ? OR file_path LIKE ? OR content_preview LIKE ? OR app_name LIKE ?)");
        QString kw = QStringLiteral("%") + filter.value(QStringLiteral("keyword")).toString() + QStringLiteral("%");
        binds << kw << kw << kw << kw;
    }
    if (filter.contains(QStringLiteral("since"))) {
        clauses << QStringLiteral("timestamp >= ?");
        binds << filter.value(QStringLiteral("since"));
    }
    if (filter.contains(QStringLiteral("until"))) {
        clauses << QStringLiteral("timestamp <= ?");
        binds << filter.value(QStringLiteral("until"));
    }

    QString sql = QStringLiteral("SELECT * FROM actions");
    if (!clauses.isEmpty())
        sql += QStringLiteral(" WHERE ") + clauses.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY timestamp DESC");

    if (filter.contains(QStringLiteral("limit"))) {
        sql += QStringLiteral(" LIMIT ?");
        binds << filter.value(QStringLiteral("limit"));
    }
    if (filter.contains(QStringLiteral("offset"))) {
        sql += QStringLiteral(" OFFSET ?");
        binds << filter.value(QStringLiteral("offset"));
    }

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto &v : binds)
        q.addBindValue(v);

    QList<QVariantMap> results;
    if (q.exec()) {
        while (q.next())
            results.append(queryRowToMap(q));
    } else {
        awLogWarning() << "queryActions failed:" << q.lastError().text();
    }
    return results;
}

QList<QVariantMap> StorageController::queryActionsByApp(const QString &app, int limit)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM actions WHERE app_name = ? ORDER BY timestamp DESC LIMIT ?"));
    q.addBindValue(app);
    q.addBindValue(limit);

    QList<QVariantMap> results;
    if (q.exec()) {
        while (q.next())
            results.append(queryRowToMap(q));
    }
    return results;
}

QList<QVariantMap> StorageController::recentActions(int limit)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM actions ORDER BY timestamp DESC LIMIT ?"));
    q.addBindValue(limit);

    QList<QVariantMap> results;
    if (q.exec()) {
        while (q.next())
            results.append(queryRowToMap(q));
    }
    return results;
}

QVariantMap StorageController::actionStats(const QVariantMap &filter)
{
    Q_UNUSED(filter)
    QVariantMap stats;
    QSqlQuery q(m_db);

    // 各类型操作次数
    if (q.exec(QStringLiteral("SELECT type, COUNT(*) FROM actions GROUP BY type"))) {
        QVariantMap byType;
        while (q.next())
            byType.insert(q.value(0).toString(), q.value(1));
        stats.insert(QStringLiteral("by_type"), byType);
    }

    // 最常用应用 Top 5
    if (q.exec(QStringLiteral("SELECT app_name, COUNT(*) as cnt FROM actions "
                              "GROUP BY app_name ORDER BY cnt DESC LIMIT 5"))) {
        QVariantList topApps;
        while (q.next()) {
            QVariantMap entry;
            entry.insert(QStringLiteral("app"), q.value(0));
            entry.insert(QStringLiteral("count"), q.value(1));
            topApps.append(entry);
        }
        stats.insert(QStringLiteral("top_apps"), topApps);
    }

    // 各应用活跃时长
    if (q.exec(QStringLiteral("SELECT app_name, SUM(duration_ms) FROM app_sessions "
                              "WHERE end_time IS NOT NULL GROUP BY app_name ORDER BY 2 DESC"))) {
        QVariantMap appDuration;
        while (q.next())
            appDuration.insert(q.value(0).toString(), q.value(1));
        stats.insert(QStringLiteral("app_duration_ms"), appDuration);
    }

    return stats;
}

QVariantMap StorageController::activityDigest(qint64 since, qint64 until)
{
    QVariantMap digest;
    digest[QStringLiteral("since")] = since;
    digest[QStringLiteral("until")] = until;

    QVariantList apps, files, urls;

    // apps: 从 actions 按 app_name 去重，取最早和最晚时间戳
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT app_name, MIN(timestamp) AS start_time, "
            "MAX(timestamp) AS end_time, COUNT(*) AS event_count "
            "FROM actions WHERE app_name IS NOT NULL AND app_name != '' "
            "AND timestamp >= ? AND timestamp <= ? "
            "GROUP BY app_name ORDER BY start_time ASC"));
        q.addBindValue(since);
        q.addBindValue(until);
        if (q.exec()) {
            while (q.next()) {
                QVariantMap app;
                app[QStringLiteral("name")] = q.value(0).toString();
                app[QStringLiteral("start_time")] = q.value(1).toLongLong();
                app[QStringLiteral("end_time")] = q.value(2).toLongLong();
                app[QStringLiteral("event_count")] = q.value(3).toInt();
                apps.append(app);
            }
        }
    }

    // files: type=file 去重（同路径只保留最新一条）
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT file_path, app_name, MAX(timestamp) AS ts "
            "FROM actions WHERE type='file' AND file_path IS NOT NULL "
            "AND file_path != '' AND timestamp >= ? AND timestamp <= ? "
            "GROUP BY file_path ORDER BY ts DESC"));
        q.addBindValue(since);
        q.addBindValue(until);
        if (q.exec()) {
            while (q.next()) {
                QVariantMap file;
                file[QStringLiteral("file_path")] = q.value(0).toString();
                file[QStringLiteral("app")] = q.value(1).toString();
                file[QStringLiteral("last_modified")] = q.value(2).toLongLong();
                files.append(file);
            }
        }
    }

    // urls: 浏览器活动在 actions 中以 window 类型记录，
    // 通过 app_name 匹配浏览器，window_title 包含页面标题
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT DISTINCT window_title, MAX(timestamp) AS ts, app_name "
            "FROM actions WHERE type='window' "
            "AND app_name IN ('chrome','firefox','deepin-browser') "
            "AND window_title IS NOT NULL AND window_title != '' "
            "AND timestamp >= ? AND timestamp <= ? "
            "GROUP BY window_title ORDER BY ts DESC"));
        q.addBindValue(since);
        q.addBindValue(until);
        if (q.exec()) {
            while (q.next()) {
                QVariantMap url;
                url[QStringLiteral("title")] = q.value(0).toString();
                url[QStringLiteral("timestamp")] = q.value(1).toLongLong();
                url[QStringLiteral("browser")] = q.value(2).toString();
                urls.append(url);
            }
        }
    }

    // clipboard_count: 只返回次数
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM actions WHERE type='clipboard' "
            "AND timestamp >= ? AND timestamp <= ?"));
        q.addBindValue(since);
        q.addBindValue(until);
        if (q.exec() && q.next())
            digest[QStringLiteral("clipboard_count")] = q.value(0).toInt();
        else
            digest[QStringLiteral("clipboard_count")] = 0;
    }

    digest[QStringLiteral("apps")] = apps;
    digest[QStringLiteral("files")] = files;
    digest[QStringLiteral("urls")] = urls;
    return digest;
}

QList<QVariantMap> StorageController::searchActions(const QString &keyword)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM actions WHERE "
                              "app_name LIKE ? OR window_title LIKE ? OR file_path LIKE ? OR content_preview LIKE ? "
                              "ORDER BY timestamp DESC LIMIT 100"));
    const QString like = QStringLiteral("%%1%").arg(keyword);
    q.addBindValue(like);
    q.addBindValue(like);
    q.addBindValue(like);
    q.addBindValue(like);

    QList<QVariantMap> results;
    if (q.exec()) {
        while (q.next())
            results.append(queryRowToMap(q));
    }
    return results;
}

QList<QVariantMap> StorageController::queryBrowserHistory(int limit, const QString &keyword)
{
    QSqlQuery q(m_db);
    if (keyword.isEmpty()) {
        q.prepare(QStringLiteral("SELECT * FROM browser_visits ORDER BY timestamp DESC LIMIT ?"));
        q.addBindValue(limit);
    } else {
        q.prepare(QStringLiteral("SELECT * FROM browser_visits WHERE title LIKE ? OR url LIKE ? "
                                  "ORDER BY timestamp DESC LIMIT ?"));
        const QString like = QStringLiteral("%%1%").arg(keyword);
        q.addBindValue(like);
        q.addBindValue(like);
        q.addBindValue(limit);
    }

    QList<QVariantMap> results;
    if (q.exec()) {
        while (q.next())
            results.append(queryRowToMap(q));
    }
    return results;
}

QList<QVariantMap> StorageController::queryBrowserBookmarks(int limit, const QString &folder)
{
    // Bookmarks 通常存储在文件中，此处从 browser_visits 表中近似查询
    // 完整实现需 BrowserSensor 提供书签数据
    Q_UNUSED(folder)
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT DISTINCT url, title, browser FROM browser_visits "
                              "ORDER BY timestamp DESC LIMIT ?"));
    q.addBindValue(limit);

    QList<QVariantMap> results;
    if (q.exec()) {
        while (q.next())
            results.append(queryRowToMap(q));
    }
    return results;
}

QList<QVariantMap> StorageController::searchBrowserHistory(const QString &keyword, int limit)
{
    return queryBrowserHistory(limit, keyword);
}

// --- 清理 ---

int StorageController::clearHistory(qint64 beforeTimestamp)
{
    int total = 0;
    QStringList tables = {QStringLiteral("actions"), QStringLiteral("app_sessions"),
                          QStringLiteral("system_snapshots"), QStringLiteral("browser_visits")};
    for (const auto &table : tables) {
        QSqlQuery q(m_db);
        // app_sessions 用 start_time，其余用 timestamp
        QString timeCol = (table == QStringLiteral("app_sessions"))
                          ? QStringLiteral("start_time") : QStringLiteral("timestamp");
        q.prepare(QStringLiteral("DELETE FROM %1 WHERE %2 < ?").arg(table, timeCol));
        q.addBindValue(beforeTimestamp);
        if (q.exec())
            total += q.numRowsAffected();
    }
    awLogInfo() << "Cleared" << total << "records older than" << beforeTimestamp;
    return total;
}

int StorageController::clearAll()
{
    int total = 0;
    QStringList tables = {QStringLiteral("actions"), QStringLiteral("app_sessions"),
                          QStringLiteral("system_snapshots"), QStringLiteral("browser_visits")};
    for (const auto &table : tables) {
        QSqlQuery q(m_db);
        if (q.exec(QStringLiteral("DELETE FROM %1").arg(table)))
            total += q.numRowsAffected();
    }
    awLogInfo() << "Cleared all" << total << "records";
    return total;
}

void StorageController::cleanup()
{
    const int retentionDays = Config::instance().retentionDays();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 cutoff = now - static_cast<qint64>(retentionDays) * 86400000LL;

    clearHistory(cutoff);
    downsampleSnapshots();

    awLogInfo() << "Cleanup completed (retention =" << retentionDays << "days)";
}

void StorageController::downsampleSnapshots()
{
    const auto &config = Config::instance();
    const QJsonObject sysConfig = config.sensorConfig(QStringLiteral("system"));
    const int afterDays = sysConfig.value(QStringLiteral("downsample_after_days")).toInt(3);
    const int intervalSec = sysConfig.value(QStringLiteral("downsample_interval_sec")).toInt(3600);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 downsampleAfter = now - static_cast<qint64>(afterDays) * 86400000LL;
    const qint64 retentionCutoff = now - static_cast<qint64>(config.retentionDays()) * 86400000LL;

    // 按小时聚合 3-7天前的数据
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT (timestamp / ?) as hour_bucket, "
        "AVG(cpu_usage), AVG(memory_usage), AVG(disk_usage), "
        "MAX(network_status), MAX(network_ssid), "
        "AVG(battery_level), AVG(screen_brightness) "
        "FROM system_snapshots "
        "WHERE timestamp < ? AND timestamp >= ? "
        "GROUP BY hour_bucket"));
    q.addBindValue(intervalSec * 1000);
    q.addBindValue(downsampleAfter);
    q.addBindValue(retentionCutoff);

    if (q.exec()) {
        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral(
            "INSERT INTO system_snapshots (timestamp, cpu_usage, memory_usage, disk_usage, "
            "network_status, network_ssid, battery_level, screen_brightness, metadata) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));

        while (q.next()) {
            const qint64 bucket = q.value(0).toLongLong() * intervalSec * 1000;
            insert.addBindValue(bucket);
            insert.addBindValue(q.value(1));
            insert.addBindValue(q.value(2));
            insert.addBindValue(q.value(3));
            insert.addBindValue(q.value(4));
            insert.addBindValue(q.value(5));
            insert.addBindValue(q.value(6));
            insert.addBindValue(q.value(7));
            insert.addBindValue(QStringLiteral("{\"downsampled\":true}"));
            insert.exec();
        }

        // 删除已聚合的原始记录
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral(
            "DELETE FROM system_snapshots WHERE timestamp < ? AND timestamp >= ?"));
        del.addBindValue(downsampleAfter);
        del.addBindValue(retentionCutoff);
        del.exec();

        awLogInfo() << "Downsampled system_snapshots older than" << afterDays << "days";
    }
}

} // namespace Awareness
