#pragma once

#include "ISensor.h"
#include "../core/EventBus.h"

#include <QTimer>

namespace Awareness {

/**
 * @brief 系统状态监控 Sensor
 *
 * 定时采集 CPU、内存、磁盘、网络、电池、亮度等系统状态，
 * 通过 StorageController::insertSnapshot 写入快照。
 * 采集频率可通过配置调整（默认 30 秒）。
 */
class SystemSensor : public ISensor
{
    Q_OBJECT
public:
    explicit SystemSensor(EventBus *bus, QObject *parent = nullptr);

    bool start() override;
    void stop() override;

private slots:
    void onSampleTimer();

private:
    // 采样函数
    double readCpuUsage();
    double readMemoryUsage();
    double readDiskUsage();
    QString readNetworkInfo();
    QString readSSID();
    int readBatteryLevel();
    int readBrightness();
    QVariantMap readMetadata();

    EventBus *m_bus;
    QTimer *m_sampleTimer = nullptr;

    int m_intervalSec = 30;

    // CPU 计算需要上一次的值
    qint64 m_prevIdleTime = 0;
    qint64 m_prevTotalTime = 0;
};

} // namespace Awareness
