#include "BrowserSensor.h"
#include "../core/Config.h"
#include "../utils/Logger.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QStandardPaths>

namespace Awareness {

BrowserSensor::BrowserSensor(EventBus *bus, QObject *parent)
    : ISensor(QStringLiteral("browser"), parent), m_bus(bus)
{
}

bool BrowserSensor::start()
{
    if (m_running) return true;

    auto &cfg = Config::instance();
    if (!cfg.sensorEnabled(m_name)) {
        awLogWarning() << "BrowserSensor disabled by config";
        return false;
    }

    // 文件系统监控浏览器 History 文件
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &BrowserSensor::onBrowserDBChanged);

    // 定期轮询（文件系统监控可能丢失事件）
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &BrowserSensor::onPollTimer);
    m_pollTimer->start();

    discoverBrowsers();

    m_running = true;
    emit statusChanged(QStringLiteral("running"));
    awLogInfo() << "BrowserSensor started, found" << m_browsers.size() << "browser profiles";
    return true;
}

void BrowserSensor::stop()
{
    if (!m_running) return;

    if (m_pollTimer) {
        m_pollTimer->stop();
        m_pollTimer->deleteLater();
        m_pollTimer = nullptr;
    }

    if (m_watcher) {
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }

    m_browsers.clear();
    m_running = false;
    emit statusChanged(QStringLiteral("stopped"));
    awLogInfo() << "BrowserSensor stopped";
}

void BrowserSensor::discoverBrowsers()
{
    QString home = QDir::homePath();

    // Chromium-based 浏览器
    struct BrowserDef {
        QString name;
        QStringList profilePaths; // 带 %1 占位符
    };

    QList<BrowserDef> knownBrowsers = {
        {QStringLiteral("google-chrome"), {
            home + "/.config/google-chrome/%1/History",
            home + "/.config/google-chrome/%1/History-journal"
        }},
        {QStringLiteral("chromium"), {
            home + "/.config/chromium/%1/History",
            home + "/.config/chromium/%1/History-journal"
        }},
        {QStringLiteral("microsoft-edge"), {
            home + "/.config/microsoft-edge/%1/History",
            home + "/.config/microsoft-edge/%1/History-journal"
        }},
        {QStringLiteral("deepin-browser"), {
            home + "/.config/browser/%1/History",
            home + "/.config/browser/%1/History-journal"
        }},
        {QStringLiteral("brave"), {
            home + "/.config/BraveSoftware/Brave-Browser/%1/History",
            home + "/.config/BraveSoftware/Brave-Browser/%1/History-journal"
        }},
        {QStringLiteral("firefox"), {
            home + "/.mozilla/firefox/%1/places.sqlite",
            home + "/.mozilla/firefox/%1/places.sqlite-wal"
        }},
    };

    // 默认 profile 名
    QStringList defaultProfiles = {"Default", "Profile 1", "Profile 2", "Profile 3"};

    for (const auto &def : knownBrowsers) {
        for (const auto &profile : defaultProfiles) {
            QString histPath = def.profilePaths[0].arg(profile);
            QFileInfo fi(histPath);
            if (fi.exists()) {
                BrowserProfile bp;
                bp.name = def.name;
                bp.dbPath = histPath;
                bp.lastModified = fi.lastModified().toMSecsSinceEpoch();
                m_browsers.append(bp);

                // 监控文件变化
                m_watcher->addPath(histPath);
                if (def.profilePaths.size() > 1) {
                    QString journalPath = def.profilePaths[1].arg(profile);
                    if (QFileInfo::exists(journalPath)) {
                        m_watcher->addPath(journalPath);
                    }
                }

                awLogDebug() << "BrowserSensor: discovered" << bp.name << "profile" << profile
                             << "at" << bp.dbPath;
                break; // 只取第一个有效的 profile
            }
        }
    }
}

void BrowserSensor::onPollTimer()
{
    checkHistoryChanges();
}

void BrowserSensor::onBrowserDBChanged(const QString &path)
{
    Q_UNUSED(path);
    // 文件变化触发检查
    QTimer::singleShot(1000, this, &BrowserSensor::checkHistoryChanges);
}

void BrowserSensor::onBrowserHistoryChanged(const QString &path)
{
    Q_UNUSED(path);
    // History file changed via file system watcher - handled by onBrowserDBChanged
}

