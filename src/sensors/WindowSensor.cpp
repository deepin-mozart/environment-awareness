#include "WindowSensor.h"
#include "../core/Config.h"
#include "../utils/Logger.h"

#include <QFile>
#include <QGuiApplication>
#include <QTimer>

// X11 headers
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

namespace Awareness {

WindowSensor::WindowSensor(EventBus *bus, QObject *parent)
    : ISensor(QStringLiteral("window"), parent), m_bus(bus)
{
}

bool WindowSensor::start()
{
    if (m_running) return true;

    auto &cfg = Config::instance();
    if (!cfg.sensorEnabled(m_name)) {
        awLogWarning() << "WindowSensor disabled by config";
        return false;
    }

    // 初始化 X11 连接
    setupX11Monitor();

    // 备用轮询：如果 X11 事件监听失败，用定时器轮询
    auto interval = cfg.sensorConfig(m_name).value(QStringLiteral("poll_interval_ms")).toInt(1000);
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(interval);
    connect(m_pollTimer, &QTimer::timeout, this, &WindowSensor::onActiveWindowChanged);
    m_pollTimer->start();
    // 首次获取活动窗口 appName（不发送事件，仅填充状态）
    if (m_x11Display) {
        auto *display = static_cast<Display *>(m_x11Display);
        Window root = DefaultRootWindow(display);
        Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
        Atom actualType; int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *data = nullptr;
        if (XGetWindowProperty(display, root, activeAtom, 0, 1, False,
                                XA_WINDOW, &actualType, &actualFormat,
                                &nitems, &bytesAfter, &data) == Success && data) {
            qint64 wid = static_cast<qint64>(*(Window *)data);
            XFree(data);
            m_lastWindowId = wid;
            Atom pidAtom = XInternAtom(display, "_NET_WM_PID", True);
            if (wid && pidAtom != None) {
                if (XGetWindowProperty(display, static_cast<Window>(wid), pidAtom, 0, 1, False,
                                        XA_CARDINAL, &actualType, &actualFormat,
                                        &nitems, &bytesAfter, &data) == Success && data) {
                    qint64 pid = *(unsigned long *)data;
                    XFree(data);
                    m_lastPid = pid;
                    m_lastAppName = getAppNameFromPid(pid);
                }
            }
        }
    }

    m_running = true;
    emit statusChanged(QStringLiteral("running"));
    awLogInfo() << "WindowSensor started";
    return true;
}

void WindowSensor::stop()
{
    if (!m_running) return;

    if (m_pollTimer) {
        m_pollTimer->stop();
        m_pollTimer->deleteLater();
        m_pollTimer = nullptr;
    }

    cleanupX11Monitor();

    m_running = false;
    emit statusChanged(QStringLiteral("stopped"));
    awLogInfo() << "WindowSensor stopped";
}

void WindowSensor::setupX11Monitor()
{
    auto *display = XOpenDisplay(nullptr);
    if (!display) {
        awLogWarning() << "WindowSensor: cannot open X11 display, using poll only";
        return;
    }

    m_x11Display = display;

    // 监听根窗口的 PropertyNotify 事件来检测活动窗口变化
    Window root = DefaultRootWindow(display);
    Atom activeWindowAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);

    // 获取当前活动窗口
    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char *data = nullptr;
    if (XGetWindowProperty(display, root, activeWindowAtom, 0, 1, False,
                            XA_WINDOW, &actualType, &actualFormat,
                            &nitems, &bytesAfter, &data) == Success && data) {
        m_lastWindowId = *(Window *)data;
        XFree(data);
    }

    // 选择根窗口的 PropertyChangeMask
    XSelectInput(display, root, PropertyChangeMask);
    XFlush(display);

    // 创建 QSocketNotifier 监听 X11 连接
    int fd = ConnectionNumber(display);
    m_x11Notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_x11Notifier, &QSocketNotifier::activated, this, [this]() {
        // 读取并分发 X11 事件
        auto *display = static_cast<Display *>(m_x11Display);
        XEvent event;
        while (XPending(display) > 0) {
            XNextEvent(display, &event);
            if (event.type == PropertyNotify) {
                auto *pn = &event.xproperty;
                Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
                if (pn->atom == activeAtom) {
                    onActiveWindowChanged();
                }
            }
        }
    });

    m_x11Connection = fd;
    awLogInfo() << "WindowSensor: X11 monitor setup on fd" << fd;
}

