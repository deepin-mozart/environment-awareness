#pragma once

#include "ISensor.h"
#include "../core/EventBus.h"

#include <QTimer>
#include <QSocketNotifier>

namespace Awareness {

/**
 * @brief 窗口/应用切换监控 Sensor
 *
 * 通过 DDE DWindowManager 或 X11 属性变化检测活动窗口切换。
 * 监控窗口的 open/close/switch/minimize/maximize 事件。
 */
class WindowSensor : public ISensor
{
    Q_OBJECT
public:
    explicit WindowSensor(EventBus *bus, QObject *parent = nullptr);

    bool start() override;
    void stop() override;

    /// 获取当前活动窗口信息（供 D-Bus Context 接口查询）
    QVariantMap activeWindowInfo() const;

private slots:
    void onActiveWindowChanged();

private:
    void setupX11Monitor();
    void cleanupX11Monitor();
    QString getCurrentActiveWindow();
    QString getAppNameFromPid(qint64 pid);

    EventBus *m_bus;
    QTimer *m_pollTimer = nullptr;

    // X11 监控
    void *m_x11Display = nullptr; // Display*
    int m_x11Connection = -1;
    QSocketNotifier *m_x11Notifier = nullptr;

    QString m_lastWindowTitle;
    QString m_lastAppName;
    qint64 m_lastWindowId = 0;
    qint64 m_lastPid = 0;
};

} // namespace Awareness
