#include "../src/core/StorageController.h"
#include "../src/core/Config.h"
#include "../src/utils/Logger.h"
#include "../src/utils/Redactor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTest>
#include <QTemporaryDir>

using namespace Awareness;

class TestStorage : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testInsertAction();
    void testQueryActions();
    void testRecentActions();
    void testSearchActions();
    void testClearHistory();
    void testClearAll();
    void cleanupTestCase();

private:
    QTemporaryDir m_tmpDir;
    QString m_dbPath;
    StorageController *m_storage = nullptr;
};

void TestStorage::initTestCase()
{
    QVERIFY(m_tmpDir.isValid());
    m_dbPath = m_tmpDir.path() + QStringLiteral("/test.db");

    // Config 需要初始化（不写文件，只用默认值）
    // Redactor 初始化内置规则
    Redactor::instance().initBuiltinRules();

    m_storage = new StorageController();
    QVERIFY(m_storage->init(m_dbPath));
}

void TestStorage::testInsertAction()
{
    Event ev(EventType::Window, QStringLiteral("activated"));
    ev.appName = QStringLiteral("org.deepin.editor");
    ev.appPid = 12345;
    ev.windowTitle = QStringLiteral("test.txt - Editor");

    const qint64 id = m_storage->insertAction(ev);
    QVERIFY(id > 0);
}

void TestStorage::testQueryActions()
{
    // 插入几条不同类型记录
    Event ev1(EventType::File, QStringLiteral("open"));
    ev1.appName = QStringLiteral("org.deepin.editor");
    ev1.filePath = QStringLiteral("/home/user/test.txt");
    m_storage->insertAction(ev1);

    Event ev2(EventType::Clipboard, QStringLiteral("copy"));
    ev2.contentPreview = QStringLiteral("copied text 13800138000 with phone");
    m_storage->insertAction(ev2);

    // 按类型查询
    QVariantMap filter;
    filter.insert(QStringLiteral("type"), QStringLiteral("file"));
    filter.insert(QStringLiteral("limit"), 10);
    const auto results = m_storage->queryActions(filter);
    QVERIFY(results.size() >= 1);
    QCOMPARE(results.first().value(QStringLiteral("type")).toString(), QStringLiteral("file"));
}

void TestStorage::testRecentActions()
{
    Event ev(EventType::Input, QStringLiteral("key_press"));
    ev.appName = QStringLiteral("org.deepin.terminal");
    m_storage->insertAction(ev);

    const auto results = m_storage->recentActions(5);
    QVERIFY(!results.isEmpty());
    QVERIFY(results.size() <= 5);
}

void TestStorage::testSearchActions()
{
    Event ev(EventType::Window, QStringLiteral("switch"));
    ev.appName = QStringLiteral("deepin-music");
    ev.windowTitle = QStringLiteral("My Favorite Song");
    m_storage->insertAction(ev);

    const auto results = m_storage->searchActions(QStringLiteral("music"));
    QVERIFY(!results.isEmpty());
}

void TestStorage::testClearHistory()
{
    // 插入一条旧记录
    Event ev(EventType::System, QStringLiteral("alert"));
    ev.timestamp = QDateTime::currentMSecsSinceEpoch() - 86400000LL * 30; // 30天前
    m_storage->insertAction(ev);

    // 清理7天前
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - 86400000LL * 7;
    const int deleted = m_storage->clearHistory(cutoff);
    QVERIFY(deleted >= 1);
}

void TestStorage::testClearAll()
{
    const int deleted = m_storage->clearAll();
    QVERIFY(deleted >= 0);

    const auto results = m_storage->recentActions(100);
    QVERIFY(results.isEmpty());
}

void TestStorage::cleanupTestCase()
{
    m_storage->close();
    delete m_storage;
}

QTEST_MAIN(TestStorage)
#include "test_storage.moc"
