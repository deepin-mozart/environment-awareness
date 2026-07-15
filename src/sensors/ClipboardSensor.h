#pragma once

#include "ISensor.h"
#include "../core/EventBus.h"

#include <QClipboard>
#include <QTimer>
#include <QPointer>

namespace Awareness {

/**
 * @brief 剪贴板监控 Sensor
 *
 * 通过 QApplication::clipboard() 监控剪贴板内容变化。
 * 仅记录文本和图片类型，二进制内容只记录元数据。
 * 防抖机制避免高频剪贴板事件。
 */
class ClipboardSensor : public ISensor
{
    Q_OBJECT
public:
    explicit ClipboardSensor(EventBus *bus, QObject *parent = nullptr);
    bool start() override;
    void stop() override;

    /// 获取当前剪贴板内容（供 D-Bus Context 接口查询）
    QVariantMap currentContent() const;

private slots:
    void onClipboardChanged();
    void checkClipboard();

private:
    void emitClipboardEvent(const QString &text);
    QString extractTextPreview(const QString &text, int maxLength = 200) const;

    EventBus *m_bus;
    QPointer<QClipboard> m_clipboard;
    QTimer *m_pollTimer = nullptr;

    QString m_lastText;
    qint64 m_lastChangeTime = 0;
    static constexpr int DEBOUNCE_MS = 300;
};

} // namespace Awareness
