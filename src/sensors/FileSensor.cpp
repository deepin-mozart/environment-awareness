#include "FileSensor.h"
#include "../core/Config.h"
#include "../utils/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>

namespace Awareness {

FileSensor::FileSensor(EventBus *bus, QObject *parent)
    : ISensor(QStringLiteral("file"), parent), m_bus(bus)
{
}

bool FileSensor::start()
{
    if (m_running) return true;

    auto &cfg = Config::instance();
    if (!cfg.sensorEnabled(m_name)) {
        awLogWarning() << "FileSensor disabled by config";
        return false;
    }

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &FileSensor::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &FileSensor::onDirectoryChanged);

    // 定期重新扫描被删除后重建的文件
    m_rescanTimer = new QTimer(this);
    m_rescanTimer->setInterval(5000);
    connect(m_rescanTimer, &QTimer::timeout, this, &FileSensor::onRescanTimer);
    m_rescanTimer->start();

    setupDefaultWatches();

    m_running = true;
    emit statusChanged(QStringLiteral("running"));
    awLogInfo() << "FileSensor started, watching" << m_watchedDirs.size() << "directories";
    return true;
}

void FileSensor::stop()
{
    if (!m_running) return;

    if (m_rescanTimer) {
        m_rescanTimer->stop();
        m_rescanTimer->deleteLater();
        m_rescanTimer = nullptr;
    }

    if (m_watcher) {
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }

    m_watchedDirs.clear();
    m_debounce.clear();

    m_running = false;
    emit statusChanged(QStringLiteral("stopped"));
    awLogInfo() << "FileSensor stopped";
}

void FileSensor::setupDefaultWatches()
{
    auto &cfg = Config::instance();
    QJsonObject sensorCfg = cfg.sensorConfig(m_name);

    // 从配置读取监控路径，默认监控桌面和文档
    QStringList watchPaths;

    if (sensorCfg.contains(QStringLiteral("watch_paths"))) {
        QJsonArray paths = sensorCfg[QStringLiteral("watch_paths")].toArray();
        for (const auto &v : paths) {
            watchPaths.append(v.toString());
        }
    } else {
        // 默认监控路径
        QString home = QDir::homePath();
        watchPaths << home + QStringLiteral("/Desktop")
                   << home + QStringLiteral("/Documents")
                   << home + QStringLiteral("/Downloads");
    }

    for (const auto &path : watchPaths) {
        QDir dir(path);
        if (dir.exists()) {
            m_watcher->addPath(path);
            m_watchedDirs.insert(path);
        } else {
            awLogWarning() << "FileSensor: watch path does not exist:" << path;
        }
    }
}

void FileSensor::onFileChanged(const QString &path)
{
    auto now = QDateTime::currentMSecsSinceEpoch();
    auto it = m_debounce.find(path);
    if (it != m_debounce.end() && (now - it.value()) < DEBOUNCE_MS) {
        return;
    }
    m_debounce[path] = now;

    QFileInfo fi(path);
    QString action;

    if (!fi.exists()) {
        action = QStringLiteral("delete");
    } else if (fi.size() == 0) {
        // 新创建的空文件
        action = QStringLiteral("create");
    } else {
        action = QStringLiteral("modify");
    }

    emitFileEvent(path, action);
}

void FileSensor::onDirectoryChanged(const QString &path)
{
    auto now = QDateTime::currentMSecsSinceEpoch();
    auto it = m_debounce.find(path);
    if (it != m_debounce.end() && (now - it.value()) < DEBOUNCE_MS) {
        return;
    }
    m_debounce[path] = now;

    // 目录变化通常意味着新建/修改文件，检查最近几个文件
    QDir dir(path);
    QStringList entries = dir.entryList(QDir::Files, QDir::Time);
    for (int i = 0; i < qMin(entries.size(), 3); ++i) {
        QString newPath = dir.absoluteFilePath(entries[i]);
        QFileInfo fi(newPath);
        if (fi.lastModified().toMSecsSinceEpoch() > now - 5000) {
            auto fit = m_debounce.find(newPath);
            if (fit == m_debounce.end() || (now - fit.value()) > DEBOUNCE_MS) {
                m_debounce[newPath] = now;
                emitFileEvent(newPath, fi.size() == 0 ? QStringLiteral("create") : QStringLiteral("modify"));
            }
        }
    }
}

void FileSensor::onRescanTimer()
{
    // 重新扫描被删除的监控路径
    QStringList toRemove;
    for (const auto &dir : m_watchedDirs) {
        if (!QDir(dir).exists()) {
            toRemove.append(dir);
        }
    }
    for (const auto &dir : toRemove) {
        if (m_watcher) m_watcher->removePath(dir);
        m_watchedDirs.remove(dir);
    }

    // 备用扫描：检测最近修改的文件并产生 file 事件
    auto now = QDateTime::currentMSecsSinceEpoch();
    bool foundAny = false;
    for (const auto &dirPath : m_watchedDirs) {
        QDir dir(dirPath);
        // QDir::Time 默认从新到旧排序
        QStringList entries = dir.entryList(QDir::Files, QDir::Time);
        for (int i = 0; i < qMin(entries.size(), 10); ++i) {
            QString filePath = dir.absoluteFilePath(entries[i]);
            QFileInfo fi(filePath);
            qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
            // 最近 10 秒内修改过的文件
            if (mtime > now - 10000) {
                foundAny = true;
                auto it = m_debounce.find(filePath);
                if (it == m_debounce.end() || (now - it.value()) > DEBOUNCE_MS) {
                    m_debounce[filePath] = now;
                    awLogInfo() << "FileSensor: detected new file:" << filePath;
                    emitFileEvent(filePath, fi.size() == 0 ? QStringLiteral("create") : QStringLiteral("modify"));
                }
            }
        }
    }

    // 清理过期的防抖记录
    for (auto it = m_debounce.begin(); it != m_debounce.end(); ) {
        if (now - it.value() > 10000) {
            it = m_debounce.erase(it);
        } else {
            ++it;
        }
    }
}

void FileSensor::emitFileEvent(const QString &path, const QString &action)
{
    Event event(EventType::File, action);
    event.filePath = path;

    QFileInfo fi(path);
    event.appName = fi.suffix().isEmpty() ? path.mid(path.lastIndexOf('/') + 1) : fi.suffix();

    // content preview: 文件名 + 大小
    QString sizeStr;
    if (fi.exists()) {
        if (fi.size() < 1024) {
            sizeStr = QString("%1 B").arg(fi.size());
        } else if (fi.size() < 1024 * 1024) {
            sizeStr = QString("%1 KB").arg(fi.size() / 1024.0, 0, 'f', 1);
        } else {
            sizeStr = QString("%1 MB").arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1);
        }
    }
    event.contentPreview = fi.fileName() + " (" + sizeStr + ")";
    event.metadata.insert(QStringLiteral("file_size"), fi.size());
    event.metadata.insert(QStringLiteral("file_suffix"), fi.suffix());
    event.metadata.insert(QStringLiteral("absolute_path"), fi.absoluteFilePath());

    m_bus->publish(event);
    emit eventPublished(event);
}

} // namespace Awareness
