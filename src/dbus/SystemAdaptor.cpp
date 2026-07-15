#include "SystemAdaptor.h"
#include "../utils/Logger.h"
#include <QFile>
#include <QStorageInfo>
#include <QTextStream>
#include <QRegularExpression>

// Qt 5.14+ 用 Qt::SkipEmptyParts, Qt 5.11 用 QString::SkipEmptyParts
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
static constexpr auto kSkipEmpty = QString::SkipEmptyParts;
#else
static constexpr auto kSkipEmpty = Qt::SkipEmptyParts;
#endif

namespace Awareness {

SystemAdaptor::SystemAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
}

static double parseCpuUsage()
{
    // /proc/stat 第一行: cpu  user nice system idle ...
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0.0;

    const QString line = QString::fromLatin1(file.readLine());
    file.close();

    const auto parts = line.split(QRegularExpression(QStringLiteral("\\s+")), kSkipEmpty);
    if (parts.size() < 5)
        return 0.0;

    bool ok = false;
    const qint64 user = parts[1].toLongLong(&ok);
    const qint64 nice = parts[2].toLongLong(&ok);
    const qint64 system = parts[3].toLongLong(&ok);
    const qint64 idle = parts[4].toLongLong(&ok);
    const qint64 total = user + nice + system + idle;
    if (total == 0)
        return 0.0;
    return (1.0 - static_cast<double>(idle) / total) * 100.0;
}

static double parseMemUsage()
{
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0.0;

    qint64 memTotal = 0, memAvailable = 0;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith(QStringLiteral("MemTotal:")))
            memTotal = line.split(QRegularExpression(QStringLiteral("\\s+")), kSkipEmpty)[1].toLongLong();
        else if (line.startsWith(QStringLiteral("MemAvailable:")))
            memAvailable = line.split(QRegularExpression(QStringLiteral("\\s+")), kSkipEmpty)[1].toLongLong();
    }
    file.close();

    if (memTotal == 0)
        return 0.0;
    return (1.0 - static_cast<double>(memAvailable) / memTotal) * 100.0;
}

QVariantMap SystemAdaptor::GetSystemStatus()
{
    QVariantMap map;
    map.insert(QStringLiteral("cpu_usage"), parseCpuUsage());
    map.insert(QStringLiteral("memory_usage"), parseMemUsage());

    const QStorageInfo root = QStorageInfo::root();
    map.insert(QStringLiteral("disk_usage"),
               root.isValid() ? 100.0 * (1.0 - static_cast<double>(root.bytesAvailable()) / root.bytesTotal()) : 0.0);

    // 电池和亮度框架阶段返回默认值，SystemSensor 实现后填充
    map.insert(QStringLiteral("battery_level"), -1);
    map.insert(QStringLiteral("screen_brightness"), -1);
    return map;
}

QVariantMap SystemAdaptor::GetNetworkInfo()
{
    QVariantMap map;
    // 框架阶段返回默认值，SystemSensor 实现后从 NetworkManager D-Bus 获取
    map.insert(QStringLiteral("status"), QStringLiteral("unknown"));
    map.insert(QStringLiteral("ssid"), QString());
    map.insert(QStringLiteral("ip"), QString());
    return map;
}

QVariantMap SystemAdaptor::GetStorageInfo()
{
    QVariantMap map;
    const QStorageInfo root = QStorageInfo::root();
    if (root.isValid()) {
        map.insert(QStringLiteral("total_bytes"), static_cast<qint64>(root.bytesTotal()));
        map.insert(QStringLiteral("available_bytes"), static_cast<qint64>(root.bytesAvailable()));
        map.insert(QStringLiteral("device"), root.device());
    }
    return map;
}

} // namespace Awareness
