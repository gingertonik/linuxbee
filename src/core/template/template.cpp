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

namespace {

// Spec section 9: every field name in the tree, in order of first appearance,
// each listed once, both sides of every argument.
void collectFields(const tmpl::Sequence &sequence, QStringList *names);

void collectFieldsInNode(const tmpl::Node &node, QStringList *names)
{
    if (node.kind == tmpl::Node::Kind::Field && !names->contains(node.text))
        names->append(node.text);
    for (const tmpl::Argument &argument : node.arguments) {
        collectFields(argument.left, names);
        collectFields(argument.right, names);
    }
}

void collectFields(const tmpl::Sequence &sequence, QStringList *names)
{
    for (const tmpl::Node &node : sequence)
        collectFieldsInNode(node, names);
}

} // namespace

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
    if (!isValid())
        return { }; // T-ERRORS
    QStringList names;
    collectFields(m_root->children, &names);
    return names;
}

QStringList Template::topLevelFields() const
{
    if (!isValid())
        return { };
    // P-MISSING: the root sequence only. Fields inside function arguments
    // are in the Call nodes' arguments, which this walk never enters.
    QStringList names;
    for (const tmpl::Node &node : m_root->children) {
        if (node.kind == tmpl::Node::Kind::Field && !names.contains(node.text))
            names.append(node.text);
    }
    return names;
}

} // namespace linuxbee
