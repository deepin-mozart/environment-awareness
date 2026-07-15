#pragma once

#include <QString>
#include <QRegularExpression>
#include <QList>
#include <QJsonObject>

namespace Awareness {

/**
 * @brief 脱敏引擎，对文本内容应用正则替换规则
 *
 * 内置规则：手机号/身份证/邮箱/银行卡/密钥
 * 支持从 redaction_rules 表加载自定义规则。
 */
class Redactor
{
public:
    struct Rule {
        QString name;
        QRegularExpression pattern;
        QString replacement;
        bool enabled = true;
    };

    static Redactor &instance();

    /// 初始化内置脱敏规则
    void initBuiltinRules();

    /// 添加/更新规则（name 相同时覆盖）
    void addRule(const QString &name, const QString &pattern, const QString &replacement, bool enabled = true);

    /// 移除规则
    void removeRule(const QString &name);

    /// 获取所有规则
    const QList<Rule> &rules() const;

    /// 对文本应用所有 enabled 规则
    QString redact(const QString &text) const;

    /// 截断到指定长度
    static QString truncate(const QString &text, int maxLen);

private:
    Redactor() = default;
    QList<Rule> m_rules;
};

} // namespace Awareness
