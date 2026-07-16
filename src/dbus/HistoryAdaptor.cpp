#include "HistoryAdaptor.h"
#include "../utils/Logger.h"

#include <QSqlError>
#include <QStandardPaths>
#include <QFile>
#include <algorithm>

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

QVariantMap HistoryAdaptor::GetActivityDigest(qint64 since, qint64 until)
{
    return m_storage->activityDigest(since, until);
}

QList<QVariantMap> HistoryAdaptor::GetRecentFile(int limit)
{
    QVariantMap filter;
    filter.insert(QStringLiteral("type"), QStringLiteral("file"));
    return m_storage->queryActions(filter).mid(0, limit);
}

QList<QVariantMap> HistoryAdaptor::GetBrowserHistory(int limit, const QString &keyword)
{
    // 先查本地 browser_visits 表
    QList<QVariantMap> results = m_storage->queryBrowserHistory(limit, keyword);
    if (!results.isEmpty()) {
        return results;
    }

    // fallback: 直接读取浏览器 History 文件
    auto chromeResults = queryChromiumHistory(limit, keyword);
    if (!chromeResults.isEmpty()) {
        return chromeResults;
    }

    auto firefoxResults = queryFirefoxHistory(limit, keyword);
    if (!firefoxResults.isEmpty()) {
        return firefoxResults;
    }

    return {};
}

QList<QVariantMap> HistoryAdaptor::GetBrowserBookmarks(int limit, const QString &folder)
{
    return m_storage->queryBrowserBookmarks(limit, folder);
}


QString HistoryAdaptor::findBrowserHistoryPath(const QString &browserName)
{
    QString home = QDir::homePath();
    QStringList defaultProfiles = {"Default", "Profile 1", "Profile 2"};

    if (browserName == "google-chrome") {
        for (const auto &profile : defaultProfiles) {
            QString path = home + "/.config/google-chrome/" + profile + "/History";
            if (QFileInfo::exists(path)) return path;
        }
    } else if (browserName == "chromium") {
        for (const auto &profile : defaultProfiles) {
            QString path = home + "/.config/chromium/" + profile + "/History";
            if (QFileInfo::exists(path)) return path;
        }
    } else if (browserName == "microsoft-edge") {
        for (const auto &profile : defaultProfiles) {
            QString path = home + "/.config/microsoft-edge/" + profile + "/History";
            if (QFileInfo::exists(path)) return path;
        }
    } else if (browserName == "brave") {
        for (const auto &profile : defaultProfiles) {
            QString path = home + "/.config/BraveSoftware/Brave-Browser/" + profile + "/History";
            if (QFileInfo::exists(path)) return path;
        }
    } else if (browserName == "deepin-browser") {
        for (const auto &profile : defaultProfiles) {
            QString path = home + "/.config/browser/" + profile + "/History";
            if (QFileInfo::exists(path)) return path;
        }
    } else if (browserName == "firefox") {
        // Firefox: 查找 places.sqlite
        QString profileDir = home + "/.mozilla/firefox/";
        QDir dir(profileDir);
        QStringList profiles = dir.entryList(QDir::Dirs);
        for (const auto &p : profiles) {
            if (p.startsWith(".")) continue;
            QString path = profileDir + p + "/places.sqlite";
            if (QFileInfo::exists(path)) return path;
        }
    }
    return {};
}

