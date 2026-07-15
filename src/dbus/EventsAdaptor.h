#pragma once

#include "../core/Event.h"

#include <QDBusAbstractAdaptor>
#include <QVariantMap>

namespace Awareness {

/**
 * @brief D-Bus Events 接口适配器：事件信号（主动通知）
 *
 * 框架阶段定义所有信号，实际由 DBusManager 在收到 EventBus 事件时转发。
 */
class EventsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.EnvironmentAwareness.Events")
public:
    explicit EventsAdaptor(QObject *parent = nullptr);

    /// 根据事件类型发出对应 D-Bus 信号
    void emitEvent(const Event &event);

signals:
    void WindowChanged(const QVariantMap &window_info);
    void FileOpened(const QVariantMap &file_info);
    void FileModified(const QVariantMap &file_info);
    void ClipboardChanged(const QVariantMap &clipboard_info);
    void InputPattern(const QVariantMap &pattern_info);
    void SystemAlert(const QVariantMap &alert_info);
    void BrowserTabChanged(const QVariantMap &tab_info);
    void BrowserNavigation(const QVariantMap &nav_info);
    void BrowserDownload(const QVariantMap &download_info);
};

} // namespace Awareness
