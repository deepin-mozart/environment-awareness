#include "Logger.h"

Q_LOGGING_CATEGORY(lcAwareness, "deepin.awareness")
Q_LOGGING_CATEGORY(lcStorage, "deepin.awareness.storage")
Q_LOGGING_CATEGORY(lcDBus, "deepin.awareness.dbus")
Q_LOGGING_CATEGORY(lcSensor, "deepin.awareness.sensor")

namespace Awareness {

void initLogging()
{
    // 设置日志格式: 时间戳 [分类] 级别 消息
    qSetMessagePattern("[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{category}] %{type}: %{message}");

    // 默认开启 Info 级别
    QLoggingCategory::setFilterRules(
        QStringLiteral("deepin.awareness.info=true\n"
                       "deepin.awareness.debug=false\n"
                       "deepin.awareness.storage.info=true\n"
                       "deepin.awareness.storage.debug=false\n"
                       "deepin.awareness.dbus.info=true\n"
                       "deepin.awareness.dbus.debug=false\n"
                       "deepin.awareness.sensor.info=true\n"
                       "deepin.awareness.sensor.debug=false"));
}

} // namespace Awareness
