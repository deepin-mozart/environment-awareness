#include "ConfigAdaptor.h"
#include "../utils/Logger.h"
#include <QJsonDocument>

namespace Awareness {

ConfigAdaptor::ConfigAdaptor(StorageController *storage, QObject *parent)
    : QDBusAbstractAdaptor(parent), m_storage(storage)
{
}

QVariantMap ConfigAdaptor::GetConfig()
{
    const QJsonObject json = Config::instance().toJson();
    return json.toVariantMap();
}

bool ConfigAdaptor::SetConfig(const QVariantMap &config)
{
    const QJsonObject json = QJsonObject::fromVariantMap(config);
    return Config::instance().updateFromJson(json);
}

int ConfigAdaptor::ClearHistory(qint64 beforeTimestamp)
{
    return m_storage->clearHistory(beforeTimestamp);
}

int ConfigAdaptor::ClearAll()
{
    return m_storage->clearAll();
}

} // namespace Awareness
