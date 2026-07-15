#include "Config.h"
#include "../utils/Logger.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>

namespace Awareness {

Config &Config::instance()
{
    static Config inst;
    return inst;
}
void Config::setDefaults()
{
    // 构建各 sensor 配置
    QJsonObject windowCfg;
    windowCfg.insert(QStringLiteral("enabled"), true);
    windowCfg.insert(QStringLiteral("poll_interval_ms"), 2000);

    QJsonObject fileCfg;
    fileCfg.insert(QStringLiteral("enabled"), true);
    fileCfg.insert(QStringLiteral("watch_dirs"),
        QJsonArray() << QStringLiteral("~/Documents") << QStringLiteral("~/Desktop") << QStringLiteral("~/Downloads"));
    fileCfg.insert(QStringLiteral("max_watches"), 1024);
    fileCfg.insert(QStringLiteral("watch_depth"), 2);
    fileCfg.insert(QStringLiteral("ignore_dirs"),
        QJsonArray() << QStringLiteral(".git") << QStringLiteral("node_modules")
                     << QStringLiteral("__pycache__") << QStringLiteral(".venv")
                     << QStringLiteral("build") << QStringLiteral(".cache"));
    fileCfg.insert(QStringLiteral("file_types"),
        QJsonArray() << QStringLiteral("txt") << QStringLiteral("md") << QStringLiteral("cpp")
                     << QStringLiteral("py") << QStringLiteral("json") << QStringLiteral("xml"));

    QJsonObject clipboardCfg;
    clipboardCfg.insert(QStringLiteral("enabled"), true);
    clipboardCfg.insert(QStringLiteral("max_preview_len"), 200);

    QJsonObject inputCfg;
    inputCfg.insert(QStringLiteral("enabled"), true);
    inputCfg.insert(QStringLiteral("record_patterns"), true);
    inputCfg.insert(QStringLiteral("record_keys"), false);

    QJsonObject systemCfg;
    systemCfg.insert(QStringLiteral("enabled"), true);
    systemCfg.insert(QStringLiteral("alert_battery"), 20);
    systemCfg.insert(QStringLiteral("alert_disk"), 90);
    systemCfg.insert(QStringLiteral("alert_cpu"), 90);
    systemCfg.insert(QStringLiteral("downsample_after_days"), 3);
    systemCfg.insert(QStringLiteral("downsample_interval_sec"), 3600);

    QJsonObject browserCfg;
    browserCfg.insert(QStringLiteral("enabled"), true);
    browserCfg.insert(QStringLiteral("poll_interval_ms"), 30000);
    browserCfg.insert(QStringLiteral("supported_browsers"),
        QJsonArray() << QStringLiteral("deepin-browser") << QStringLiteral("chrome") << QStringLiteral("firefox"));
    browserCfg.insert(QStringLiteral("dedup_window_sec"), 300);
    browserCfg.insert(QStringLiteral("redact_url_params"), true);

    QJsonObject sensors;
    sensors.insert(QStringLiteral("window"), windowCfg);
    sensors.insert(QStringLiteral("file"), fileCfg);
    sensors.insert(QStringLiteral("clipboard"), clipboardCfg);
    sensors.insert(QStringLiteral("input"), inputCfg);
    sensors.insert(QStringLiteral("system"), systemCfg);
    sensors.insert(QStringLiteral("browser"), browserCfg);

    m_root = QJsonObject();
    m_root.insert(QStringLiteral("retention_days"), 7);
    m_root.insert(QStringLiteral("sampling_interval_sec"), 30);
    m_root.insert(QStringLiteral("redaction_enabled"), true);
    m_root.insert(QStringLiteral("sensors"), sensors);
}

bool Config::load(const QString &path)
{
    if (path.isEmpty()) {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                                  + QStringLiteral("/deepin/environment-awareness");
        QDir().mkpath(configDir);
        m_path = configDir + QStringLiteral("/config.json");
    } else {
        m_path = path;
    }

    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        m_root = doc.object();
        file.close();
        awLogInfo() << "Config loaded from" << m_path;
        return true;
    }

    // 文件不存在，写入默认值
    setDefaults();
    return save();
}

bool Config::save()
{
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        awLogWarning() << "Cannot write config to" << m_path;
        return false;
    }
    QJsonDocument doc(m_root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    awLogInfo() << "Config saved to" << m_path;
    return true;
}

QJsonObject Config::toJson() const
{
    return m_root;
}

bool Config::updateFromJson(const QJsonObject &json)
{
    // 合并：顶层 key 覆盖
    for (auto it = json.begin(); it != json.end(); ++it) {
        m_root.insert(it.key(), it.value());
    }
    save();
    emit configChanged();
    return true;
}

int Config::retentionDays() const
{
    return m_root.value(QStringLiteral("retention_days")).toInt(7);
}

int Config::samplingIntervalSec() const
{
    return m_root.value(QStringLiteral("sampling_interval_sec")).toInt(30);
}

bool Config::redactionEnabled() const
{
    return m_root.value(QStringLiteral("redaction_enabled")).toBool(true);
}

bool Config::sensorEnabled(const QString &name) const
{
    const QJsonObject sensors = m_root.value(QStringLiteral("sensors")).toObject();
    const QJsonObject sensor = sensors.value(name).toObject();
    return sensor.value(QStringLiteral("enabled")).toBool(true);
}

QJsonObject Config::sensorConfig(const QString &name) const
{
    const QJsonObject sensors = m_root.value(QStringLiteral("sensors")).toObject();
    return sensors.value(name).toObject();
}

void Config::setRetentionDays(int days)
{
    m_root.insert(QStringLiteral("retention_days"), days);
    save();
    emit configChanged();
}

void Config::setSensorEnabled(const QString &name, bool enabled)
{
    QJsonObject sensors = m_root.value(QStringLiteral("sensors")).toObject();
    QJsonObject sensor = sensors.value(name).toObject();
    sensor.insert(QStringLiteral("enabled"), enabled);
    sensors.insert(name, sensor);
    m_root.insert(QStringLiteral("sensors"), sensors);
    save();
    emit configChanged();
}

} // namespace Awareness
