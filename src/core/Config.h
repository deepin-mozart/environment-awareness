#pragma once

#include <QObject>
#include <QJsonObject>

namespace Awareness {

/**
 * @brief 配置管理，读写 config.json + 热加载
 *
 * 单例。配置变更时发出 configChanged 信号。
 */
class Config : public QObject
{
    Q_OBJECT
public:
    static Config &instance();

    /// 加载配置文件，失败则使用默认值并写入
    bool load(const QString &path = QString());

    /// 保存当前配置到文件
    bool save();

    /// 获取完整配置（D-Bus GetConfig 用）
    QJsonObject toJson() const;

    /// 从 JSON 更新配置（D-Bus SetConfig 用），热生效
    bool updateFromJson(const QJsonObject &json);

    // --- 常用配置项 getter ---
    int retentionDays() const;
    int samplingIntervalSec() const;
    bool redactionEnabled() const;

    // 各 Sensor 开关
    bool sensorEnabled(const QString &name) const;

    // Sensor 子配置
    QJsonObject sensorConfig(const QString &name) const;

    // --- setter（热修改）---
    void setRetentionDays(int days);
    void setSensorEnabled(const QString &name, bool enabled);

signals:
    void configChanged();

private:
    Config() = default;
    Q_DISABLE_COPY(Config)

    void setDefaults();

    QString m_path;
    QJsonObject m_root;
};

} // namespace Awareness
