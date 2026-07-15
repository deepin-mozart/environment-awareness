#include "ClipboardSensor.h"
#include "../core/Config.h"
#include "../utils/Logger.h"

#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QMimeData>
#include <QUrl>

namespace Awareness {

ClipboardSensor::ClipboardSensor(EventBus *bus, QObject *parent)
    : ISensor(QStringLiteral("clipboard"), parent), m_bus(bus)
{
}

bool ClipboardSensor::start()
{
    if (m_running) return true;

    auto &cfg = Config::instance();
    if (!cfg.sensorEnabled(m_name)) {
        awLogWarning() << "ClipboardSensor disabled by config";
        return false;
    }

    m_clipboard = QApplication::clipboard();
    if (!m_clipboard) {
        awLogWarning() << "ClipboardSensor: no clipboard available (headless?)";
        return false;
    }

    // 连接剪贴板变化信号
    connect(m_clipboard, &QClipboard::dataChanged, this, &ClipboardSensor::onClipboardChanged);

    // 备用轮询（部分环境下 dataChanged 信号不可靠）
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &ClipboardSensor::checkClipboard);
    m_pollTimer->start();

    // 记录初始内容
    m_lastText = m_clipboard->text();
    m_lastChangeTime = 0;

    m_running = true;
    emit statusChanged(QStringLiteral("running"));
    awLogInfo() << "ClipboardSensor started";
    return true;
}

void ClipboardSensor::stop()
{
    if (!m_running) return;

    if (m_pollTimer) {
        m_pollTimer->stop();
        m_pollTimer->deleteLater();
        m_pollTimer = nullptr;
    }

    disconnect(m_clipboard, &QClipboard::dataChanged, this, &ClipboardSensor::onClipboardChanged);

    m_clipboard.clear();
    m_running = false;
    emit statusChanged(QStringLiteral("stopped"));
    awLogInfo() << "ClipboardSensor stopped";
}

void ClipboardSensor::onClipboardChanged()
{
    auto now = QDateTime::currentMSecsSinceEpoch();
    if ((now - m_lastChangeTime) < DEBOUNCE_MS) {
        return;
    }

    if (!m_clipboard) return;

    const QMimeData *mime = m_clipboard->mimeData();
    if (!mime) return;

    // 优先处理文本
    if (mime->hasText()) {
        QString text = m_clipboard->text();
        if (text != m_lastText && !text.isEmpty()) {
            m_lastChangeTime = now;
            m_lastText = text;
            emitClipboardEvent(text);
        }
    }
    // 图片：只记录元数据
    else if (mime->hasImage()) {
        m_lastChangeTime = now;
        m_lastText.clear();

        Event event(EventType::Clipboard, QStringLiteral("copy"));
        event.contentPreview = QStringLiteral("[image]");
        event.metadata.insert(QStringLiteral("mime_type"), QStringLiteral("image"));
        event.metadata.insert(QStringLiteral("has_image"), true);

        m_bus->publish(event);
        emit eventPublished(event);
    }
    // 其他格式
    else if (mime->hasUrls()) {
        m_lastChangeTime = now;
        m_lastText.clear();

        QList<QUrl> urls = mime->urls();
        QStringList urlStrings;
        for (const auto &url : urls) {
            urlStrings << url.toString();
        }

        Event event(EventType::Clipboard, QStringLiteral("copy"));
        event.contentPreview = extractTextPreview(urlStrings.join("; "));
        event.metadata.insert(QStringLiteral("mime_type"), QStringLiteral("text/uri-list"));
        event.metadata.insert(QStringLiteral("url_count"), urls.size());

        m_bus->publish(event);
        emit eventPublished(event);
    }
}

void ClipboardSensor::checkClipboard()
{
    // 备用检查：轮询模式下对比当前与上次
    if (!m_clipboard) return;

    QString text = m_clipboard->text();
    if (text != m_lastText && !text.isEmpty()) {
        auto now = QDateTime::currentMSecsSinceEpoch();
        if ((now - m_lastChangeTime) >= DEBOUNCE_MS) {
            m_lastText = text;
            m_lastChangeTime = now;
            emitClipboardEvent(text);
        }
    }
}

void ClipboardSensor::emitClipboardEvent(const QString &text)
{
    Event event(EventType::Clipboard, QStringLiteral("copy"));
    event.contentPreview = extractTextPreview(text);

    event.metadata.insert(QStringLiteral("text_length"), text.length());
    event.metadata.insert(QStringLiteral("has_newlines"), text.contains('\n'));
    event.metadata.insert(QStringLiteral("line_count"), text.count('\n') + 1);

    // 检测内容类型
    if (text.startsWith(QStringLiteral("http://")) || text.startsWith(QStringLiteral("https://"))) {
        event.metadata.insert(QStringLiteral("content_type"), QStringLiteral("url"));
    } else if (text.length() > 50) {
        event.metadata.insert(QStringLiteral("content_type"), QStringLiteral("long_text"));
    } else {
        event.metadata.insert(QStringLiteral("content_type"), QStringLiteral("short_text"));
    }

    m_bus->publish(event);
    emit eventPublished(event);
}

QString ClipboardSensor::extractTextPreview(const QString &text, int maxLength) const
{
    if (text.length() <= maxLength) return text;

    QString preview = text.left(maxLength);
    preview.append(QStringLiteral("..."));
    return preview;
}

} // namespace Awareness
