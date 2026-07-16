#include "DBusTestWindow.h"

#include <QScrollBar>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
// 构造常量
static const char *kContextInterface = "org.deepin.EnvironmentAwareness.Context";
static const char *kHistoryInterface = "org.deepin.EnvironmentAwareness.History";
static const char *kSystemInterface  = "org.deepin.EnvironmentAwareness.System";
static const char *kConfigInterface = "org.deepin.EnvironmentAwareness.Config";
static const char *kEventsInterface  = "org.deepin.EnvironmentAwareness.Events";

DBusTestWindow::DBusTestWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();
    setupDBus();
}

DBusTestWindow::~DBusTestWindow() = default;

void DBusTestWindow::setupUI()
{
    setWindowTitle(QStringLiteral("Environment Awareness - D-Bus Test Tool"));
    resize(1000, 750);

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QHBoxLayout(central);
    // 左侧：功能面板，右侧：输出面板（水平分割，可拖动）
    auto *hSplitter = new QSplitter(Qt::Horizontal);

    m_tabWidget = new QTabWidget;
    m_tabWidget->setMinimumWidth(280);
    hSplitter->addWidget(m_tabWidget);
    // 右侧：输出面板（上下分割）
    auto *splitter = new QSplitter(Qt::Vertical);

    // 上：调用结果日志
    auto *logGroup = new QGroupBox(QStringLiteral("调用结果"));
    auto *logLayout = new QVBoxLayout(logGroup);
    m_logOutput = new QTextEdit;
    m_logOutput->setReadOnly(true);
    m_logOutput->setFont(QFont("Monospace", 9));
    logLayout->addWidget(m_logOutput);
    auto *clearLogBtn = new QPushButton(QStringLiteral("清空日志"));
    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(clearLogBtn);
    auto *clearDbBtn = new QPushButton(QStringLiteral("清除数据库"));
    clearDbBtn->setStyleSheet("QPushButton { color: #e74c3c; }");
    btnRow->addWidget(clearDbBtn);
    logLayout->addLayout(btnRow);
    connect(clearLogBtn, &QPushButton::clicked, m_logOutput, &QTextEdit::clear);
    connect(clearDbBtn, &QPushButton::clicked, this, [this]() {
        QDBusInterface iface("org.deepin.EnvironmentAwareness",
                             "/org/deepin/EnvironmentAwareness",
                             "org.deepin.EnvironmentAwareness.Config",
                             QDBusConnection::sessionBus());
        auto reply = iface.call("ClearAll");
        if (reply.type() == QDBusMessage::ReplyMessage) {
            int count = reply.arguments().first().toInt();
            m_logOutput->append(QStringLiteral("已清除 %1 条记录").arg(count));
        } else {
            m_logOutput->append(QStringLiteral("清除失败: %1").arg(reply.errorMessage()));
        }
    });
    splitter->addWidget(logGroup);

    // 下：实时事件
    auto *eventGroup = new QGroupBox(QStringLiteral("实时事件推送 (Events)"));
    auto *eventLayout = new QVBoxLayout(eventGroup);
    m_eventOutput = new QTextEdit;
    m_eventOutput->setReadOnly(true);
    m_eventOutput->setFont(QFont("Monospace", 9));
    eventLayout->addWidget(m_eventOutput);
    auto *clearEventBtn = new QPushButton(QStringLiteral("清空事件"));
    eventLayout->addWidget(clearEventBtn);
    connect(clearEventBtn, &QPushButton::clicked, m_eventOutput, &QTextEdit::clear);
    splitter->addWidget(eventGroup);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    hSplitter->addWidget(splitter);
    hSplitter->setStretchFactor(0, 0);   // 左侧按钮面板不自动拉伸
    hSplitter->setStretchFactor(1, 1);   // 右侧输出面板自动拉伸
    // 设置初始宽度比例
    hSplitter->setSizes(QList<int>{420, 580});
    mainLayout->addWidget(hSplitter);
    // ========== Tab 1: Context ==========
    auto *contextTab = new QWidget;
    auto *ctxLayout = new QVBoxLayout(contextTab);

    auto createBtn = [&](const QString &text, std::function<void()> fn) {
        auto *btn = new QPushButton(text);
        connect(btn, &QPushButton::clicked, this, [fn](bool) { fn(); });
        btn->setMinimumHeight(36);
        ctxLayout->addWidget(btn);
        return btn;
    };

    createBtn(QStringLiteral("GetActiveWindow"), [this]() { onGetActiveWindow(); });
    createBtn(QStringLiteral("GetRecentFiles"), [this]() { onGetRecentFiles(); });
    createBtn(QStringLiteral("GetClipboardContent"), [this]() { onGetClipboardContent(); });
    createBtn(QStringLiteral("GetRecentActions"), [this]() { onGetRecentActions(); });

    auto *limitLabel = new QLabel(QStringLiteral("limit:"));
    auto *limitRow = new QHBoxLayout;
    limitRow->addWidget(limitLabel);
    m_limitSpin = new QSpinBox;
    m_limitSpin->setRange(1, 1000);
    m_limitSpin->setValue(20);
    limitRow->addWidget(m_limitSpin);
    limitRow->addStretch();
    ctxLayout->addLayout(limitRow);

    createBtn(QStringLiteral("GetBrowserTabs"), [this]() { onGetBrowserTabs(); });
    createBtn(QStringLiteral("GetUserFocus"), [this]() { onGetUserFocus(); });
    ctxLayout->addStretch();
    m_tabWidget->addTab(contextTab, QStringLiteral("Context"));

    // ========== Tab 2: History ==========
    auto *historyTab = new QWidget;
    auto *histLayout = new QVBoxLayout(historyTab);

    // QueryActions
    auto *filterGroup = new QGroupBox(QStringLiteral("QueryActions"));
    auto *filterLayout = new QVBoxLayout(filterGroup);
    m_filterEdit = new QTextEdit;
    m_filterEdit->setPlaceholderText(
        QStringLiteral("每行一个过滤条件，留空则不过滤。支持的键：\n"
                       "  type     — 事件类型: window/file/clipboard/input/browser\n"
                       "  app      — 应用名: e.g. code, deepin-editor\n"
                       "  keyword  — 模糊搜索: 匹配标题/路径/内容/应用名\n"
                       "  since    — 起始时间戳(毫秒)\n"
                       "  until    — 结束时间戳(毫秒)\n"
                       "  limit    — 最大返回条数\n"
                       "  offset   — 跳过前N条\n"
                       "示例：\n"
                       "  type=window\n"
                       "  app=code\n"
                       "  limit=20"));
    filterLayout->addWidget(m_filterEdit);
    histLayout->addWidget(filterGroup);

    auto historyCreateBtn = [&](const QString &text, std::function<void()> fn) {
        auto *btn = new QPushButton(text);
        connect(btn, &QPushButton::clicked, this, [fn](bool) { fn(); });
        btn->setMinimumHeight(36);
        histLayout->addWidget(btn);
        return btn;
    };

    historyCreateBtn(QStringLiteral("QueryActions"), [this]() { onQueryActions(); });
    historyCreateBtn(QStringLiteral("GetActionStats"), [this]() { onGetActionStats(); });

    auto *timelineGroup = new QGroupBox(QStringLiteral("GetActivityDigest"));
    auto *timelineGrid = new QGridLayout(timelineGroup);
    timelineGrid->addWidget(new QLabel(QStringLiteral("Since (h ago):")), 0, 0);
    m_sinceSpin = new QSpinBox;
    m_sinceSpin->setRange(1, 8760);
    m_sinceSpin->setValue(24);
    m_sinceSpin->setSuffix(QStringLiteral("h"));
    timelineGrid->addWidget(m_sinceSpin, 0, 1);
    timelineGrid->addWidget(new QLabel(QStringLiteral("Until (h ago):")), 1, 0);
    m_untilSpin = new QSpinBox;
    m_untilSpin->setRange(0, 8760);
    m_untilSpin->setValue(0);
    m_untilSpin->setSuffix(QStringLiteral("h"));
    timelineGrid->addWidget(m_untilSpin, 1, 1);
    histLayout->addWidget(timelineGroup);
    historyCreateBtn(QStringLiteral("GetActivityDigest"), [this]() { onGetActivityDigest(); });

    // GetBrowserHistory
    auto *browserRow = new QHBoxLayout;
    browserRow->addWidget(new QLabel(QStringLiteral("Limit:")));
    m_limitSpin = new QSpinBox;
    m_limitSpin->setRange(1, 1000);
    m_limitSpin->setValue(20);
    browserRow->addWidget(m_limitSpin);
    browserRow->addWidget(new QLabel(QStringLiteral("  Keyword:")));
    m_keywordEdit = new QLineEdit;
    m_keywordEdit->setPlaceholderText(QStringLiteral("留空获取全部，填入按关键词过滤"));
    browserRow->addWidget(m_keywordEdit);
    histLayout->addLayout(browserRow);
    historyCreateBtn(QStringLiteral("GetBrowserHistory"), [this]() { onGetBrowserHistory(); });

    histLayout->addStretch();
    m_tabWidget->addTab(historyTab, QStringLiteral("History"));

    // ========== Tab 3: System ==========
    auto *systemTab = new QWidget;
    auto *sysLayout = new QVBoxLayout(systemTab);
    auto sysCreateBtn = [&](const QString &text, std::function<void()> fn) {
        auto *btn = new QPushButton(text);
        connect(btn, &QPushButton::clicked, this, [fn](bool) { fn(); });
        btn->setMinimumHeight(36);
        sysLayout->addWidget(btn);
        return btn;
    };
    sysCreateBtn(QStringLiteral("GetSystemStatus"), [this]() { onGetSystemStatus(); });
    sysCreateBtn(QStringLiteral("GetNetworkInfo"), [this]() { onGetNetworkInfo(); });
    sysCreateBtn(QStringLiteral("GetStorageInfo"), [this]() { onGetStorageInfo(); });
    sysLayout->addStretch();
    m_tabWidget->addTab(systemTab, QStringLiteral("System"));

    // ========== Tab 4: Config ==========
    auto *configTab = new QWidget;
    auto *cfgLayout = new QVBoxLayout(configTab);
    auto cfgCreateBtn = [&](const QString &text, std::function<void()> fn) {
        auto *btn = new QPushButton(text);
        connect(btn, &QPushButton::clicked, this, [fn](bool) { fn(); });
        btn->setMinimumHeight(36);
        cfgLayout->addWidget(btn);
        return btn;
    };
    cfgCreateBtn(QStringLiteral("GetConfig"), [this]() { onGetConfig(); });

    auto *clearHGroup = new QGroupBox(QStringLiteral("ClearHistory"));
    auto *clearHGrid = new QGridLayout(clearHGroup);
    clearHGrid->addWidget(new QLabel(QStringLiteral("Before (days ago):")), 0, 0);
    auto *clearDaysSpin = new QSpinBox;
    clearDaysSpin->setRange(1, 365);
    clearDaysSpin->setValue(30);
    clearDaysSpin->setSuffix(QStringLiteral(" days"));
    clearHGrid->addWidget(clearDaysSpin, 0, 1);
    cfgLayout->addWidget(clearHGroup);

    auto *clearHBtn = new QPushButton(QStringLiteral("ClearHistory"));
    connect(clearHBtn, &QPushButton::clicked, this, [=]() {
        qint64 ts = QDateTime::currentMSecsSinceEpoch() - qint64(clearDaysSpin->value()) * 86400000LL;
        if (m_configIface) {
            QDBusPendingCall pcall = m_configIface->asyncCall(QStringLiteral("ClearHistory"), ts);
            auto *watcher = new QDBusPendingCallWatcher(pcall, this);
            connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
                handlePendingCall(watcher, "ClearHistory");
            });
        }
        appendToLog(QString(">> ClearHistory(before=%1)").arg(ts));
    });
    clearHBtn->setMinimumHeight(36);
    cfgLayout->addWidget(clearHBtn);

    cfgCreateBtn(QStringLiteral("ClearAll"), [this]() { onClearAll(); });
    cfgLayout->addStretch();
    m_tabWidget->addTab(configTab, QStringLiteral("Config"));

    appendToLog(QStringLiteral("=== Environment Awareness D-Bus Test Tool ==="));
    appendToLog(QStringLiteral("Service: org.deepin.EnvironmentAwareness"));
    appendToLog(QStringLiteral("Path:     /org/deepin/EnvironmentAwareness"));
    appendToLog(QString());
}

