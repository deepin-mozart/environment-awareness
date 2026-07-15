#include "SystemSensor.h"
#include "../core/Config.h"
#include "../utils/Logger.h"

#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>

namespace Awareness {

SystemSensor::SystemSensor(EventBus *bus, QObject *parent)
    : ISensor(QStringLiteral("system"), parent), m_bus(bus)
{
}

bool SystemSensor::start()
{
    if (m_running) return true;

    auto &cfg = Config::instance();
    if (!cfg.sensorEnabled(m_name)) {
        awLogWarning() << "SystemSensor disabled by config";
        return false;
    }

    m_intervalSec = cfg.sensorConfig(m_name).value(
        QStringLiteral("interval_sec")).toInt(30);

    m_sampleTimer = new QTimer(this);
    m_sampleTimer->setInterval(m_intervalSec * 1000);
    connect(m_sampleTimer, &QTimer::timeout, this, &SystemSensor::onSampleTimer);
    m_sampleTimer->start();

    // 首次采样
    QTimer::singleShot(2000, this, &SystemSensor::onSampleTimer);

    m_running = true;
    emit statusChanged(QStringLiteral("running"));
    awLogInfo() << "SystemSensor started, interval:" << m_intervalSec << "s";
    return true;
}

void SystemSensor::stop()
{
    if (!m_running) return;

    if (m_sampleTimer) {
        m_sampleTimer->stop();
        m_sampleTimer->deleteLater();
        m_sampleTimer = nullptr;
    }

    m_running = false;
    emit statusChanged(QStringLiteral("stopped"));
    awLogInfo() << "SystemSensor stopped";
}

void SystemSensor::onSampleTimer()
{
    auto now = QDateTime::currentMSecsSinceEpoch();

    double cpu = readCpuUsage();
    double mem = readMemoryUsage();
    double disk = readDiskUsage();
    QString network = readNetworkInfo();
    QString ssid = readSSID();
    int battery = readBatteryLevel();
    int brightness = readBrightness();
    QVariantMap metadata = readMetadata();

    // 通过 EventBus 发布系统状态事件
    Event event(EventType::System, QStringLiteral("snapshot"));
    event.contentPreview = QString("CPU:%1% MEM:%2% DISK:%3% BAT:%4%")
                            .arg(cpu, 0, 'f', 1)
                            .arg(mem, 0, 'f', 1)
                            .arg(disk, 0, 'f', 1)
                            .arg(battery);
    event.metadata.insert(QStringLiteral("cpu"), cpu);
    event.metadata.insert(QStringLiteral("memory"), mem);
    event.metadata.insert(QStringLiteral("disk"), disk);
    event.metadata.insert(QStringLiteral("network"), network);
    event.metadata.insert(QStringLiteral("ssid"), ssid);
    event.metadata.insert(QStringLiteral("battery"), battery);
    event.metadata.insert(QStringLiteral("brightness"), brightness);
    event.metadata = metadata; // 合并额外元数据（不覆盖上面设置的值）
    event.metadata.insert(QStringLiteral("cpu"), cpu);
    event.metadata.insert(QStringLiteral("memory"), mem);
    event.metadata.insert(QStringLiteral("disk"), disk);
    event.metadata.insert(QStringLiteral("network"), network);
    event.metadata.insert(QStringLiteral("ssid"), ssid);
    event.metadata.insert(QStringLiteral("battery"), battery);
    event.metadata.insert(QStringLiteral("brightness"), brightness);

    m_bus->publish(event);
    emit eventPublished(event);
}

double SystemSensor::readCpuUsage()
{
    QFile stat(QStringLiteral("/proc/stat"));
    if (!stat.open(QIODevice::ReadOnly | QIODevice::Text)) return 0.0;

    QTextStream in(&stat);
    QString line = in.readLine();
    stat.close();

    if (!line.startsWith(QStringLiteral("cpu "))) return 0.0;

    QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), QString::SkipEmptyParts);
    if (parts.size() < 5) return 0.0;

    // user nice system idle iowait irq softirq steal guest guest_nice
    qint64 user = parts[1].toLongLong();
    qint64 nice = parts[2].toLongLong();
    qint64 system = parts[3].toLongLong();
    qint64 idle = parts[4].toLongLong();
    qint64 iowait = (parts.size() > 5) ? parts[5].toLongLong() : 0;

    qint64 idleTime = idle + iowait;
    qint64 totalTime = user + nice + system + idleTime;

    if (m_prevTotalTime > 0) {
        qint64 dIdle = idleTime - m_prevIdleTime;
        qint64 dTotal = totalTime - m_prevTotalTime;
        if (dTotal > 0) {
            double usage = 100.0 * (1.0 - static_cast<double>(dIdle) / dTotal);
            m_prevIdleTime = idleTime;
            m_prevTotalTime = totalTime;
            return qBound(0.0, usage, 100.0);
        }
    }

    m_prevIdleTime = idleTime;
    m_prevTotalTime = totalTime;
    return 0.0;
}

