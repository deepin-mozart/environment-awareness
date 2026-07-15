#pragma once

#include "../core/Config.h"
#include "../core/StorageController.h"

#include <QDBusAbstractAdaptor>
#include <QJsonObject>
#include <QVariantMap>

namespace Awareness {

/**
 * @brief D-Bus Config 接口适配器：配置管理
 */
class ConfigAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.EnvironmentAwareness.Config")
public:
    explicit ConfigAdaptor(StorageController *storage, QObject *parent = nullptr);

public slots:
    QVariantMap GetConfig();
    bool SetConfig(const QVariantMap &config);
    int ClearHistory(qint64 beforeTimestamp);
    int ClearAll();

private:
    StorageController *m_storage;
};

} // namespace Awareness