void DBusTestWindow::setupDBus()
{
    auto bus = QDBusConnection::sessionBus();

    m_contextIface = new QDBusInterface(
        kService, kPath, kContextInterface, bus, this);
    m_historyIface = new QDBusInterface(
        kService, kPath, kHistoryInterface, bus, this);
    m_systemIface = new QDBusInterface(
        kService, kPath, kSystemInterface, bus, this);
    m_configIface = new QDBusInterface(
        kService, kPath, kConfigInterface, bus, this);
    m_eventsIface = new QDBusInterface(
        kService, kPath, kEventsInterface, bus, this);

    // 订阅所有事件信号
    auto connectSignal = [&](const char *signal, const char *slot) {
        bus.connect(kService, kPath, kEventsInterface, signal, this, slot);
    };

    connectSignal("WindowChanged", SLOT(onWindowChanged(QVariantMap)));
    connectSignal("FileOpened", SLOT(onFileOpened(QVariantMap)));
    connectSignal("FileModified", SLOT(onFileModified(QVariantMap)));
    connectSignal("ClipboardChanged", SLOT(onClipboardChanged(QVariantMap)));
    connectSignal("InputPattern", SLOT(onInputPattern(QVariantMap)));
    connectSignal("SystemAlert", SLOT(onSystemAlert(QVariantMap)));
    connectSignal("BrowserTabChanged", SLOT(onBrowserTabChanged(QVariantMap)));
    connectSignal("BrowserNavigation", SLOT(onBrowserNavigation(QVariantMap)));
    connectSignal("BrowserDownload", SLOT(onBrowserDownload(QVariantMap)));

    appendToLog(QStringLiteral("[OK] D-Bus signals subscribed"));
}

