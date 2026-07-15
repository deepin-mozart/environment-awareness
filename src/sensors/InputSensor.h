#pragma once

#include "ISensor.h"
#include "../core/EventBus.h"

#include <QTimer>

namespace Awareness {

/**
 * @brief 键鼠输入模式监控 Sensor
 *
 * 通过 XRecord 扩展或 XInput2 监控全局键鼠事件。
 * 聚合高频事件，输出输入模式（如"连续打字"、"鼠标拖拽"、"快捷键"）。
 * 定期（如 5 秒）输出一次聚合后的输入模式摘要，而非逐事件上报。
 */
class InputSensor : public ISensor
{
    Q_OBJECT
public:
    explicit InputSensor(EventBus *bus, QObject *parent = nullptr);

    bool start() override;
    void stop() override;

private slots:
    void onAggregateTimer();

private:
    bool setupXRecord();
    void cleanupXRecord();
    void handleKeyEvent();
    void handleMouseEvent();
    void emitInputPattern();

    EventBus *m_bus;
    QTimer *m_aggregateTimer = nullptr;

    // XRecord
    unsigned long m_xrecordContext = 0; // XRecordContext (XID)
    void *m_x11Display = nullptr; // Display*

    // 聚合统计
    int m_keyPressCount = 0;
    int m_mouseClickCount = 0;
    int m_mouseMoveCount = 0;
    int m_scrollCount = 0;
    qint64 m_lastKeyTimestamp = 0;
    qint64 m_lastMouseTimestamp = 0;
    bool m_hasModifiers = false; // Ctrl/Alt/Shift 组合键

    static constexpr int AGGREGATE_INTERVAL_MS = 5000; // 5秒
};

} // namespace Awareness
