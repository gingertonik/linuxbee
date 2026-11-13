#include "parser.h"

#include "functions.h"
#include "node.h"
#include "template.h"

#include <utility>
#include <vector>

namespace linuxbee::tmpl {
namespace {

// T-ERRORS: a call nested deeper than this is a parse error.
constexpr int kMaxCallDepth = 256;

bool isAsciiLetter(QChar ch)
{
    return (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z');
}

bool isLineBreak(QChar ch)
{
    return ch == u'\n' || ch == u'\r';
}

// T-QUOTE: white space at the very start and very end of an argument is
// removed, and only where that edge is an unquoted Literal node.
void trimSequenceEdges(Sequence *nodes)
{
    if (nodes->empty())
        return;
    Node &first = nodes->front();
    if (first.kind == Node::Kind::Literal && !first.quoted) {
        qsizetype spaces = 0;
        while (spaces < first.text.size() && first.text.at(spaces).isSpace())
            ++spaces;
        first.text = first.text.mid(spaces);
    }
    Node &last = nodes->back();
    if (last.kind == Node::Kind::Literal && !last.quoted) {
        qsizetype spaces = 0;
        while (spaces < last.text.size() && last.text.at(last.text.size() - 1 - spaces).isSpace())
            ++spaces;
        last.text.chop(spaces);
    }
}

// Scans template text once, left to right, into a tree. Every fault stops the
// parse and only the first one is kept (T-ERRORS).
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

    // Reads nodes until an unquoted ',' (end of argument), an unquoted ')' at
    // parenthesis depth 0 (end of call), an operator when stopAtOperators is
    // set, or the end of the text. m_stop says which ended the sequence; it is
    // null when the text ran out.
    Sequence parseSequence(int depth, bool top, bool stopAtOperators, QChar *op);
    bool parseQuoted(Sequence *nodes);
    bool parseCall(int depth, Node *call);

    // Lets OpenCallScope save and restore m_openCallDollar.
    friend class OpenCallScope;

    const QString &m_text;
    qsizetype m_position = 0;
    QChar m_stop;
    // '$' of the innermost call that is open, so that an unclosed call or an
    // unclosed quote is reported there (T-ERRORS).
    qsizetype m_openCallDollar = -1;
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

// Marks a call as open for as long as parseCall is reading its arguments, and
// restores the previous marker on every exit path.
class OpenCallScope {
public:
    OpenCallScope(Parser *parser, qsizetype dollar)
        : m_parser(parser)
        , m_saved(parser->m_openCallDollar)
    {
        m_parser->m_openCallDollar = dollar;
    }
    ~OpenCallScope() { m_parser->m_openCallDollar = m_saved; }

private:
    Parser *m_parser;
    qsizetype m_saved;
};

bool Parser::parseQuoted(Sequence *nodes)
{
    const qsizetype start = m_position + 1;
    const qsizetype close = m_text.indexOf(u'"', start);
    if (close < 0) {
        // T-QUOTE: there is no escape for a '"'.
        fail(m_openCallDollar, QStringLiteral("the quote is never closed"));
        return false;
    }
    Node node;
    node.kind = Node::Kind::Literal;
    node.text = m_text.mid(start, close - start);
    node.quoted = true;
    nodes->push_back(std::move(node));
    m_position = close + 1;
    return true;
}

bool Parser::parseCall(int depth, Node *call)
{
    const qsizetype dollar = m_position;
    if (depth + 1 > kMaxCallDepth) {
        fail(dollar, QStringLiteral("calls are nested too deeply"));
        return false;
    }
    qsizetype nameEnd = dollar + 1;
    while (nameEnd < m_text.size() && isAsciiLetter(m_text.at(nameEnd)))
        ++nameEnd;
    const QStringView name = QStringView(m_text).mid(dollar + 1, nameEnd - dollar - 1);
    const int function = findFunction(name);
    if (function < 0) {
        // T-NAMES
        fail(dollar, QStringLiteral("unknown function \"%1\"").arg(name.toString()));
        return false;
    }
    const FunctionSpec &spec = functionAt(function);
    m_position = nameEnd + 1; // past the '('
    OpenCallScope scope(this, dollar);

    std::vector<Argument> arguments;
    while (true) {
        if (m_failed)
            return false;
        Argument argument;
        const bool isCondition
            = spec.conditionArguments < 0 || int(arguments.size()) < spec.conditionArguments;
        QChar split;
        argument.left = parseSequence(depth + 1, false, isCondition, &split);
        if (m_failed)
            return false;
        trimSequenceEdges(&argument.left);
        if (!split.isNull()) {
            argument.op = split;
            argument.right = parseSequence(depth + 1, false, false, nullptr);
            if (m_failed)
                return false;
            trimSequenceEdges(&argument.right);
        }
        const QChar stop = m_stop;
        arguments.push_back(std::move(argument));
        if (stop == u')')
            break; // the call is closed
        if (stop != u',') {
            // The text ran out while this call was open.
            fail(dollar, QStringLiteral("the call is never closed"));
            return false;
        }
    }
    if (int(arguments.size()) != spec.arity) {
        fail(dollar,
            QStringLiteral("\"%1\" takes %2 arguments, this call has %3")
                .arg(QString::fromLatin1(spec.name))
                .arg(spec.arity)
                .arg(int(arguments.size())));
        return false;
    }
    call->kind = Node::Kind::Call;
    call->function = function;
    call->position = int(dollar);
    call->arguments = std::move(arguments);
    return true;
}

Sequence Parser::parseSequence(int depth, bool top, bool stopAtOperators, QChar *op)
{
    Sequence nodes;
    m_stop = QChar();
    qsizetype parens = 0;
    while (m_position < m_text.size()) {
        if (m_failed)
            return nodes;
        const QChar ch = m_text.at(m_position);
        if (!top) {
            if (ch == u')') {
                if (parens == 0) {
                    m_stop = ch;
                    ++m_position;
                    return nodes;
                }
                --parens; // T-QUOTE: a ')' that balances a bare '(' is literal
                appendLiteral(&nodes, ch);
                ++m_position;
                continue;
            }
            if (ch == u'(') {
                ++parens; // T-QUOTE: a bare '(' is literal
                appendLiteral(&nodes, ch);
                ++m_position;
                continue;
            }
            if (ch == u',' && parens == 0) {
                m_stop = ch;
                ++m_position;
                return nodes;
            }
            if (stopAtOperators && parens == 0 && (ch == u'=' || ch == u'>' || ch == u'<')) {
                // T-COMPARE: the first such operator that is not the start of a field.
                qsizetype fieldEnd = 0;
                QString fieldName;
                const bool fieldStart = (ch == u'<') && fieldAt(m_position, &fieldEnd, &fieldName);
                if (!fieldStart) {
                    if (op != nullptr)
                        *op = ch;
                    ++m_position;
                    m_stop = ch;
                    return nodes;
                }
            }
        }
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
                Node call;
                if (!parseCall(depth, &call))
                    return nodes;
                nodes.push_back(std::move(call));
                continue;
            }
            appendLiteral(&nodes, ch);
            ++m_position;
            continue;
        }
        if (ch == u'"' && !top) {
            if (!parseQuoted(&nodes))
                return nodes;
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
    result.root = parseSequence(0, true, false, nullptr);
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