// ========== 通用处理 ==========

void DBusTestWindow::appendToLog(const QString &msg)
{
    m_logOutput->append(msg);
    // 自动滚动到底部
    auto *sb = m_logOutput->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void DBusTestWindow::appendVariantMapList(const QList<QVariantMap> &list, const QString &title)
{
    appendToLog(QString("=== %1 (%2 items) ===").arg(title).arg(list.size()));
    if (list.isEmpty()) {
        appendToLog(QStringLiteral("  (empty)"));
        return;
    }
    for (int i = 0; i < list.size(); ++i) {
        appendToLog(QString("  [%1]").arg(i));
        const auto &m = list[i];
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            QString val;
            if (it.value().type() == QVariant::Map) {
                val = QString::fromUtf8(
                    QJsonDocument(QJsonObject::fromVariantMap(it.value().toMap()))
                    .toJson(QJsonDocument::Compact));
            } else {
                val = it.value().toString();
            }
            appendToLog(QString("    %1 = %2").arg(it.key(), val));
        }
    }
}

void DBusTestWindow::appendVariantMap(const QVariantMap &map, const QString &title)
{
    appendToLog(QString("=== %1 ===").arg(title));
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        QString val;
        if (it.value().type() == QVariant::Map) {
            val = QString::fromUtf8(
                QJsonDocument(QJsonObject::fromVariantMap(it.value().toMap()))
                .toJson(QJsonDocument::Compact));
        } else {
            val = it.value().toString();
        }
        appendToLog(QString("  %1 = %2").arg(it.key(), val));
    }
}

