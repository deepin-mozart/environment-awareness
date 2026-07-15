#include "../src/utils/Redactor.h"
#include "../src/utils/Logger.h"

#include <QTest>
#include <QString>

using namespace Awareness;

class TestRedactor : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testPhoneRedaction();
    void testIdCardRedaction();
    void testEmailRedaction();
    void testBankCardRedaction();
    void testMultipleRedactions();
    void testNoRedaction();
    void testTruncate();
    void testAddRemoveRule();
};

void TestRedactor::initTestCase()
{
    Redactor::instance().initBuiltinRules();
}

void TestRedactor::testPhoneRedaction()
{
    const QString input = QStringLiteral("Call me at 13800138000 please");
    const QString result = Redactor::instance().redact(input);
    QVERIFY(result.contains(QStringLiteral("[PHONE]")));
    QVERIFY(!result.contains(QStringLiteral("13800138000")));
}

void TestRedactor::testIdCardRedaction()
{
    const QString input = QStringLiteral("ID: 110101199003071234");
    const QString result = Redactor::instance().redact(input);
    QVERIFY(result.contains(QStringLiteral("[ID_CARD]")));
    QVERIFY(!result.contains(QStringLiteral("110101199003071234")));
}

void TestRedactor::testEmailRedaction()
{
    const QString input = QStringLiteral("Email: user@example.com");
    const QString result = Redactor::instance().redact(input);
    QVERIFY(result.contains(QStringLiteral("[EMAIL]")));
    QVERIFY(!result.contains(QStringLiteral("user@example.com")));
}

void TestRedactor::testBankCardRedaction()
{
    const QString input = QStringLiteral("Card: 6222021234567890123");
    const QString result = Redactor::instance().redact(input);
    QVERIFY(result.contains(QStringLiteral("[BANK_CARD]")));
}

void TestRedactor::testMultipleRedactions()
{
    const QString input = QStringLiteral("Phone 13800138000, email test@deepin.org, card 6222021234567890123");
    const QString result = Redactor::instance().redact(input);
    QVERIFY(result.contains(QStringLiteral("[PHONE]")));
    QVERIFY(result.contains(QStringLiteral("[EMAIL]")));
    QVERIFY(result.contains(QStringLiteral("[BANK_CARD]")));
}

void TestRedactor::testNoRedaction()
{
    const QString input = QStringLiteral("This is a normal text without sensitive data");
    const QString result = Redactor::instance().redact(input);
    QCOMPARE(result, input);
}

void TestRedactor::testTruncate()
{
    const QString longText = QStringLiteral("abcdefghijklmnopqrstuvwxyz");
    const QString truncated = Redactor::truncate(longText, 10);
    QCOMPARE(truncated.length(), 10);
    QCOMPARE(truncated, QStringLiteral("abcdefghij"));

    const QString shortText = QStringLiteral("hello");
    QCOMPARE(Redactor::truncate(shortText, 10), shortText);
}

void TestRedactor::testAddRemoveRule()
{
    Redactor::instance().addRule(QStringLiteral("test_rule"),
                                  QStringLiteral("\\bTESTWORD\\b"),
                                  QStringLiteral("[REDACTED]"));

    const QString input = QStringLiteral("This has TESTWORD in it");
    const QString result = Redactor::instance().redact(input);
    QVERIFY(result.contains(QStringLiteral("[REDACTED]")));

    Redactor::instance().removeRule(QStringLiteral("test_rule"));
    const QString result2 = Redactor::instance().redact(input);
    QVERIFY(!result2.contains(QStringLiteral("[REDACTED]")));
}

QTEST_MAIN(TestRedactor)
#include "test_redactor.moc"
