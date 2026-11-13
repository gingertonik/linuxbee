#pragma once
#include <QLatin1String>
#include <QString>
#include <QStringView>

#include <optional>
#include <vector>

#include "evaluator.h"
#include "node.h"

namespace linuxbee::tmpl {

// One row of the function table (docs/spec/templates.md, section 4).
struct FunctionSpec {
    QLatin1StringView name;
    int arity;
    // How many leading arguments are conditions. -1 means all of them.
    int conditionArguments;
    QString (*evaluate)(const std::vector<Argument> &arguments, const EvalContext &context);
};

// Case-insensitive (T-NAMES). -1 when the name is not in the table.
int findFunction(QStringView name);
const FunctionSpec &functionAt(int index);

// Spec section 5. nullopt when the text is not a number / not a valid count.
std::optional<double> toNumber(const QString &text);
std::optional<int> toCount(const QString &text);
QString formatNumber(double value);

} // namespace linuxbee::tmpl
