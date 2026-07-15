#pragma once

#include <QDBusAbstractAdaptor>
#include <QVariantMap>

namespace Awareness {

/**
 * @brief D-Bus System 接口适配器：系统状态查询
 */
class SystemAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.EnvironmentAwareness.System")
public:
    explicit SystemAdaptor(QObject *parent = nullptr);

public slots:
    QVariantMap GetSystemStatus();
    QVariantMap GetNetworkInfo();
    QVariantMap GetStorageInfo();
};

} // namespace Awareness