QSqlDatabase HistoryAdaptor::openBrowserDb(const QString &path, const QString &prefix, QString &tmpPath)
{
    tmpPath.clear();
    QString connName = QStringLiteral("%1_%2").arg(prefix).arg(QDateTime::currentMSecsSinceEpoch());

    // 浏览器 History 文件运行时常被锁定（WAL 模式），始终使用副本
    tmpPath = QStringLiteral("/tmp/%1_%2.tmp").arg(prefix).arg(QDateTime::currentMSecsSinceEpoch());
    if (!QFile::copy(path, tmpPath)) {
        return QSqlDatabase();
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(tmpPath);
    if (!db.open()) {
        QFile::remove(tmpPath);
        tmpPath.clear();
        QSqlDatabase::removeDatabase(connName);
        return QSqlDatabase();
    }
    return db;
}

void HistoryAdaptor::closeBrowserDb(const QString &connName, const QString &tmpPath)
{
    QSqlDatabase db = QSqlDatabase::database(connName);
    if (db.isOpen()) {
        db.close();
    }
    QSqlDatabase::removeDatabase(connName);
    if (!tmpPath.isEmpty()) {
        QFile::remove(tmpPath);
    }
}

QList<QVariantMap> HistoryAdaptor::queryChromiumHistory(int limit, const QString &keyword)
{
    // 遍历所有 Chromium 内核浏览器
    QStringList chromiumBrowsers = {
        "google-chrome", "chromium", "microsoft-edge", "brave", "deepin-browser"
    };

    QList<QVariantMap> allResults;
    static constexpr qint64 WEBKIT_EPOCH_DELTA = 11644473600000000LL;

    for (const auto &browserName : chromiumBrowsers) {
        QString path = findBrowserHistoryPath(browserName);
        if (path.isEmpty()) continue;

        QString tmpPath;
        QSqlDatabase db = openBrowserDb(path, "dbus_chrome", tmpPath);
        if (!db.isOpen()) continue;

        {
            QSqlQuery q(db);
            QString sql;
            if (keyword.isEmpty()) {
                sql = QStringLiteral(
                    "SELECT urls.url, urls.title, urls.last_visit_time, urls.visit_count "
                    "FROM urls ORDER BY urls.last_visit_time DESC LIMIT %1").arg(limit);
            } else {
                QString escaped = keyword;
                escaped.replace("'", "''");
                sql = "SELECT urls.url, urls.title, urls.last_visit_time, urls.visit_count "
                      "FROM urls WHERE urls.title LIKE '%" + escaped + "%' "
                      "OR urls.url LIKE '%" + escaped + "%' "
                      "ORDER BY urls.last_visit_time DESC LIMIT " + QString::number(limit);
            }

            if (q.exec(sql)) {
                while (q.next()) {
                    QVariantMap item;
                    item.insert(QStringLiteral("url"), q.value(0).toString());
                    item.insert(QStringLiteral("title"), q.value(1).toString());
                    qint64 webkitTime = q.value(2).toLongLong();
                    qint64 timestamp = (webkitTime - WEBKIT_EPOCH_DELTA) / 1000;
                    item.insert(QStringLiteral("timestamp"), timestamp);
                    item.insert(QStringLiteral("visit_count"), q.value(3).toInt());
                    item.insert(QStringLiteral("browser"), browserName);
                    allResults.append(item);
                }
            } else {
                awLogWarning() << "HistoryAdaptor: Chrome history query failed:" << q.lastError().text();
            }
        }
        closeBrowserDb(db.connectionName(), tmpPath);
    }

    // 按时间降序排列，取 limit 条
    std::sort(allResults.begin(), allResults.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("timestamp").toLongLong() > b.value("timestamp").toLongLong();
    });
    if (allResults.size() > limit)
        allResults = allResults.mid(0, limit);

    return allResults;
}

QList<QVariantMap> HistoryAdaptor::queryFirefoxHistory(int limit, const QString &keyword)
{
    QString path = findBrowserHistoryPath("firefox");
    if (path.isEmpty()) return {};

    QString tmpPath;
    QSqlDatabase db = openBrowserDb(path, "dbus_firefox", tmpPath);
    if (!db.isOpen()) {
        awLogWarning() << "HistoryAdaptor: failed to open Firefox places.sqlite";
        return {};
    }

    QList<QVariantMap> results;
    {
        QSqlQuery q(db);
        QString sql;
        if (keyword.isEmpty()) {
            sql = QStringLiteral(
                "SELECT p.url, p.title, h.visit_date "
                "FROM moz_places p "
                "JOIN moz_historyvisits h ON p.id = h.place_id "
                "ORDER BY h.visit_date DESC LIMIT %1").arg(limit);
        } else {
            QString escaped = keyword;
            escaped.replace("'", "''");
            sql = "SELECT p.url, p.title, h.visit_date "
                  "FROM moz_places p "
                  "JOIN moz_historyvisits h ON p.id = h.place_id "
                  "WHERE p.title LIKE '%" + escaped + "%' "
                  "OR p.url LIKE '%" + escaped + "%' "
                  "ORDER BY h.visit_date DESC LIMIT " + QString::number(limit);
        }

        if (q.exec(sql)) {
            while (q.next()) {
                QVariantMap item;
                item.insert(QStringLiteral("url"), q.value(0).toString());
                item.insert(QStringLiteral("title"), q.value(1).toString());
                qint64 visitDate = q.value(2).toLongLong() / 1000;
                item.insert(QStringLiteral("timestamp"), visitDate);
                item.insert(QStringLiteral("browser"), QStringLiteral("firefox"));
                results.append(item);
            }
        } else {
            awLogWarning() << "HistoryAdaptor: Firefox history query failed:" << q.lastError().text();
        }
    }
    closeBrowserDb(db.connectionName(), tmpPath);
    return results;
}

} // namespace Awareness
