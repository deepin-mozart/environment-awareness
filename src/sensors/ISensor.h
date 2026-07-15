#pragma once

#include "../core/Event.h"

#include <QObject>
#include <QString>

namespace Awareness {

/**
 * @brief Sensor 接口，所有 Sensor 实现此接口
 *
 * Sensor 在独立线程中运行，通过 EventBus 发布事件。
 * 框架阶段只定义接口，具体实现后续补充。
 */
class ISensor : public QObject
{
    Q_OBJECT
public:
    explicit ISensor(const QString &name, QObject *parent = nullptr)
        : QObject(parent), m_name(name) {}

    virtual ~ISensor() = default;

    /// Sensor 名称（如 "window", "file", "clipboard"）
    const QString &name() const { return m_name; }

    /// 初始化 Sensor（打开数据源、订阅信号等），成功返回 true
    virtual bool start() = 0;

    /// 停止 Sensor（清理资源）
    virtual void stop() = 0;

    /// 当前是否在运行
    bool isRunning() const { return m_running; }

signals:
    /// Sensor 发布事件
    void eventPublished(const Awareness::Event &event);

    /// Sensor 状态变化
    void statusChanged(const QString &status);

protected:
    QString m_name;
    bool m_running = false;
};

} // namespace Awareness
