#include "command.h"

#include <QTextStream>

#include <algorithm>
#include <cstring>

namespace linuxbee::cli {

namespace {
// A function-local static, so it exists before any Registrar runs.
std::vector<Command> &registry()
{
    static std::vector<Command> all;
    return all;
}
}

void registerCommand(const Command &command)
{
    registry().push_back(command);
}

std::vector<Command> commands()
{
    std::vector<Command> sorted = registry();
    std::sort(sorted.begin(), sorted.end(),
        [](const Command &a, const Command &b) { return std::strcmp(a.name, b.name) < 0; });
    return sorted;
}

int usageError(const QString &message)
{
    QTextStream out(stderr);
    out << message << '\n';
    out.flush();
    return 2;
}

}