void DBusTestWindow::handlePendingCall(QDBusPendingCallWatcher *watcher, const QString &title)
{
    watcher->deleteLater();
    QDBusPendingReply<> reply = *watcher;

    if (reply.isError()) {
        appendToLog(QString("[ERROR] %1: %2").arg(title, reply.error().message()));
        return;
    }

    if (reply.reply().type() == QDBusMessage::ReplyMessage) {
        auto args = reply.reply().arguments();
        if (!args.isEmpty()) {
            const QVariant &first = args.first();
            // D-Bus 字典和字典列表以 QDBusArgument 传输，需要 qdbus_cast 解码
            if (first.userType() == qMetaTypeId<QDBusArgument>()) {
                auto dbusArg = first.value<QDBusArgument>();
                if (dbusArg.currentType() == QDBusArgument::MapType) {
                    // a{sv} → QVariantMap
                    auto map = qdbus_cast<QVariantMap>(dbusArg);
                    appendVariantMap(map, title);
                } else if (dbusArg.currentType() == QDBusArgument::ArrayType) {
                    // aa{sv} → QList<QVariantMap>
                    auto list = qdbus_cast<QList<QVariantMap>>(dbusArg);
                    if (list.isEmpty()) {
                        appendToLog(QString("=== %1 ===").arg(title));
                        appendToLog(QString("  (empty list)"));
                    } else {
                        appendVariantMapList(list, title);
                    }
                } else {
                    appendToLog(QString("=== %1 ===").arg(title));
                    appendToLog(QString("  result = %1").arg(first.toString()));
                }
            } else if (first.canConvert<QVariantMap>()) {
                appendVariantMap(first.toMap(), title);
            } else if (first.canConvert<int>()) {
                appendToLog(QString("=== %1 ===").arg(title));
                appendToLog(QString("  result = %1").arg(first.toInt()));
            } else if (first.canConvert<bool>()) {
                appendToLog(QString("=== %1 ===").arg(title));
                appendToLog(QString("  result = %1").arg(first.toBool() ? "true" : "false"));
            } else {
                appendToLog(QString("=== %1 ===").arg(title));
                appendToLog(QString("  result = %1").arg(first.toString()));
            }
        }
    }
    appendToLog(QString());
}

