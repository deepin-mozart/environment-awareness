#include "HistoryAdaptor.h"
#include "../utils/Logger.h"

#include <QSqlError>
#include <QStandardPaths>

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
    auto chromeResults = queryChromeHistory(limit, keyword);
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

QList<QVariantMap> HistoryAdaptor::SearchBrowserHistory(const QString &keyword, int limit)
{
    return GetBrowserHistory(limit, keyword);
}

QList<QVariantMap> HistoryAdaptor::SearchActions(const QString &keyword)
{
    return m_storage->searchActions(keyword);
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

QList<QVariantMap> HistoryAdaptor::queryChromeHistory(int limit, const QString &keyword)
{
    QString path = findBrowserHistoryPath("google-chrome");
    if (path.isEmpty()) path = findBrowserHistoryPath("chromium");
    if (path.isEmpty()) path = findBrowserHistoryPath("microsoft-edge");
    if (path.isEmpty()) path = findBrowserHistoryPath("brave");
    if (path.isEmpty()) return {};

    QString connName = QStringLiteral("dbus_chrome_history_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setConnectOptions("QSQLITE_OPEN_READONLY");
    db.setDatabaseName(path);

    if (!db.open()) {
        awLogWarning() << "HistoryAdaptor: failed to open Chrome History:" << db.lastError().text();
        QSqlDatabase::removeDatabase(connName);
        return {};
    }

    QSqlQuery q(db);
    QString sql;
    if (keyword.isEmpty()) {
        q.prepare(QStringLiteral(
            "SELECT urls.url, urls.title, urls.last_visit_time, urls.visit_count "
            "FROM urls ORDER BY urls.last_visit_time DESC LIMIT ?"));
        q.addBindValue(limit);
    } else {
        q.prepare(QStringLiteral(
            "SELECT urls.url, urls.title, urls.last_visit_time, urls.visit_count "
            "FROM urls WHERE urls.title LIKE ? OR urls.url LIKE ? "
            "ORDER BY urls.last_visit_time DESC LIMIT ?"));
        QString like = "%" + keyword + "%";
        q.addBindValue(like);
        q.addBindValue(like);
        q.addBindValue(limit);
    }

    QList<QVariantMap> results;
    // Chromium 时间：WebKit epoch (1601-01-01 00:00:00 UTC)，单位微秒
    static constexpr qint64 WEBKIT_EPOCH_DELTA = 11644473600000000LL;

    if (q.exec()) {
        while (q.next()) {
            QVariantMap item;
            item.insert(QStringLiteral("url"), q.value(0).toString());
            item.insert(QStringLiteral("title"), q.value(1).toString());
            qint64 webkitTime = q.value(2).toLongLong();
            // 转 Unix 毫秒
            qint64 timestamp = (webkitTime - WEBKIT_EPOCH_DELTA) / 1000;
            item.insert(QStringLiteral("timestamp"), timestamp);
            item.insert(QStringLiteral("visit_count"), q.value(3).toInt());
            item.insert(QStringLiteral("browser"), QStringLiteral("chrome"));
            results.append(item);
        }
    } else {
        awLogWarning() << "HistoryAdaptor: Chrome history query failed:" << q.lastError().text();
    }

    db.close();
    QSqlDatabase::removeDatabase(connName);
    return results;
}

QList<QVariantMap> HistoryAdaptor::queryFirefoxHistory(int limit, const QString &keyword)
{
    QString path = findBrowserHistoryPath("firefox");
    if (path.isEmpty()) return {};

    QString connName = QStringLiteral("dbus_firefox_history_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setConnectOptions("QSQLITE_OPEN_READONLY");
    db.setDatabaseName(path);

    if (!db.open()) {
        awLogWarning() << "HistoryAdaptor: failed to open Firefox places.sqlite:" << db.lastError().text();
        QSqlDatabase::removeDatabase(connName);
        return {};
    }

    QSqlQuery q(db);
    QString sql;
    if (keyword.isEmpty()) {
        q.prepare(QStringLiteral(
            "SELECT p.url, p.title, h.visit_date "
            "FROM moz_places p "
            "JOIN moz_historyvisits h ON p.id = h.place_id "
            "ORDER BY h.visit_date DESC LIMIT ?"));
        q.addBindValue(limit);
    } else {
        q.prepare(QStringLiteral(
            "SELECT p.url, p.title, h.visit_date "
            "FROM moz_places p "
            "JOIN moz_historyvisits h ON p.id = h.place_id "
            "WHERE p.title LIKE ? OR p.url LIKE ? "
            "ORDER BY h.visit_date DESC LIMIT ?"));
        QString like = "%" + keyword + "%";
        q.addBindValue(like);
        q.addBindValue(like);
        q.addBindValue(limit);
    }

    QList<QVariantMap> results;

    if (q.exec()) {
        while (q.next()) {
            QVariantMap item;
            item.insert(QStringLiteral("url"), q.value(0).toString());
            item.insert(QStringLiteral("title"), q.value(1).toString());
            // Firefox visit_date 是微秒 Unix epoch
            qint64 visitDate = q.value(2).toLongLong() / 1000;
            item.insert(QStringLiteral("timestamp"), visitDate);
            item.insert(QStringLiteral("browser"), QStringLiteral("firefox"));
            results.append(item);
        }
    } else {
        awLogWarning() << "HistoryAdaptor: Firefox history query failed:" << q.lastError().text();
    }

    db.close();
    QSqlDatabase::removeDatabase(connName);
    return results;
}

} // namespace Awareness
