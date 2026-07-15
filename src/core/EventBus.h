#pragma once

#include "Event.h"

#include <QObject>

namespace Awareness {

/**
 * @brief 事件总线，Sensor → EventBus → StorageController + D-Bus 信号
 *
 * 所有 Sensor 发出的事件先到 EventBus，再异步分发到 StorageController（持久化）
 * 和 D-BusManager（对外发信号）。用 Qt 信号槽实现，线程安全。
 */
class EventBus : public QObject
{
    Q_OBJECT
public:
    explicit EventBus(QObject *parent = nullptr);

    /// Sensor 调用：发布事件到总线
    void publish(const Event &event);

signals:
    /// StorageController 订阅：持久化
    void eventReceived(const Awareness::Event &event);

    /// D-BusManager 订阅：转发为 D-Bus 信号
    void eventSignal(const Awareness::Event &event);
};

} // namespace Awareness
