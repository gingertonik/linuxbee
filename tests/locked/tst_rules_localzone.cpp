// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// Proposed locked test (plan 06 review): U-DATE "a date and time without a
// zone is taken as UTC" must not depend on the machine's time zone. On
// 2026-03-08 New York skips 02:00-03:00 local, so a reader that goes through
// local time moves 02:30 to 03:30.
#include <QtTest>

#include <ctime>

#include "rules/rules.h"

namespace {
class MapSource : public linuxbee::TagSource {
public:
    std::optional<QString> value(const QString &fieldName) const override
    {
        const auto it = m_values.constFind(fieldName);
        if (it == m_values.constEnd())
            return std::nullopt;
        return *it;
    }
    QHash<QString, QString> m_values;
};
}

class RulesLocalZoneTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qputenv("TZ", "America/New_York");
        tzset();
    }
    void noZoneIsUtcEvenInALocalGap()
    {
        linuxbee::Rule rule;
        rule.field = QStringLiteral("Last Played");
        rule.op = linuxbee::RuleOperator::InTheLast;
        rule.values = { QStringLiteral("1"), QStringLiteral("days") };
        linuxbee::RuleSet set;
        set.rules = { rule };
        MapSource t;
        t.m_values.insert(QStringLiteral("Last Played"), QStringLiteral("2026-03-08T02:30:00"));
        linuxbee::RuleContext c;
        // 02:30 UTC is 45 minutes before now, so it is in the last day.
        c.now = QDateTime(QDate(2026, 3, 8), QTime(3, 15), QTimeZone::UTC);
        QVERIFY(linuxbee::matchesRules(set, t, c));
    }
};

QTEST_GUILESS_MAIN(RulesLocalZoneTest)
#include "tst_rules_localzone.moc"
