#pragma once
#include <QString>

#include <vector>

namespace linuxbee::tmpl {

struct Node;
using Sequence = std::vector<Node>;

// One argument of a call. For a condition, `op` is '=', '<' or '>' and the
// text is split into left and right. Otherwise `op` is null and everything
// is in `left`.
struct Argument {
    Sequence left;
    QChar op;
    Sequence right;
};

struct Node {
    enum class Kind { Literal, Field, Call };
    Kind kind = Kind::Literal;
    QString text; // Literal: the text. Field: the name without < >
    bool quoted = false; // Literal only: came from "..." inside an argument
    int function = -1; // Call only: index into the function table
    int position = 0; // Call only: index of its '$' in the template text
    std::vector<Argument> arguments;
};

} // namespace linuxbee::tmpl
