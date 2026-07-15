#pragma once

#include <QLoggingCategory>
#include <QString>

// 声明日志分类
Q_DECLARE_LOGGING_CATEGORY(lcAwareness)
Q_DECLARE_LOGGING_CATEGORY(lcStorage)
Q_DECLARE_LOGGING_CATEGORY(lcDBus)
Q_DECLARE_LOGGING_CATEGORY(lcSensor)

namespace Awareness {

// 日志初始化，设置日志格式和输出级别
void initLogging();

// 便捷日志宏
#define awLogDebug()    qCDebug(lcAwareness)
#define awLogInfo()     qCInfo(lcAwareness)
#define awLogWarning()  qCWarning(lcAwareness)
#define awLogCritical() qCCritical(lcAwareness)

} // namespace Awareness
