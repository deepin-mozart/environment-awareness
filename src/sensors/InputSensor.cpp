#include "InputSensor.h"
#include "../core/Config.h"
#include "../utils/Logger.h"

#include <QDateTime>

// X11 / XRecord headers
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/record.h>
#include <X11/Xproto.h>

namespace Awareness {

// XRecord 回调数据结构，用于传递 this 指针
struct InputRecordData {
    InputSensor *sensor = nullptr;
    int keyPresses = 0;
    int mouseClicks = 0;
    int mouseMoves = 0;
    int scrolls = 0;
    bool hasModifiers = false;
    qint64 lastKeyTime = 0;
    qint64 lastMouseTime = 0;
};

static InputRecordData g_recordData;
static bool g_recordActive = false;

// XRecord 回调函数（XRecord 要求是 C 风格回调）
static void inputRecordCallback(XPointer closure, XRecordInterceptData *data)
{
    Q_UNUSED(closure);
    if (!data || !g_recordActive) return;

    if (data->category == XRecordFromServer) {
        auto now = QDateTime::currentMSecsSinceEpoch();
        xEvent *xev = reinterpret_cast<xEvent *>(data->data);

        int type = xev->u.u.type & 0x7F;

        if (type == KeyPress || type == KeyRelease) {
            g_recordData.keyPresses++;
            g_recordData.lastKeyTime = now;
            // 检测修饰键
            KeyCode code = xev->u.u.detail;
            auto *display = XOpenDisplay(nullptr);
            if (display) {
                KeySym sym = XKeycodeToKeysym(display, code, 0);
                XCloseDisplay(display);
                if (sym == XK_Control_L || sym == XK_Control_R ||
                    sym == XK_Alt_L || sym == XK_Alt_R ||
                    sym == XK_Shift_L || sym == XK_Shift_R ||
                    sym == XK_Super_L || sym == XK_Super_R) {
                    g_recordData.hasModifiers = true;
                }
            }
        } else if (type == ButtonPress) {
            g_recordData.mouseClicks++;
            g_recordData.lastMouseTime = now;
            if (xev->u.u.detail == 4 || xev->u.u.detail == 5) {
                g_recordData.scrolls++;
            }
        } else if (type == MotionNotify) {
            g_recordData.mouseMoves++;
            g_recordData.lastMouseTime = now;
        }
    }

    XRecordFreeData(data);
}

InputSensor::InputSensor(EventBus *bus, QObject *parent)
    : ISensor(QStringLiteral("input"), parent), m_bus(bus)
{
}

bool InputSensor::start()
{
    if (m_running) return true;

    auto &cfg = Config::instance();
    if (!cfg.sensorEnabled(m_name)) {
        awLogWarning() << "InputSensor disabled by config";
        return false;
    }

    // 尝试建立 XRecord 监听
    bool xrecordOk = setupXRecord();

    if (!xrecordOk) {
        awLogWarning() << "InputSensor: XRecord unavailable, using aggregate timer only";
    }

    // 聚合定时器：定期输出输入模式摘要
    auto interval = cfg.sensorConfig(m_name).value(
        QStringLiteral("aggregate_interval_ms")).toInt(AGGREGATE_INTERVAL_MS);
    m_aggregateTimer = new QTimer(this);
    m_aggregateTimer->setInterval(interval);
    connect(m_aggregateTimer, &QTimer::timeout, this, &InputSensor::onAggregateTimer);
    m_aggregateTimer->start();

    m_running = true;
    emit statusChanged(QStringLiteral("running"));
    awLogInfo() << "InputSensor started (xrecord:" << xrecordOk << ")";
    return true;
}

void InputSensor::stop()
{
    if (!m_running) return;

    if (m_aggregateTimer) {
        m_aggregateTimer->stop();
        m_aggregateTimer->deleteLater();
        m_aggregateTimer = nullptr;
    }

    cleanupXRecord();

    m_running = false;
    emit statusChanged(QStringLiteral("stopped"));
    awLogInfo() << "InputSensor stopped";
}

bool InputSensor::setupXRecord()
{
    auto *display = XOpenDisplay(nullptr);
    if (!display) return false;

    m_x11Display = static_cast<void*>(display);

    // 检查 XRecord 扩展
    int major, minor;
    if (!XRecordQueryVersion(display, &major, &minor)) {
        awLogWarning() << "InputSensor: XRecord extension not available";
        return false;
    }

    // 创建 XRecord context：监控所有客户端的键鼠输入
    XRecordClientSpec clients = XRecordAllClients;
    XRecordRange *range = XRecordAllocRange();
    if (!range) {
        awLogWarning() << "InputSensor: failed to allocate XRecord range";
        return false;
    }

    range->device_events.first = KeyPress;
    range->device_events.last = MotionNotify;

    m_xrecordContext = XRecordCreateContext(display, 0, &clients, 1, &range, 1);
    XFree(range);

    if (!m_xrecordContext) {
        awLogWarning() << "InputSensor: failed to create XRecord context";
        return false;
    }

    // 设置回调数据
    g_recordData = InputRecordData{this, 0, 0, 0, 0, false, 0, 0};
    g_recordActive = true;

    // 启动 XRecord（异步，在独立连接上运行）
    if (!XRecordEnableContextAsync(display, m_xrecordContext,
                                   inputRecordCallback, nullptr)) {
        awLogWarning() << "InputSensor: failed to enable XRecord";
        XRecordFreeContext(display, m_xrecordContext);
        m_xrecordContext = 0;
        g_recordActive = false;
        return false;
    }

    // XRecord 需要一个额外的 X11 连接来分发记录数据
    // 在实际环境中 Qt 事件循环会处理 XRecord 的内部连接
    awLogInfo() << "InputSensor: XRecord context created";
    return true;
}

void InputSensor::cleanupXRecord()
{
    g_recordActive = false;

    if (m_xrecordContext && m_x11Display) {
        auto *display = static_cast<Display*>(m_x11Display);
        XRecordDisableContext(display, m_xrecordContext);
        XRecordFreeContext(display, m_xrecordContext);
        m_xrecordContext = 0;
    }

    if (m_x11Display) {
        XCloseDisplay(static_cast<Display*>(m_x11Display));
        m_x11Display = nullptr;
    }
}

void InputSensor::onAggregateTimer()
{
    // 从 XRecord 回调数据收集统计
    m_keyPressCount += g_recordData.keyPresses;
    m_mouseClickCount += g_recordData.mouseClicks;
    m_mouseMoveCount += g_recordData.mouseMoves;
    m_scrollCount += g_recordData.scrolls;
    if (g_recordData.hasModifiers) m_hasModifiers = true;
    if (g_recordData.lastKeyTime > m_lastKeyTimestamp)
        m_lastKeyTimestamp = g_recordData.lastKeyTime;
    if (g_recordData.lastMouseTime > m_lastMouseTimestamp)
        m_lastMouseTimestamp = g_recordData.lastMouseTime;

    // 重置回调计数器
    g_recordData = InputRecordData{this, 0, 0, 0, 0, false, 0, 0};

    // 如果没有任何输入，跳过
    if (m_keyPressCount == 0 && m_mouseClickCount == 0 &&
        m_mouseMoveCount == 0 && m_scrollCount == 0) {
        return;
    }

    emitInputPattern();

    // 重置计数器
    m_keyPressCount = 0;
    m_mouseClickCount = 0;
    m_mouseMoveCount = 0;
    m_scrollCount = 0;
    m_hasModifiers = false;
}

void InputSensor::emitInputPattern()
{
    auto now = QDateTime::currentMSecsSinceEpoch();

    // 识别输入模式
    QString pattern;
    qint64 activeDuration = 0;

    if (m_keyPressCount > 0 && m_mouseClickCount == 0 && m_scrollCount == 0) {
        if (m_hasModifiers) {
            pattern = QStringLiteral("shortcut");
        } else if (m_keyPressCount > 20) {
            pattern = QStringLiteral("typing_fast");
        } else {
            pattern = QStringLiteral("typing");
        }
        activeDuration = m_lastKeyTimestamp > 0 ? (now - m_lastKeyTimestamp) : 0;
    } else if (m_mouseClickCount > 0 && m_keyPressCount == 0) {
        if (m_scrollCount > 0) {
            pattern = QStringLiteral("scrolling");
        } else if (m_mouseClickCount > 5) {
            pattern = QStringLiteral("clicking_rapid");
        } else {
            pattern = QStringLiteral("clicking");
        }
        activeDuration = m_lastMouseTimestamp > 0 ? (now - m_lastMouseTimestamp) : 0;
    } else if (m_mouseMoveCount > 100 && m_keyPressCount == 0 && m_mouseClickCount == 0) {
        pattern = QStringLiteral("mouse_move");
        activeDuration = m_lastMouseTimestamp > 0 ? (now - m_lastMouseTimestamp) : 0;
    } else if (m_keyPressCount > 0 && m_mouseClickCount > 0) {
        pattern = QStringLiteral("mixed_input");
        activeDuration = qMax(m_lastKeyTimestamp, m_lastMouseTimestamp) > 0
                         ? (now - qMax(m_lastKeyTimestamp, m_lastMouseTimestamp))
                         : 0;
    } else {
        pattern = QStringLiteral("idle");
        activeDuration = 0;
    }

    Event event(EventType::Input, pattern);
    event.metadata.insert(QStringLiteral("key_presses"), m_keyPressCount);
    event.metadata.insert(QStringLiteral("mouse_clicks"), m_mouseClickCount);
    event.metadata.insert(QStringLiteral("mouse_moves"), m_mouseMoveCount);
    event.metadata.insert(QStringLiteral("scrolls"), m_scrollCount);
    event.metadata.insert(QStringLiteral("active_duration_ms"), activeDuration);

    m_bus->publish(event);
    emit eventPublished(event);
}

void InputSensor::handleKeyEvent() { /* 由 XRecord 回调处理 */ }
void InputSensor::handleMouseEvent() { /* 由 XRecord 回调处理 */ }

} // namespace Awareness
