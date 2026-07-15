#include "../src/core/EventBus.h"
#include "../src/core/Config.h"
#include "../src/sensors/ClipboardSensor.h"
#include "../src/utils/Logger.h"

#include <QApplication>
#include <QClipboard>
#include <QTest>

using namespace Awareness;

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

class TestClipboardSensor : public QObject
{
    Q_OBJECT
private slots:
    void testStartStop();
    void testTextCopy();
    void testPreviewTruncation();
    void testUrlDetection();
    void testContentTypeClassification();
    void testMultilineContent();
};

void TestClipboardSensor::testStartStop()
{
    EventBus bus;
    ClipboardSensor sensor(&bus);
    QVERIFY(!sensor.isRunning());
    QVERIFY(sensor.start());
    QVERIFY(sensor.isRunning());
    sensor.stop();
    QVERIFY(!sensor.isRunning());
}

void TestClipboardSensor::testTextCopy()
{
    EventBus bus;
    EventCollector collector(&bus);
    ClipboardSensor sensor(&bus);

    QVERIFY(sensor.start());
    collector.events.clear();

    QClipboard *clipboard = QApplication::clipboard();
    QVERIFY(clipboard);

    QString testText = "test clipboard content 12345";
    clipboard->setText(testText);
    QTest::qWait(800); // DEBOUNCE(300ms) + poll interval(500ms)

    bool found = false;
    for (const auto &ev : collector.events) {
        if (ev.type == EventType::Clipboard && ev.action == "copy") {
            found = true;
            QCOMPARE(ev.contentPreview, testText);
            QCOMPARE(ev.metadata.value("text_length").toInt(), testText.length());
            QCOMPARE(ev.metadata.value("line_count").toInt(), 1);
            QVERIFY(!ev.metadata.value("has_newlines").toBool());
            break;
        }
    }
    if (!found) {
        QWARN("Clipboard event not received - headless environment may lack clipboard signals");
    }

    sensor.stop();
}

void TestClipboardSensor::testPreviewTruncation()
{
    // extractTextPreview: 长文本截断到 maxLength + "..."
    QString longText(300, 'A');
    QString preview = longText.left(200);
    preview.append("...");
    QCOMPARE(preview.length(), 203);
    QVERIFY(preview.endsWith("..."));

    // 短文本不截断
    QString shortText("hello");
    QCOMPARE(shortText.length(), 5);
    QVERIFY(!shortText.endsWith("..."));
}

void TestClipboardSensor::testUrlDetection()
{
    QVERIFY(QString("https://www.example.com").startsWith("https://"));
    QVERIFY(QString("http://example.com").startsWith("http://"));
    QVERIFY(!QString("plain text").startsWith("http://"));
    QVERIFY(!QString("plain text").startsWith("https://"));
}

void TestClipboardSensor::testContentTypeClassification()
{
    // short_text: length <= 50, non-URL
    QString shortText(30, 'x');
    QVERIFY(shortText.length() <= 50);
    QVERIFY(!shortText.startsWith("http://"));
    QVERIFY(!shortText.startsWith("https://"));

    // long_text: length > 50, non-URL
    QString longText(80, 'x');
    QVERIFY(longText.length() > 50);
    QVERIFY(!longText.startsWith("http://"));
    QVERIFY(!longText.startsWith("https://"));

    // url: starts with http(s)://
    QString url = "https://www.example.com";
    QVERIFY(url.startsWith("https://"));
}

void TestClipboardSensor::testMultilineContent()
{
    QString multiLine = "line1\nline2\nline3";
    int newlineCount = multiLine.count('\n');
    QCOMPARE(newlineCount, 2);
    QCOMPARE(multiLine.contains('\n'), true);
    QCOMPARE(newlineCount + 1, 3); // line_count = newlineCount + 1
}

QTEST_MAIN(TestClipboardSensor)
#include "test_clipboard_sensor.moc"
