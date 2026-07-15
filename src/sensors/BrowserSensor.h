#pragma once

#include "ISensor.h"
#include "../core/EventBus.h"

#include <QFileSystemWatcher>
#include <QTimer>
#include <QSet>

namespace Awareness {

/**
 * @brief 浏览器活动监控 Sensor
 *
 * 监控浏览器历史文件（~/.config/google-chrome/Default/History 等）
 * 和浏览器 D-Bus 接口的变化。
 * 产生 navigation/tab_changed/download 事件。
 */
class BrowserSensor : public ISensor
{
    Q_OBJECT
public:
    explicit BrowserSensor(EventBus *bus, QObject *parent = nullptr);

    bool start() override;
    void stop() override;

private slots:
    void onBrowserHistoryChanged(const QString &path);
    void onPollTimer();
    void onBrowserDBChanged(const QString &path);

private:
    struct BrowserProfile {
        QString name;       // 浏览器名称
        QString dbPath;    // History SQLite 文件路径
        qint64 lastModified = 0; // 上次文件修改时间
    };

    void discoverBrowsers();
    void checkHistoryChanges();
    void parseBrowserHistory(const BrowserProfile &profile);
    void emitBrowserEvent(const QString &action, const QVariantMap &info);

    EventBus *m_bus;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_pollTimer = nullptr;

    QList<BrowserProfile> m_browsers;

    static constexpr int POLL_INTERVAL_MS = 10000; // 10秒
    static constexpr int MAX_EVENTS_PER_POLL = 20;
};

} // namespace Awareness