// ========== Context 接口 ==========

void DBusTestWindow::onGetActiveWindow()
{
    appendToLog(QStringLiteral(">> GetActiveWindow()"));
    auto *w = new QDBusPendingCallWatcher(m_contextIface->asyncCall("GetActiveWindow"), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetActiveWindow"); });
}

void DBusTestWindow::onGetRecentFiles()
{
    int limit = m_limitSpin->value();
    appendToLog(QStringLiteral(">> GetRecentFiles(limit=%1)").arg(limit));
    auto *w = new QDBusPendingCallWatcher(m_contextIface->asyncCall("GetRecentFiles", limit), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetRecentFiles"); });
}

void DBusTestWindow::onGetClipboardContent()
{
    appendToLog(QStringLiteral(">> GetClipboardContent()"));
    auto *w = new QDBusPendingCallWatcher(m_contextIface->asyncCall("GetClipboardContent"), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetClipboardContent"); });
}

void DBusTestWindow::onGetRecentActions()
{
    int limit = m_limitSpin->value();
    appendToLog(QStringLiteral(">> GetRecentActions(limit=%1)").arg(limit));
    auto *w = new QDBusPendingCallWatcher(m_contextIface->asyncCall("GetRecentActions", limit), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetRecentActions"); });
}

void DBusTestWindow::onGetBrowserTabs()
{
    appendToLog(QStringLiteral(">> GetBrowserTabs()"));
    auto *w = new QDBusPendingCallWatcher(m_contextIface->asyncCall("GetBrowserTabs"), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetBrowserTabs"); });
}

void DBusTestWindow::onGetUserFocus()
{
    appendToLog(QStringLiteral(">> GetUserFocus()"));
    auto *w = new QDBusPendingCallWatcher(m_contextIface->asyncCall("GetUserFocus"), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetUserFocus"); });
}

// ========== History 接口 ==========

void DBusTestWindow::onQueryActions()
{
    QVariantMap filter;
    QString text = m_filterEdit->toPlainText();
    QStringList parts;
    for (const auto &line : text.split('\n', QString::SkipEmptyParts)) {
        auto eq = line.indexOf('=');
        if (eq > 0) {
            QString key = line.left(eq).trimmed();
            QString val = line.mid(eq + 1).trimmed();
            // 数值类型自动转换
            bool ok;
            qint64 num = val.toLongLong(&ok);
            if (ok && (key == "since" || key == "until" || key == "limit" || key == "offset"))
                filter.insert(key, num);
            else
                filter.insert(key, val);
            parts << key + "=" + val;
        }
    }
    appendToLog(QStringLiteral(">> QueryActions(filter={%1})").arg(parts.join(", ")));
    auto *w = new QDBusPendingCallWatcher(m_historyIface->asyncCall("QueryActions", QVariant::fromValue(filter)), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "QueryActions"); });
}

void DBusTestWindow::onGetActionStats()
{
    appendToLog(QStringLiteral(">> GetActionStats()"));
    auto *w = new QDBusPendingCallWatcher(m_historyIface->asyncCall("GetActionStats", QVariantMap()), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetActionStats"); });
}

void DBusTestWindow::onGetBrowserHistory()
{
    int limit = m_limitSpin->value();
    QString keyword = m_keywordEdit->text();
    appendToLog(QStringLiteral(">> GetBrowserHistory(limit=%1, keyword=%2)").arg(limit).arg(keyword));
    auto *w = new QDBusPendingCallWatcher(m_historyIface->asyncCall("GetBrowserHistory", limit, keyword), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetBrowserHistory"); });
}
void DBusTestWindow::onGetActivityDigest()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 since = now - qint64(m_sinceSpin->value()) * 3600000LL;
    qint64 until = now - qint64(m_untilSpin->value()) * 3600000LL;
    appendToLog(QStringLiteral(">> GetActivityDigest(since=%1, until=%2)").arg(since).arg(until));
    auto *w = new QDBusPendingCallWatcher(
        m_historyIface->asyncCall("GetActivityDigest", since, until), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() {
        QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isValid()) {
            const QVariantMap result = reply.value();
            appendToLog(QStringLiteral("<< apps: %1, files: %2, urls: %3, clipboard: %4")
                .arg(result.value("apps").toList().size())
                .arg(result.value("files").toList().size())
                .arg(result.value("urls").toList().size())
                .arg(result.value("clipboard_count").toInt()));
            for (const auto &app : result.value("apps").toList()) {
                const auto m = app.toMap();
                appendToLog(QStringLiteral("  app: %1, start=%2, end=%3")
                    .arg(m.value("name").toString())
                    .arg(m.value("start_time").toLongLong())
                    .arg(m.value("end_time").toLongLong()));
            }
            for (const auto &file : result.value("files").toList()) {
                const auto m = file.toMap();
                appendToLog(QStringLiteral("  file: %1 (via %2)")
                    .arg(m.value("file_path").toString())
                    .arg(m.value("app").toString()));
            }
            for (const auto &url : result.value("urls").toList()) {
                const auto m = url.toMap();
                appendToLog(QStringLiteral("  url: %1 (%2)")
                    .arg(m.value("title").toString())
                    .arg(m.value("browser").toString()));
            }
        } else {
            appendToLog(QStringLiteral("<< error: %1").arg(reply.error().message()));
        }
        w->deleteLater();
    });
}

// ========== System 接口 ==========

void DBusTestWindow::onGetSystemStatus()
{
    appendToLog(QStringLiteral(">> GetSystemStatus()"));
    auto *w = new QDBusPendingCallWatcher(m_systemIface->asyncCall("GetSystemStatus"), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetSystemStatus"); });
}

void DBusTestWindow::onGetNetworkInfo()
{
    appendToLog(QStringLiteral(">> GetNetworkInfo()"));
    auto *w = new QDBusPendingCallWatcher(m_systemIface->asyncCall("GetNetworkInfo"), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetNetworkInfo"); });
}

void DBusTestWindow::onGetStorageInfo()
{
    appendToLog(QStringLiteral(">> GetStorageInfo()"));
    auto *w = new QDBusPendingCallWatcher(m_systemIface->asyncCall("GetStorageInfo"), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetStorageInfo"); });
}

// ========== Config 接口 ==========

void DBusTestWindow::onGetConfig()
{
    appendToLog(QStringLiteral(">> GetConfig()"));
    auto *w = new QDBusPendingCallWatcher(m_configIface->asyncCall("GetConfig"), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetConfig"); });
}

void DBusTestWindow::onClearHistory()
{
    // 通过 Tab4 内的按钮实现，此处为备用
}

void DBusTestWindow::onClearAll()
{
    appendToLog(QStringLiteral(">> ClearAll()"));
    auto *w = new QDBusPendingCallWatcher(m_configIface->asyncCall("ClearAll"), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "ClearAll"); });
}

// ========== 事件信号处理 ==========

static QString formatEventMap(const QVariantMap &info)
{
    QStringList lines;
    lines << QString("[%1] timestamp=%2 type=%3 action=%4")
               .arg(QDateTime::fromMSecsSinceEpoch(info.value("timestamp").toLongLong())
                    .toString("hh:mm:ss.zzz"))
               .arg(info.value("timestamp").toLongLong())
               .arg(info.value("type").toString())
               .arg(info.value("action").toString());

    // 核心字段
    if (info.contains("app_name"))
        lines << QString("  app_name: %1").arg(info.value("app_name").toString());
    if (info.contains("window_title"))
        lines << QString("  window_title: %1").arg(info.value("window_title").toString());
    if (info.contains("file_path"))
        lines << QString("  file_path: %1").arg(info.value("file_path").toString());
    if (info.contains("content_preview"))
        lines << QString("  content_preview: %1").arg(info.value("content_preview").toString().left(100));

    // metadata
    if (info.contains("metadata")) {
        auto meta = info.value("metadata").toMap();
        if (!meta.isEmpty()) {
            lines << "  metadata:";
            for (auto it = meta.constBegin(); it != meta.constEnd(); ++it) {
                lines << QString("    %1 = %2").arg(it.key(), it.value().toString());
            }
        }
    }

    // 剩余字段
    static const QSet<QString> coreKeys = {"timestamp", "type", "action", "app_name",
                                           "window_title", "file_path", "content_preview", "metadata"};
    for (auto it = info.constBegin(); it != info.constEnd(); ++it) {
        if (!coreKeys.contains(it.key())) {
            lines << QString("  %1 = %2").arg(it.key(), it.value().toString());
        }
    }

    return lines.join("\n");
}

void DBusTestWindow::onWindowChanged(const QVariantMap &info)
{
    m_eventCount++;
    m_eventOutput->append(QString("<b style='color:#4CAF50'>[%1] #Event%2 WindowChanged</b>")
        .arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(m_eventCount));
    m_eventOutput->append(formatEventMap(info));
    m_eventOutput->append("");
}

void DBusTestWindow::onFileOpened(const QVariantMap &info)
{
    m_eventCount++;
    m_eventOutput->append(QString("<b style='color:#2196F3'>[%1] #Event%2 FileOpened</b>")
        .arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(m_eventCount));
    m_eventOutput->append(formatEventMap(info));
    m_eventOutput->append("");
}

void DBusTestWindow::onFileModified(const QVariantMap &info)
{
    m_eventCount++;
    m_eventOutput->append(QString("<b style='color:#FF9800'>[%1] #Event%2 FileModified</b>")
        .arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(m_eventCount));
    m_eventOutput->append(formatEventMap(info));
    m_eventOutput->append("");
}

void DBusTestWindow::onClipboardChanged(const QVariantMap &info)
{
    m_eventCount++;
    m_eventOutput->append(QString("<b style='color:#9C27B0'>[%1] #Event%2 ClipboardChanged</b>")
        .arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(m_eventCount));
    m_eventOutput->append(formatEventMap(info));
    m_eventOutput->append("");
}

void DBusTestWindow::onInputPattern(const QVariantMap &info)
{
    m_eventCount++;
    m_eventOutput->append(QString("<b style='color:#FF5722'>[%1] #Event%2 InputPattern</b>")
        .arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(m_eventCount));
    m_eventOutput->append(formatEventMap(info));
    m_eventOutput->append("");
}

void DBusTestWindow::onSystemAlert(const QVariantMap &info)
{
    m_eventCount++;
    m_eventOutput->append(QString("<b style='color:#607D8B'>[%1] #Event%2 SystemAlert</b>")
        .arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(m_eventCount));
    m_eventOutput->append(formatEventMap(info));
    m_eventOutput->append("");
}

void DBusTestWindow::onBrowserTabChanged(const QVariantMap &info)
{
    m_eventCount++;
    m_eventOutput->append(QString("<b style='color:#E91E63'>[%1] #Event%2 BrowserTabChanged</b>")
        .arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(m_eventCount));
    m_eventOutput->append(formatEventMap(info));
    m_eventOutput->append("");
}

void DBusTestWindow::onBrowserNavigation(const QVariantMap &info)
{
    m_eventCount++;
    m_eventOutput->append(QString("<b style='color:#3F51B5'>[%1] #Event%2 BrowserNavigation</b>")
        .arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(m_eventCount));
    m_eventOutput->append(formatEventMap(info));
    m_eventOutput->append("");
}

void DBusTestWindow::onBrowserDownload(const QVariantMap &info)
{
    m_eventCount++;
    m_eventOutput->append(QString("<b style='color:#F44336'>[%1] #Event%2 BrowserDownload</b>")
        .arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(m_eventCount));
    m_eventOutput->append(formatEventMap(info));
    m_eventOutput->append("");
}