void WindowSensor::cleanupX11Monitor()
{
    if (m_x11Notifier) {
        m_x11Notifier->deleteLater();
        m_x11Notifier = nullptr;
    }

    if (m_x11Display) {
        auto *display = static_cast<Display *>(m_x11Display);
        XCloseDisplay(display);
        m_x11Display = nullptr;
        m_x11Connection = -1;
    }
}

void WindowSensor::onActiveWindowChanged()
{
    QString currentTitle = getCurrentActiveWindow();

    // 没有变化则跳过（空值相等也跳过，避免连续空事件）
    if (currentTitle == m_lastWindowTitle) {
        return;
    }

    QString appName;
    qint64 windowId = 0;
    qint64 pid = 0;

    // 获取当前活动窗口信息
    if (m_x11Display) {
        auto *display = static_cast<Display *>(m_x11Display);
        Window root = DefaultRootWindow(display);
        Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);

        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *data = nullptr;
        if (XGetWindowProperty(display, root, activeAtom, 0, 1, False,
                                XA_WINDOW, &actualType, &actualFormat,
                                &nitems, &bytesAfter, &data) == Success && data) {
            windowId = static_cast<qint64>(*(Window *)data);
            XFree(data);
        }

        // 获取窗口的 PID
        Atom pidAtom = XInternAtom(display, "_NET_WM_PID", True);
        if (windowId && pidAtom != None) {
            if (XGetWindowProperty(display, static_cast<Window>(windowId), pidAtom, 0, 1, False,
                                    XA_CARDINAL, &actualType, &actualFormat,
                                    &nitems, &bytesAfter, &data) == Success && data) {
                pid = *(unsigned long *)data;
                XFree(data);
                appName = getAppNameFromPid(pid);
            }
        }
    }

    // 判断动作类型
    QString action;
    if (m_lastWindowTitle.isEmpty() && !currentTitle.isEmpty()) {
        action = QStringLiteral("open");
    } else if (!m_lastWindowTitle.isEmpty() && currentTitle.isEmpty()) {
        action = QStringLiteral("close");
    } else {
        action = QStringLiteral("switch");
    }

    Event event(EventType::Window, action);
    event.windowTitle = currentTitle;
    event.appName = appName;
    event.appPid = pid;
    event.contentPreview = currentTitle.left(200);

    m_bus->publish(event);
    emit eventPublished(event);

    m_lastWindowTitle = currentTitle;
    m_lastAppName = appName;
    m_lastWindowId = windowId;
    m_lastPid = pid;
}

QString WindowSensor::getCurrentActiveWindow()
{
    if (!m_x11Display) return {};

    auto *display = static_cast<Display *>(m_x11Display);
    Window root = DefaultRootWindow(display);
    Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);

    if (activeAtom == None) return {};

    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char *data = nullptr;

    if (XGetWindowProperty(display, root, activeAtom, 0, 1, False,
                            XA_WINDOW, &actualType, &actualFormat,
                            &nitems, &bytesAfter, &data) != Success || !data) {
        return {};
    }

    Window activeWin = *(Window *)data;
    XFree(data);

    if (activeWin == None) return {};

    // 获取窗口名称
    Atom nameAtom = XInternAtom(display, "_NET_WM_NAME", True);
    Atom utf8Atom = XInternAtom(display, "UTF8_STRING", True);

    QString title;
    if (XGetWindowProperty(display, activeWin, nameAtom, 0, 1024, False,
                            utf8Atom != None ? utf8Atom : XA_STRING,
                            &actualType, &actualFormat,
                            &nitems, &bytesAfter, &data) == Success && data) {
        title = QString::fromUtf8(reinterpret_cast<char *>(data), nitems);
        XFree(data);
    }

    return title;
}

QString WindowSensor::getAppNameFromPid(qint64 pid)
{
    if (pid <= 0) return {};

    // 读取 /proc/<pid>/comm 获取进程名
    QString commPath = QStringLiteral("/proc/%1/comm").arg(pid);
    QFile f(commPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString name = QString::fromUtf8(f.readLine().trimmed());
        f.close();
        return name;
    }

    return {};
}


QVariantMap WindowSensor::activeWindowInfo() const
{
    QVariantMap map;
    map.insert(QStringLiteral("title"), m_lastWindowTitle);
    map.insert(QStringLiteral("app_name"), m_lastAppName);
    map.insert(QStringLiteral("pid"), m_lastPid);
    map.insert(QStringLiteral("wm_class"), QString());
    map.insert(QStringLiteral("window_id"), m_lastWindowId);
    map.insert(QStringLiteral("is_minimized"), false);
    return map;
}
} // namespace Awareness
