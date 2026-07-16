#pragma once

#include <QMainWindow>
#include <QDBusInterface>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QListWidget>
#include <QTextEdit>
#include <QTabWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QDateTime>

class DBusTestWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit DBusTestWindow(QWidget *parent = nullptr);
    ~DBusTestWindow();

private slots:
    // Context 接口
    void onGetActiveWindow();
    void onGetRecentFiles();
    void onGetClipboardContent();
    void onGetRecentActions();
    void onGetBrowserTabs();
    void onGetUserFocus();

    // History 接口
    void onQueryActions();
    void onGetActionStats();
    void onGetActivityDigest();
    void onGetBrowserHistory();

    // System 接口
    void onGetSystemStatus();
    void onGetNetworkInfo();
    void onGetStorageInfo();

    // Config 接口
    void onGetConfig();
    void onClearHistory();
    void onClearAll();

    // 事件信号槽
    void onWindowChanged(const QVariantMap &info);
    void onFileOpened(const QVariantMap &info);
    void onFileModified(const QVariantMap &info);
    void onClipboardChanged(const QVariantMap &info);
    void onInputPattern(const QVariantMap &info);
    void onSystemAlert(const QVariantMap &info);
    void onBrowserTabChanged(const QVariantMap &info);
    void onBrowserNavigation(const QVariantMap &info);
    void onBrowserDownload(const QVariantMap &info);

private:
    void setupUI();
    void setupDBus();
    void appendToLog(const QString &msg);
    void appendVariantMapList(const QList<QVariantMap> &list, const QString &title);
    void appendVariantMap(const QVariantMap &map, const QString &title);
    void handlePendingCall(QDBusPendingCallWatcher *watcher, const QString &title);

    // D-Bus 接口
    QDBusInterface *m_contextIface = nullptr;
    QDBusInterface *m_historyIface = nullptr;
    QDBusInterface *m_systemIface = nullptr;
    QDBusInterface *m_configIface = nullptr;
    QDBusInterface *m_eventsIface = nullptr;

    // UI 控件
    QTabWidget *m_tabWidget = nullptr;
    QTextEdit *m_logOutput = nullptr;
    QTextEdit *m_eventOutput = nullptr;

    // 参数输入
    QSpinBox *m_limitSpin = nullptr;
    QSpinBox *m_sinceSpin = nullptr;
    QSpinBox *m_untilSpin = nullptr;
    QLineEdit *m_keywordEdit = nullptr;
    QTextEdit *m_filterEdit = nullptr;
    static constexpr const char *kService = "org.deepin.EnvironmentAwareness";
    static constexpr const char *kPath = "/org/deepin/EnvironmentAwareness";
    int m_eventCount = 0;
};
