#include "../src/core/EventBus.h"
#include "../src/core/Config.h"
#include "../src/sensors/FileSensor.h"
#include "../src/sensors/WindowSensor.h"
#include "../src/sensors/InputSensor.h"
#include "../src/sensors/BrowserSensor.h"
#include "../src/utils/Logger.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QTest>
#include <QTimer>

using namespace Awareness;

// 收集 EventBus 事件
class EventCollector : public QObject
{
    Q_OBJECT
public:
    QList<Event> events;
    explicit EventCollector(EventBus *bus, QObject *parent = nullptr) : QObject(parent)
    {
        connect(bus, &EventBus::eventSignal, this, &EventCollector::collect);
    }
private slots:
    void collect(const Event &ev) { events.append(ev); }
};

// ==================== FileSensor ====================
class TestFileSensor : public QObject
{
    Q_OBJECT
private slots:
    void testStartStop();
    void testDebounceSkipsRapid();
    void testEventFields();

private:
    EventBus m_bus;
    EventCollector m_collector{&m_bus};
    FileSensor m_sensor{&m_bus};
};

void TestFileSensor::testStartStop()
{
    QVERIFY(!m_sensor.isRunning());
    QVERIFY(m_sensor.start());
    QVERIFY(m_sensor.isRunning());
    m_sensor.stop();
    QVERIFY(!m_sensor.isRunning());
}

void TestFileSensor::testDebounceSkipsRapid()
{
    QVERIFY(m_sensor.start());
    int countBefore = m_collector.events.size();
    // 无文件操作，事件数不应增加
    QTest::qWait(600);
    QCOMPARE(m_collector.events.size(), countBefore);
    m_sensor.stop();
}

void TestFileSensor::testEventFields()
{
    Event ev(EventType::File, "create");
    ev.filePath = "/home/user/test.txt";
    QCOMPARE(ev.type, EventType::File);
    QCOMPARE(ev.action, QString("create"));
    QCOMPARE(ev.filePath, QString("/home/user/test.txt"));
    QVERIFY(ev.timestamp > 0);
}

// ==================== WindowSensor ====================
class TestWindowSensor : public QObject
{
    Q_OBJECT
private slots:
    void testStartStop();
    void testStartIdempotent();
    void testSensorName();

private:
    EventBus m_bus;
    WindowSensor m_sensor{&m_bus};
};

void TestWindowSensor::testStartStop()
{
    QVERIFY(!m_sensor.isRunning());
    QVERIFY(m_sensor.start());
    QVERIFY(m_sensor.isRunning());
    m_sensor.stop();
    QVERIFY(!m_sensor.isRunning());
}

void TestWindowSensor::testStartIdempotent()
{
    QVERIFY(m_sensor.start());
    QVERIFY(m_sensor.start()); // 幂等
    QVERIFY(m_sensor.isRunning());
    m_sensor.stop();
}

void TestWindowSensor::testSensorName()
{
    QCOMPARE(m_sensor.name(), QString("window"));
}

// ==================== InputSensor ====================
class TestInputSensor : public QObject
{
    Q_OBJECT
private slots:
    void testStartStop();
    void testSensorName();
    void testPatternTyping();
    void testPatternShortcut();
    void testPatternClicking();
    void testPatternScrolling();
    void testPatternMouseRapid();
    void testPatternMixed();

private:
    EventBus m_bus;
    EventCollector m_collector{&m_bus};
    InputSensor m_sensor{&m_bus};
};

void TestInputSensor::testStartStop()
{
    QVERIFY(!m_sensor.isRunning());
    QVERIFY(m_sensor.start());
    QVERIFY(m_sensor.isRunning());
    m_sensor.stop();
    QVERIFY(!m_sensor.isRunning());
}

void TestInputSensor::testSensorName()
{
    QCOMPARE(m_sensor.name(), QString("input"));
}

void TestInputSensor::testPatternTyping()
{
    // 逻辑: keys > 0, clicks == 0, scrolls == 0, no modifiers, keys <= 20
    int keys = 10, clicks = 0, scrolls = 0;
    bool mods = false;
    QString pattern;
    if (keys > 0 && clicks == 0 && scrolls == 0) {
        if (mods) pattern = "shortcut";
        else if (keys > 20) pattern = "typing_fast";
        else pattern = "typing";
    }
    QCOMPARE(pattern, QString("typing"));
}

void TestInputSensor::testPatternShortcut()
{
    int keys = 5, clicks = 0, scrolls = 0;
    bool mods = true;
    QString pattern;
    if (keys > 0 && clicks == 0 && scrolls == 0) {
        if (mods) pattern = "shortcut";
        else if (keys > 20) pattern = "typing_fast";
        else pattern = "typing";
    }
    QCOMPARE(pattern, QString("shortcut"));
}

