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

    // 左侧：功能面板
    m_tabWidget = new QTabWidget;
    m_tabWidget->setFixedWidth(420);
    mainLayout->addWidget(m_tabWidget);

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
    logLayout->addWidget(clearLogBtn);
    connect(clearLogBtn, &QPushButton::clicked, m_logOutput, &QTextEdit::clear);
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
    mainLayout->addWidget(splitter, 1);

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
    auto *filterGroup = new QGroupBox(QStringLiteral("QueryActions (filter)"));
    auto *filterGrid = new QGridLayout(filterGroup);
    filterGrid->addWidget(new QLabel(QStringLiteral("Key:")), 0, 0);
    m_filterKeyEdit = new QLineEdit(QStringLiteral("type"));
    filterGrid->addWidget(m_filterKeyEdit, 0, 1);
    filterGrid->addWidget(new QLabel(QStringLiteral("Value:")), 1, 0);
    m_filterValueEdit = new QLineEdit(QStringLiteral("window"));
    filterGrid->addWidget(m_filterValueEdit, 1, 1);
    histLayout->addWidget(filterGroup);

    auto historyCreateBtn = [&](const QString &text, std::function<void()> fn) {
        auto *btn = new QPushButton(text);
        connect(btn, &QPushButton::clicked, this, [fn](bool) { fn(); });
        btn->setMinimumHeight(36);
        histLayout->addWidget(btn);
        return btn;
    };

    historyCreateBtn(QStringLiteral("QueryActions"), [this]() { onQueryActions(); });

    auto *appRow = new QHBoxLayout;
    appRow->addWidget(new QLabel(QStringLiteral("App Name:")));
    m_appNameEdit = new QLineEdit;
    m_appNameEdit->setPlaceholderText(QStringLiteral("e.g. deepin-editor"));
    appRow->addWidget(m_appNameEdit);
    histLayout->addLayout(appRow);

    historyCreateBtn(QStringLiteral("QueryActionsByApp"), [this]() { onQueryActionsByApp(); });
    historyCreateBtn(QStringLiteral("GetActionStats"), [this]() { onGetActionStats(); });

    auto *timelineGroup = new QGroupBox(QStringLiteral("GetTimeline"));
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
    historyCreateBtn(QStringLiteral("GetTimeline"), [this]() { onGetTimeline(); });

    auto *keywordRow = new QHBoxLayout;
    keywordRow->addWidget(new QLabel(QStringLiteral("Keyword:")));
    m_keywordEdit = new QLineEdit;
    m_keywordEdit->setPlaceholderText(QStringLiteral("e.g. deepin"));
    keywordRow->addWidget(m_keywordEdit);
    histLayout->addLayout(keywordRow);

    historyCreateBtn(QStringLiteral("GetBrowserHistory"), [this]() { onGetBrowserHistory(); });
    historyCreateBtn(QStringLiteral("SearchBrowserHistory"), [this]() { onSearchBrowserHistory(); });
    historyCreateBtn(QStringLiteral("SearchActions"), [this]() { onSearchActions(); });
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
    filter.insert(m_filterKeyEdit->text(), m_filterValueEdit->text());
    appendToLog(QStringLiteral(">> QueryActions(filter={%1:%2})")
                .arg(m_filterKeyEdit->text(), m_filterValueEdit->text()));
    auto *w = new QDBusPendingCallWatcher(m_historyIface->asyncCall("QueryActions", filter), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "QueryActions"); });
}

void DBusTestWindow::onQueryActionsByApp()
{
    QString app = m_appNameEdit->text();
    int limit = m_limitSpin->value();
    appendToLog(QStringLiteral(">> QueryActionsByApp(app=%1, limit=%2)").arg(app).arg(limit));
    auto *w = new QDBusPendingCallWatcher(m_historyIface->asyncCall("QueryActionsByApp", app, limit), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "QueryActionsByApp"); });
}

void DBusTestWindow::onGetActionStats()
{
    QVariantMap filter;
    filter.insert(m_filterKeyEdit->text(), m_filterValueEdit->text());
    appendToLog(QStringLiteral(">> GetActionStats(filter={%1:%2})")
                .arg(m_filterKeyEdit->text(), m_filterValueEdit->text()));
    auto *w = new QDBusPendingCallWatcher(m_historyIface->asyncCall("GetActionStats", filter), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetActionStats"); });
}

void DBusTestWindow::onGetTimeline()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 since = now - qint64(m_sinceSpin->value()) * 3600000LL;
    qint64 until = now - qint64(m_untilSpin->value()) * 3600000LL;
    appendToLog(QStringLiteral(">> GetTimeline(since=%1, until=%2)").arg(since).arg(until));
    auto *w = new QDBusPendingCallWatcher(m_historyIface->asyncCall("GetTimeline", since, until), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetTimeline"); });
}

void DBusTestWindow::onGetBrowserHistory()
{
    int limit = m_limitSpin->value();
    QString keyword = m_keywordEdit->text();
    appendToLog(QStringLiteral(">> GetBrowserHistory(limit=%1, keyword=%2)").arg(limit).arg(keyword));
    auto *w = new QDBusPendingCallWatcher(m_historyIface->asyncCall("GetBrowserHistory", limit, keyword), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "GetBrowserHistory"); });
}

void DBusTestWindow::onSearchBrowserHistory()
{
    QString keyword = m_keywordEdit->text();
    int limit = m_limitSpin->value();
    appendToLog(QStringLiteral(">> SearchBrowserHistory(keyword=%1, limit=%2)").arg(keyword).arg(limit));
    auto *w = new QDBusPendingCallWatcher(m_historyIface->asyncCall("SearchBrowserHistory", keyword, limit), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "SearchBrowserHistory"); });
}

void DBusTestWindow::onSearchActions()
{
    QString keyword = m_keywordEdit->text();
    appendToLog(QStringLiteral(">> SearchActions(keyword=%1)").arg(keyword));
    auto *w = new QDBusPendingCallWatcher(m_historyIface->asyncCall("SearchActions", keyword), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w]() { handlePendingCall(w, "SearchActions"); });
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

