#include "evaluator.h"

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
            // Unreachable for now: the parser reports every call it reads.
            // Task 3 gives calls a function table.
            break;
        }
    }
    return out;
}

} // namespace linuxbee::tmpl
