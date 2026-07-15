#pragma once

#include "ISensor.h"
#include "../core/EventBus.h"

#include <QFileSystemWatcher>
#include <QTimer>
#include <QSet>
#include <QHash>

namespace Awareness {

/**
 * @brief 文件操作监控 Sensor
 *
 * 通过 QFileSystemWatcher 监控指定目录/文件的变化，
 * 产生 file open/modify/delete/create 事件。
 * 支持通过配置指定监控路径。
 */
class FileSensor : public ISensor
{
    Q_OBJECT
public:
    explicit FileSensor(EventBus *bus, QObject *parent = nullptr);

    bool start() override;
    void stop() override;

private slots:
    void onFileChanged(const QString &path);
    void onDirectoryChanged(const QString &path);
    void onRescanTimer();

private:
    void setupDefaultWatches();
    void emitFileEvent(const QString &path, const QString &action);

    EventBus *m_bus;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_rescanTimer = nullptr;

    // 已监控目录集合
    QSet<QString> m_watchedDirs;
    // 最近处理过的路径，防抖
    QHash<QString, qint64> m_debounce;
    static constexpr int DEBOUNCE_MS = 500;
};

} // namespace Awareness