double SystemSensor::readMemoryUsage()
{
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (!meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) return 0.0;

    qint64 total = 0, available = 0;
    QTextStream in(&meminfo);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith(QStringLiteral("MemTotal:"))) {
            total = line.simplified().split(' ')[1].toLongLong();
        } else if (line.startsWith(QStringLiteral("MemAvailable:"))) {
            available = line.simplified().split(' ')[1].toLongLong();
            break;
        }
    }
    meminfo.close();

    if (total <= 0) return 0.0;
    return 100.0 * (1.0 - static_cast<double>(available) / total);
}

double SystemSensor::readDiskUsage()
{
    // 读取根分区使用率
    QProcess df;
    df.start(QStringLiteral("df"), {QStringLiteral("--output=pct"), QStringLiteral("/")});
    if (!df.waitForFinished(2000)) return 0.0;

    QString output = QString::fromUtf8(df.readAllStandardOutput()).trimmed();
    // 第一行是标题 "Use%", 后面是数据
    QStringList lines = output.split('\n');
    if (lines.size() >= 2) {
        QString pct = lines[1].trimmed();
        pct.remove('%');
        bool ok = false;
        double val = pct.toDouble(&ok);
        if (ok) return val;
    }

    return 0.0;
}

QString SystemSensor::readNetworkInfo()
{
    // 读取网络连接状态
    QProcess nm;
    nm.start(QStringLiteral("nmcli"), {QStringLiteral("-t"), QStringLiteral("-f"),
                QStringLiteral("TYPE,STATE,CON-NAME"), QStringLiteral("general")});
    if (!nm.waitForFinished(2000)) return QStringLiteral("unknown");

    QString output = QString::fromUtf8(nm.readAllStandardOutput()).trimmed();
    return output.isEmpty() ? QStringLiteral("disconnected") : output;
}

QString SystemSensor::readSSID()
{
    QProcess nm;
    QStringList args = {QStringLiteral("-t"), QStringLiteral("-f"),
                QStringLiteral("active,ssid"), QStringLiteral("dev"), QStringLiteral("wifi")};
    nm.start(QStringLiteral("nmcli"), args);
    if (!nm.waitForFinished(2000)) return {};

    QString output = QString::fromUtf8(nm.readAllStandardOutput()).trimmed();
    // 格式: yes:MyWiFi 或 no:
    QStringList lines = output.split('\n');
    for (const auto &line : lines) {
        QStringList parts = line.split(':');
        if (parts.size() >= 2 && parts[0] == QStringLiteral("yes") && !parts[1].isEmpty()) {
            return parts[1];
        }
    }
    return {};
}

int SystemSensor::readBatteryLevel()
{
    // 通过 upower 读取电池
    QProcess up;
    up.start(QStringLiteral("upower"), {QStringLiteral("-i"),
                QStringLiteral("/org/freedesktop/UPower/devices/battery_BAT0")});
    if (!up.waitForFinished(2000)) return -1;

    QString output = QString::fromUtf8(up.readAllStandardOutput());
    QRegularExpression re(QStringLiteral("percentage:\\s*(\\d+)%"));
    auto match = re.match(output);
    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }
    return -1;
}

int SystemSensor::readBrightness()
{
    // 读取屏幕亮度（deepin 使用 D-Bus）
    QProcess ddc;
    ddc.start(QStringLiteral("dbus-send"), {
        QStringLiteral("--print-reply"),
        QStringLiteral("--dest=com.deepin.daemon.Display"),
        QStringLiteral("/com/deepin/daemon/Display"),
        QStringLiteral("org.freedesktop.DBus.Properties.Get"),
        QStringLiteral("string:com.deepin.daemon.Display"),
        QStringLiteral("string:Brightness")
    });
    if (!ddc.waitForFinished(2000)) return -1;

    QString output = QString::fromUtf8(ddc.readAllStandardOutput());
    QRegularExpression re(QStringLiteral("variant\\s+double\\s+(\\d+)"));
    auto match = re.match(output);
    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }
    return -1;
}

QVariantMap SystemSensor::readMetadata()
{
    QVariantMap meta;

    // 运行时间
    QFile uptime(QStringLiteral("/proc/uptime"));
    if (uptime.open(QIODevice::ReadOnly | QIODevice::Text)) {
        double secs = uptime.readLine().trimmed().split(' ')[0].toDouble();
        meta.insert(QStringLiteral("uptime_seconds"), static_cast<qint64>(secs));
        uptime.close();
    }

    // 负载
    QFile loadavg(QStringLiteral("/proc/loadavg"));
    if (loadavg.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString line = loadavg.readLine().trimmed();
        QStringList parts = line.split(' ');
        if (parts.size() >= 3) {
            meta.insert(QStringLiteral("load_1m"), parts[0].toDouble());
            meta.insert(QStringLiteral("load_5m"), parts[1].toDouble());
            meta.insert(QStringLiteral("load_15m"), parts[2].toDouble());
        }
        loadavg.close();
    }

    return meta;
}

} // namespace Awareness
