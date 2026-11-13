#pragma once

#include "node.h"
#include "template.h"

namespace linuxbee::tmpl {

struct ParseResult {
    Sequence root;
    TemplateError error; // error.position == -1 when the parse succeeded
};

// Turns template text into a tree, applying T-LEX, T-QUOTE, T-NAMES and
// T-ERRORS (docs/spec/templates.md). Never throws: a fault is returned in
// `error`, whose position is the index of the '$' that begins the faulty
// call, or -1 when the text parsed.
ParseResult parseTemplate(const QString &text);

} // namespace linuxbee::tmpl
