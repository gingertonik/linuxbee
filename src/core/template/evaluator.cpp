#include "evaluator.h"

#include "functions.h"
#include "node.h"
#include "template.h"

#include <optional>

namespace linuxbee::tmpl {
namespace {

// T-UNKNOWN and the T-BLANK exception.
QString fieldText(const QString &name, const EvalContext &context, bool topLevel)
{
    const std::optional<QString> value = context.tags.value(name);
    if (!value.has_value()) {
        QString out;
        out += QLatin1Char('<');
        out += name;
        out += QLatin1Char('>');
        return out;
    }
    if (value->isEmpty() && topLevel && context.options.unknownForBlank)
        return QStringLiteral("Unknown ") + name;
    return *value;
}

} // namespace

QString evaluateSequence(const Sequence &sequence, const EvalContext &context, bool topLevel)
{
    QString out;
    for (const Node &node : sequence) {
        switch (node.kind) {
        case Node::Kind::Literal:
            out += node.text;
            break;
        case Node::Kind::Field:
            out += fieldText(node.text, context, topLevel);
            break;
        case Node::Kind::Call:
            out += functionAt(node.function).evaluate(node.arguments, context);
            break;
        }
    }
    return out;
}

// T-COMPARE.
bool evaluateCondition(const Argument &argument, const EvalContext &context)
{
    const QString left = evaluateSequence(argument.left, context, false);
    if (argument.op.isNull())
        return left == QLatin1String("T"); // a condition with no operator
    const QString right = evaluateSequence(argument.right, context, false);
    const std::optional<double> leftNumber = toNumber(left);
    const std::optional<double> rightNumber = toNumber(right);
    if (leftNumber.has_value() && rightNumber.has_value()) {
        // Both sides are numbers: compare them as numbers.
        const bool isLess = *leftNumber < *rightNumber;
        const bool isGreater = *leftNumber > *rightNumber;
        if (argument.op == u'=')
            return !isLess && !isGreater;
        if (argument.op == u'<')
            return isLess;
        return isGreater; // argument.op == u'>'
    }
    if (argument.op == u'=')
        return left.compare(right, Qt::CaseInsensitive) == 0;
    return false; // T-COMPARE: '<' and '>' on text are false
}

} // namespace linuxbee::tmpl
