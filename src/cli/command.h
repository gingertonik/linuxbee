#pragma once
#include <QString>
#include <QStringList>

#include <vector>

// How linuxbeectl finds its commands.
//
// Each command lives in its own file, cmd_<name>.cpp, and registers itself:
//
//     int runThing(const QStringList &arguments) { ... }
//     LINUXBEE_COMMAND(thing, "thing", "One line saying what it does.", runThing)
//
// `arguments` is everything after the command name. The function returns the
// process exit code: 0 for success, 2 for a usage mistake. main.cpp never
// needs to know a command exists.
namespace linuxbee::cli {

using CommandFunction = int (*)(const QStringList &arguments);

struct Command {
    const char *name;
    const char *summary;
    CommandFunction run;
};

void registerCommand(const Command &command);

// Every registered command, sorted by name.
std::vector<Command> commands();

// Prints the message on standard error and returns 2.
int usageError(const QString &message);

struct Registrar {
    explicit Registrar(const Command &command) { registerCommand(command); }
};

}

#define LINUXBEE_COMMAND(identifier, name, summary, function)                                      \
    static const ::linuxbee::cli::Registrar linuxbeeCommand_##identifier {                         \
        ::linuxbee::cli::Command { name, summary, function }                                       \
    };
