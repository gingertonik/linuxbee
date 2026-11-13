#include "template.h"

#include "evaluator.h"
#include "node.h"
#include "parser.h"

#include <memory>
#include <utility>

namespace linuxbee {

struct Template::Node {
    tmpl::Sequence children;
};

Template Template::parse(const QString &text)
{
    tmpl::ParseResult parsed = tmpl::parseTemplate(text);
    Template t;
    t.m_error = parsed.error;
    if (parsed.error.position < 0) {
        Node root;
        root.children = std::move(parsed.root);
        t.m_root = std::make_shared<Node>(std::move(root));
    }
    return t;
}

bool Template::isValid() const
{
    return m_error.position < 0 && m_root != nullptr;
}

TemplateError Template::error() const
{
    return m_error;
}

QString Template::evaluate(const TagSource &tags, const TemplateOptions &options) const
{
    if (!isValid())
        return { };
    const tmpl::EvalContext context { tags, options };
    return tmpl::evaluateSequence(m_root->children, context, true);
}

QStringList Template::referencedFields() const
{
    return { };
}

} // namespace linuxbee
