#include "../src/core/EventBus.h"
#include "../src/core/Config.h"
#include "../src/sensors/SystemSensor.h"
#include "../src/utils/Logger.h"

#include <QCoreApplication>
#include <QTest>

using namespace Awareness;

class EventCollector : public QObject
{
    Q_OBJECT
public:
    QList<Event> events;
    explicit EventCollector(EventBus *bus, QObject *parent = nullptr) : QObject(parent)
    {
        connect(bus, &EventBus::eventSignal, this, &EventCollector::collect);
    }
private slots:
    void collect(const Event &ev) { events.append(ev); }
};

class TestSystemSensor : public QObject
{
    Q_OBJECT
private slots:
    void testStartStop();
    void testStartIdempotent();
    void testSensorName();
    void testSnapshotEvent();
    void testCpuInRange();
    void testMemoryInRange();
    void testDiskInRange();
    void testEventMetadataKeys();
    void testReadUptime();
    void testReadLoadavg();

private:
    EventBus m_bus;
    EventCollector m_collector{&m_bus};
    SystemSensor m_sensor{&m_bus};
};

void TestSystemSensor::testStartStop()
{
    QVERIFY(!m_sensor.isRunning());
    QVERIFY(m_sensor.start());
    QVERIFY(m_sensor.isRunning());
    m_sensor.stop();
    QVERIFY(!m_sensor.isRunning());
}

void TestSystemSensor::testStartIdempotent()
{
    QVERIFY(m_sensor.start());
    QVERIFY(m_sensor.start());
    QVERIFY(m_sensor.isRunning());
    m_sensor.stop();
}

void TestSystemSensor::testSensorName()
{
    QCOMPARE(m_sensor.name(), QString("system"));
}

void TestSystemSensor::testSnapshotEvent()
{
    QVERIFY(m_sensor.start());
    m_collector.events.clear();

    // 等待首次采样（2秒延迟）+ 余量
    QTest::qWait(4000);

    bool found = false;
    for (const auto &ev : m_collector.events) {
        if (ev.type == EventType::System) {
            QCOMPARE(ev.action, QString("snapshot"));
            QVERIFY(!ev.contentPreview.isEmpty());
            QVERIFY(ev.contentPreview.contains("CPU:"));
            QVERIFY(ev.contentPreview.contains("MEM:"));
            found = true;
            break;
        }
    }
    if (!found) {
        QWARN("System snapshot event not received within timeout");
    }

    m_sensor.stop();
}

void TestSystemSensor::testCpuInRange()
{
    QVERIFY(m_sensor.start());
    QTest::qWait(4000);

    for (const auto &ev : m_collector.events) {
        if (ev.type == EventType::System) {
            double cpu = ev.metadata.value("cpu").toDouble();
            QVERIFY2(cpu >= 0.0 && cpu <= 100.0,
                     qPrintable(QString("CPU %1% out of [0,100]").arg(cpu, 0, 'f', 2)));
            break;
        }
    }
    m_sensor.stop();
}

void TestSystemSensor::testMemoryInRange()
{
    QVERIFY(m_sensor.start());
    QTest::qWait(4000);

    for (const auto &ev : m_collector.events) {
        if (ev.type == EventType::System) {
            double mem = ev.metadata.value("memory").toDouble();
            QVERIFY2(mem >= 0.0 && mem <= 100.0,
                     qPrintable(QString("MEM %1% out of [0,100]").arg(mem, 0, 'f', 2)));
            break;
        }
    }
    m_sensor.stop();
}

void TestSystemSensor::testDiskInRange()
{
    QVERIFY(m_sensor.start());
    QTest::qWait(4000);

    for (const auto &ev : m_collector.events) {
        if (ev.type == EventType::System) {
            double disk = ev.metadata.value("disk").toDouble();
            QVERIFY2(disk >= 0.0 && disk <= 100.0,
                     qPrintable(QString("DISK %1% out of [0,100]").arg(disk, 0, 'f', 2)));
            break;
        }
    }
    m_sensor.stop();
}

void TestSystemSensor::testEventMetadataKeys()
{
    QVERIFY(m_sensor.start());
    QTest::qWait(4000);

    for (const auto &ev : m_collector.events) {
        if (ev.type == EventType::System) {
            QStringList requiredKeys = {"cpu", "memory", "disk", "battery", "brightness", "network"};
            for (const auto &key : requiredKeys) {
                QVERIFY2(ev.metadata.contains(key),
                         qPrintable(QString("Missing metadata key: %1").arg(key)));
            }
            break;
        }
    }
    m_sensor.stop();
}

void TestSystemSensor::testReadUptime()
{
    QFile uptime("/proc/uptime");
    QVERIFY(uptime.open(QIODevice::ReadOnly | QIODevice::Text));
    QString line = uptime.readLine().trimmed();
    uptime.close();

    QStringList parts = line.split(' ');
    QVERIFY(parts.size() >= 1);
    double secs = parts[0].toDouble();
    QVERIFY(secs > 0); // 系统运行时间应大于 0
}

void TestSystemSensor::testReadLoadavg()
{
    QFile loadavg("/proc/loadavg");
    QVERIFY(loadavg.open(QIODevice::ReadOnly | QIODevice::Text));
    QString line = loadavg.readLine().trimmed();
    loadavg.close();

    QStringList parts = line.split(' ');
    QVERIFY(parts.size() >= 3);
    bool ok1, ok2, ok3;
    double m1 = parts[0].toDouble(&ok1);
    double m5 = parts[1].toDouble(&ok2);
    double m15 = parts[2].toDouble(&ok3);
    QVERIFY(ok1 && ok2 && ok3);
    QVERIFY(m1 >= 0.0);
    QVERIFY(m5 >= 0.0);
    QVERIFY(m15 >= 0.0);
}

QTEST_MAIN(TestSystemSensor)
#include "test_system_sensor.moc"
