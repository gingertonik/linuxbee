#include "parser.h"

#include "node.h"
#include "template.h"

#include <utility>

namespace linuxbee::tmpl {
namespace {

bool isAsciiLetter(QChar ch)
{
    return (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z');
}

bool isLineBreak(QChar ch)
{
    return ch == u'\n' || ch == u'\r';
}

// Scans template text once, left to right. Every fault stops the parse: only
// the first one is kept (T-ERRORS).
class Parser {
public:
    explicit Parser(const QString &text)
        : m_text(text)
    {
    }

    ParseResult run();

private:
    // T-LEX: does a field start at `position`? On success *end is the index
    // just after the '>' and *name the text between the brackets.
    bool fieldAt(qsizetype position, qsizetype *end, QString *name) const;
    // T-LEX: does a call start at `position` ('$', letters, '(')?
    bool isCallStart(qsizetype position) const;
    void appendLiteral(Sequence *nodes, QChar ch);
    void fail(qsizetype position, const QString &message);

    Sequence parseTopLevel();

    const QString &m_text;
    qsizetype m_position = 0;
    bool m_failed = false;
    TemplateError m_error;
};

bool Parser::fieldAt(qsizetype position, qsizetype *end, QString *name) const
{
    for (qsizetype probe = position + 1; probe < m_text.size(); ++probe) {
        const QChar ch = m_text.at(probe);
        if (ch == u'>') {
            if (probe == position + 1)
                return false; // "<>" is literal
            *end = probe + 1;
            *name = m_text.mid(position + 1, probe - position - 1);
            return true;
        }
        if (ch == u'<' || ch == u',' || isLineBreak(ch))
            return false;
    }
    return false; // no '>' at all
}

bool Parser::isCallStart(qsizetype position) const
{
    if (m_text.at(position) != u'$')
        return false;
    qsizetype probe = position + 1;
    while (probe < m_text.size() && isAsciiLetter(m_text.at(probe)))
        ++probe;
    if (probe == position + 1)
        return false; // no letters
    return probe < m_text.size() && m_text.at(probe) == u'(';
}

void Parser::appendLiteral(Sequence *nodes, QChar ch)
{
    if (!nodes->empty()) {
        Node &last = nodes->back();
        if (last.kind == Node::Kind::Literal && !last.quoted) {
            last.text.append(ch);
            return;
        }
    }
    Node node;
    node.kind = Node::Kind::Literal;
    node.text = QString(ch);
    nodes->push_back(std::move(node));
}

void Parser::fail(qsizetype position, const QString &message)
{
    if (m_failed)
        return;
    m_failed = true;
    m_error.position = int(position);
    m_error.message = message;
}

Sequence Parser::parseTopLevel()
{
    Sequence nodes;
    while (m_position < m_text.size()) {
        if (m_failed)
            return nodes;
        const QChar ch = m_text.at(m_position);
        if (ch == u'<') {
            qsizetype end = 0;
            QString name;
            if (fieldAt(m_position, &end, &name)) {
                Node node;
                node.kind = Node::Kind::Field;
                node.text = name;
                nodes.push_back(std::move(node));
                m_position = end;
                continue;
            }
            appendLiteral(&nodes, ch);
            ++m_position;
            continue;
        }
        if (ch == u'$') {
            if (isCallStart(m_position)) {
                // Task 3 gives the parser a function table.
                fail(m_position, QStringLiteral("unknown function"));
                return nodes;
            }
            appendLiteral(&nodes, ch);
            ++m_position;
            continue;
        }
        appendLiteral(&nodes, ch);
        ++m_position;
    }
    return nodes;
}

ParseResult Parser::run()
{
    ParseResult result;
    result.root = parseTopLevel();
    result.error = m_error;
    if (!m_failed)
        result.error.position = -1;
    return result;
}

} // namespace

ParseResult parseTemplate(const QString &text)
{
    Parser parser(text);
    return parser.run();
}

} // namespace linuxbee::tmpl
