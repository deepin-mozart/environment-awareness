#pragma once

#include <QVariantMap>
#include <QString>
#include <QList>
#include <QDateTime>

namespace Awareness {

/**
 * @brief 事件类型枚举，对应 actions 表的 type 字段
 */
enum class EventType {
    Window,     ///< 窗口/应用切换
    File,       ///< 文件操作
    Clipboard,  ///< 剪贴板
    Input,      ///< 键鼠输入
    System,     ///< 系统状态
    Browser,    ///< 浏览器活动
};

/// EventType → 字符串
QString eventTypeToString(EventType type);

/// 字符串 → EventType（无效返回 Window）
EventType stringToEventType(const QString &str);

/**
 * @brief 事件数据结构，Sensor 产生后通过 EventBus 传递
 *
 * 一个 Event 对应 actions 表的一行记录。
 * Sensor 填充自己负责的字段，其余留空。
 */
struct Event {
    qint64 timestamp = 0;       ///< Unix 时间戳(毫秒)
    EventType type;             ///< 事件类型
    QString action;             ///< 具体动作(open/close/switch/modify/...)
    QString appName;            ///< 相关应用名
    qint64 appPid = 0;          ///< 相关进程 PID
    QString windowTitle;        ///< 窗口标题
    QString filePath;           ///< 文件路径(file类型)
    QString contentPreview;     ///< 脱敏后内容预览(最多200字符)
    QVariantMap metadata;       ///< 额外字段(JSON)
    /// metadata 转 JSON 字符串
    QString metadataToJson() const;


    /// 构造：自动填充 timestamp
    Event(EventType t, const QString &act)
        : timestamp(QDateTime::currentMSecsSinceEpoch())
        , type(t)
        , action(act)
    {}

    /// 默认构造（用于容器）
    Event() = default;

    /// 转为 QVariantMap，用于 D-Bus 传输和 metadata JSON
    QVariantMap toVariantMap() const;

    /// 从 QVariantMap 恢复
    static Event fromVariantMap(const QVariantMap &map);
};

} // namespace Awareness
