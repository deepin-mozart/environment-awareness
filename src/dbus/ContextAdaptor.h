#pragma once

#include "../core/StorageController.h"

#include <QDBusAbstractAdaptor>
#include <QVariantMap>

namespace Awareness {

class StorageController;
class WindowSensor;
class ClipboardSensor;

/**
 * @brief D-Bus Context 接口适配器：实时获取当前上下文
 *
 * Methods:
 *   GetActiveWindow()      → a{sv}
 *   GetRecentFiles(i)       → aa{sv}
 *   GetClipboardContent()   → a{sv}
 *   GetRecentActions(i)     → aa{sv}
 *   GetBrowserTabs()        → aa{sv}
 *   GetUserFocus()           → a{sv}
 */
class ContextAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.EnvironmentAwareness.Context")
public:
    explicit ContextAdaptor(StorageController *storage, QObject *parent = nullptr);

    /// 设置 Sensor 引用（必须在 start() 之后调用）
    void setSensors(WindowSensor *window, ClipboardSensor *clipboard);

public slots:
    QVariantMap GetActiveWindow();
    QList<QVariantMap> GetRecentFiles(int limit);
    QVariantMap GetClipboardContent();
    QList<QVariantMap> GetRecentActions(int limit);
    QList<QVariantMap> GetBrowserTabs();
    QVariantMap GetUserFocus();

private:
    StorageController *m_storage;
    WindowSensor *m_windowSensor = nullptr;
    ClipboardSensor *m_clipboardSensor = nullptr;
};

} // namespace Awareness
