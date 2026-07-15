#include "Redactor.h"
#include "Logger.h"

namespace Awareness {

Redactor &Redactor::instance()
{
    static Redactor inst;
    return inst;
}

void Redactor::initBuiltinRules()
{
    addRule(QStringLiteral("phone"),
            QStringLiteral(R"(\b1[3-9]\d{9}\b)"),
            QStringLiteral("[PHONE]"));
    addRule(QStringLiteral("id_card"),
            QStringLiteral(R"(\b[1-9]\d{16}[0-9Xx]\b)"),
            QStringLiteral("[ID_CARD]"));
    addRule(QStringLiteral("email"),
            QStringLiteral(R"(\b[\w.-]+@[\w.-]+\.\w+\b)"),
            QStringLiteral("[EMAIL]"));
    addRule(QStringLiteral("bank_card"),
            QStringLiteral(R"(\b\d{16,19}\b)"),
            QStringLiteral("[BANK_CARD]"));
    addRule(QStringLiteral("secret"),
            // 匹配常见 token/key 格式：32+位连续十六进制或 base64
            QStringLiteral(R"(\b[0-9a-fA-F]{32,}\b|[A-Za-z0-9+/]{40,}={0,2})"),
            QStringLiteral("[SECRET]"));

    awLogInfo() << "Builtin redaction rules initialized:" << m_rules.size();
}

void Redactor::addRule(const QString &name, const QString &pattern, const QString &replacement, bool enabled)
{
    QRegularExpression regex(pattern);
    if (!regex.isValid()) {
        awLogWarning() << "Invalid regex for rule" << name << ":" << regex.errorString();
        return;
    }
    // name 相同时覆盖
    for (auto &r : m_rules) {
        if (r.name == name) {
            r.pattern = regex;
            r.replacement = replacement;
            r.enabled = enabled;
            return;
        }
    }
    m_rules.append({name, regex, replacement, enabled});
}

void Redactor::removeRule(const QString &name)
{
    for (int i = m_rules.size() - 1; i >= 0; --i) {
        if (m_rules[i].name == name)
            m_rules.removeAt(i);
    }
}
const QList<Redactor::Rule> &Redactor::rules() const
{
    return m_rules;
}

QString Redactor::redact(const QString &text) const
{
    QString result = text;
    for (const auto &r : m_rules) {
        if (r.enabled) {
            result.replace(r.pattern, r.replacement);
        }
    }
    return result;
}

QString Redactor::truncate(const QString &text, int maxLen)
{
    if (text.length() <= maxLen)
        return text;
    return text.left(maxLen);
}

} // namespace Awareness