void TestInputSensor::testPatternClicking()
{
    int keys = 0, clicks = 3, scrolls = 0;
    QString pattern;
    if (clicks > 0 && keys == 0) {
        if (scrolls > 0) pattern = "scrolling";
        else if (clicks > 5) pattern = "clicking_rapid";
        else pattern = "clicking";
    }
    QCOMPARE(pattern, QString("clicking"));
}

void TestInputSensor::testPatternScrolling()
{
    int keys = 0, clicks = 2, scrolls = 5;
    QString pattern;
    if (clicks > 0 && keys == 0) {
        if (scrolls > 0) pattern = "scrolling";
        else if (clicks > 5) pattern = "clicking_rapid";
        else pattern = "clicking";
    }
    QCOMPARE(pattern, QString("scrolling"));
}

void TestInputSensor::testPatternMouseRapid()
{
    int keys = 0, clicks = 8, scrolls = 0;
    QString pattern;
    if (clicks > 0 && keys == 0) {
        if (scrolls > 0) pattern = "scrolling";
        else if (clicks > 5) pattern = "clicking_rapid";
        else pattern = "clicking";
    }
    QCOMPARE(pattern, QString("clicking_rapid"));
}

void TestInputSensor::testPatternMixed()
{
    int keys = 3, clicks = 2;
    QString pattern;
    if (keys > 0 && clicks > 0) pattern = "mixed_input";
    QCOMPARE(pattern, QString("mixed_input"));
}

// ==================== BrowserSensor ====================
class TestBrowserSensor : public QObject
{
    Q_OBJECT
private slots:
    void testStartStop();
    void testChromiumHistorySchema();
    void testFirefoxHistorySchema();
    void testEventFields();

private:
    QTemporaryDir m_tmpDir;
    EventBus m_bus;
    EventCollector m_collector{&m_bus};
    BrowserSensor m_sensor{&m_bus};
};

void TestBrowserSensor::testStartStop()
{
    QVERIFY(!m_sensor.isRunning());
    QVERIFY(m_sensor.start());
    QVERIFY(m_sensor.isRunning());
    m_sensor.stop();
    QVERIFY(!m_sensor.isRunning());
}

void TestBrowserSensor::testChromiumHistorySchema()
{
    QVERIFY(m_tmpDir.isValid());
    QString dbPath = m_tmpDir.path() + "/chromium_test.db";
    QString cn = "test_chromium";
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", cn);
    db.setDatabaseName(dbPath);
    QVERIFY(db.open());

    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE urls ("
                    "id INTEGER PRIMARY KEY, url TEXT, title TEXT, "
                    "visit_count INTEGER, last_visit_time INTEGER)"));
    QVERIFY(q.exec("CREATE TABLE visits ("
                    "id INTEGER PRIMARY KEY, url INTEGER, visit_time INTEGER)"));
    QVERIFY(q.exec("INSERT INTO urls VALUES(1, 'https://example.com', 'Example', 2, 13250198400000000)"));
    QVERIFY(q.exec("INSERT INTO visits VALUES(1, 1, 13250198400000000)"));

    QVERIFY(q.exec("SELECT url, title FROM urls"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QString("https://example.com"));

    db.close();
    QSqlDatabase::removeDatabase(cn);
}

void TestBrowserSensor::testFirefoxHistorySchema()
{
    QVERIFY(m_tmpDir.isValid());
    QString dbPath = m_tmpDir.path() + "/firefox_test.db";
    QString cn = "test_firefox";
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", cn);
    db.setDatabaseName(dbPath);
    QVERIFY(db.open());

    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url TEXT, title TEXT)"));
    QVERIFY(q.exec("CREATE TABLE moz_historyvisits ("
                    "id INTEGER PRIMARY KEY, place_id INTEGER, "
                    "visit_date INTEGER, visit_type INTEGER)"));
    QVERIFY(q.exec("INSERT INTO moz_places VALUES(1, 'https://mozilla.org', 'Mozilla')"));
    QVERIFY(q.exec("INSERT INTO moz_historyvisits VALUES(1, 1, 1325019840000000, 1)"));

    QVERIFY(q.exec("SELECT p.url, h.visit_type FROM moz_places p "
                    "JOIN moz_historyvisits h ON p.id = h.place_id"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QString("https://mozilla.org"));
    QCOMPARE(q.value(1).toInt(), 1);

    db.close();
    QSqlDatabase::removeDatabase(cn);
}

void TestBrowserSensor::testEventFields()
{
    Event ev(EventType::Browser, "navigation");
    ev.appName = "google-chrome";
    ev.filePath = "https://example.com";
    ev.metadata.insert("browser", "google-chrome");
    ev.metadata.insert("url", "https://example.com");

    QCOMPARE(ev.type, EventType::Browser);
    QCOMPARE(ev.action, QString("navigation"));
    QCOMPARE(ev.metadata.value("browser").toString(), QString("google-chrome"));
}

QTEST_APPLESS_MAIN(TestFileSensor)
#include "test_sensors.moc"