void BrowserSensor::checkHistoryChanges()
{
    for (auto &browser : m_browsers) {
        QFileInfo fi(browser.dbPath);
        if (!fi.exists()) continue;

        qint64 modified = fi.lastModified().toMSecsSinceEpoch();
        if (modified > browser.lastModified) {
            browser.lastModified = modified;
            parseBrowserHistory(browser);
        }
    }
}

void BrowserSensor::parseBrowserHistory(const BrowserProfile &profile)
{
    bool isFirefox = profile.name.contains("firefox");

    QString connectionName = QStringLiteral("browser_%1_%2").arg(profile.name).arg(
        QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setConnectOptions("QSQLITE_OPEN_READONLY");

    // Chromium History 文件可能被锁定，使用副本
    QString dbPath = profile.dbPath;

    // 对于 Firefox 的 places.sqlite
    if (isFirefox) {
        db.setDatabaseName(dbPath);
    } else {
        // Chromium 系列可能正在使用，尝试直接打开
        db.setDatabaseName(dbPath);
    }

    if (!db.open()) {
        awLogDebug() << "BrowserSensor: failed to open" << profile.name
                     << "database:" << db.lastError().text();
        QSqlDatabase::removeDatabase(connectionName);
        return;
    }

    int eventCount = 0;

    if (isFirefox) {
        // Firefox: moz_places + moz_historyvisits
        QSqlQuery query(db);
        query.exec(QStringLiteral(
            "SELECT p.url, p.title, h.visit_date, v.visit_type "
            "FROM moz_places p "
            "JOIN moz_historyvisits h ON p.id = h.place_id "
            "WHERE h.visit_date > 0 "
            "ORDER BY h.visit_date DESC LIMIT 20"
        ));

        while (query.next() && eventCount < MAX_EVENTS_PER_POLL) {
            QString url = query.value(0).toString();
            QString title = query.value(1).toString();
            qint64 visitDate = query.value(2).toLongLong();
            int visitType = query.value(3).toInt();

            QString action;
            if (visitType == 1) {
                action = QStringLiteral("navigation");
            } else if (visitType == 2) {
                action = QStringLiteral("tab_changed");
            } else {
                action = QStringLiteral("navigation");
            }

            QVariantMap info;
            info.insert(QStringLiteral("url"), url);
            info.insert(QStringLiteral("title"), title);
            info.insert(QStringLiteral("browser"), profile.name);
            info.insert(QStringLiteral("visit_date"), visitDate);

            emitBrowserEvent(action, info);
            eventCount++;
        }
    } else {
        // Chromium: urls 表
        QSqlQuery query(db);
        query.exec(QStringLiteral(
            "SELECT urls.url, urls.title, urls.visit_count, urls.last_visit_time, visits.visit_time "
            "FROM urls JOIN visits ON urls.id = visits.url "
            "ORDER BY visits.visit_time DESC LIMIT 20"
        ));

        while (query.next() && eventCount < MAX_EVENTS_PER_POLL) {
            QString url = query.value(0).toString();
            QString title = query.value(1).toString();
            int visitCount = query.value(2).toInt();
            // Chromium 时间是 WebKit epoch (1601-01-01)，需要转换
            qint64 webkitTime = query.value(4).toLongLong();

            QVariantMap info;
            info.insert(QStringLiteral("url"), url);
            info.insert(QStringLiteral("title"), title);
            info.insert(QStringLiteral("browser"), profile.name);
            info.insert(QStringLiteral("visit_count"), visitCount);
            info.insert(QStringLiteral("visit_time"), webkitTime);

            emitBrowserEvent(QStringLiteral("navigation"), info);
            eventCount++;
        }
    }

    db.close();
    QSqlDatabase::removeDatabase(connectionName);

    if (eventCount > 0) {
        awLogDebug() << "BrowserSensor:" << profile.name << "emitted" << eventCount << "events";
    }
}

void BrowserSensor::emitBrowserEvent(const QString &action, const QVariantMap &info)
{
    Event event(EventType::Browser, action);
    event.appName = info.value(QStringLiteral("browser")).toString();
    event.windowTitle = info.value(QStringLiteral("title")).toString();
    event.filePath = info.value(QStringLiteral("url")).toString();
    event.contentPreview = info.value(QStringLiteral("title")).toString().left(200);
    event.metadata = info;

    m_bus->publish(event);
    emit eventPublished(event);
}

} // namespace Awareness
