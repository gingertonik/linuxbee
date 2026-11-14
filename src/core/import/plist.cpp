#include "plist.h"

#include <QXmlStreamReader>

namespace linuxbee::plist {

namespace {

// Containers may nest this deep and no deeper, so a hostile file cannot
// exhaust the stack.
constexpr int kMaxDepth = 64;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// The reader's own message when it has one, else the fallback.
bool failReader(QXmlStreamReader &r, QString *error, const QString &fallback)
{
    return fail(error, r.hasError() ? r.errorString() : fallback);
}

// Moves to the next start or end element, skipping text, comments and
// processing instructions. False at the end of the document or on an error.
bool nextElement(QXmlStreamReader &r)
{
    for (;;) {
        const QXmlStreamReader::TokenType token = r.readNext();
        if (token == QXmlStreamReader::StartElement || token == QXmlStreamReader::EndElement)
            return true;
        if (token == QXmlStreamReader::Invalid || token == QXmlStreamReader::EndDocument
            || r.hasError())
            return false;
    }
}

bool parseValue(QXmlStreamReader &r, int depth, Value *out, QString *error);

// The reader is on a start element. Reads its text and moves past its end tag.
bool readText(QXmlStreamReader &r, QString *text, QString *error)
{
    *text = r.readElementText();
    return r.hasError() ? failReader(r, error, QString()) : true;
}

bool parseContainer(QXmlStreamReader &r, Value::Type type, int depth, Value *out, QString *error)
{
    if (depth >= kMaxDepth)
        return fail(error, QStringLiteral("property list nests deeper than %1").arg(kMaxDepth));
    out->type = type;
    for (;;) {
        if (!nextElement(r))
            return failReader(r, error,
                QStringLiteral("property list ends inside a %1")
                    .arg(type == Value::Type::Dict ? QStringLiteral("dict")
                                                   : QStringLiteral("array")));
        if (r.isEndElement())
            return true;
        if (type == Value::Type::Array) {
            Value child;
            if (!parseValue(r, depth + 1, &child, error))
                return false;
            out->array.append(child);
            continue;
        }
        // A dict is a key, then its value, in turn.
        if (r.name().toString() != QLatin1String("key"))
            return fail(error,
                QStringLiteral("a dict holds <%1> where a key belongs").arg(r.name().toString()));
        QString key;
        if (!readText(r, &key, error))
            return false;
        if (!nextElement(r) || r.isEndElement())
            return failReader(r, error, QStringLiteral("key \"%1\" has no value").arg(key));
        Value child;
        if (!parseValue(r, depth + 1, &child, error))
            return false;
        out->dict.append({ key, child });
    }
}

// The reader is on a start element holding one value.
bool parseValue(QXmlStreamReader &r, int depth, Value *out, QString *error)
{
    const QString name = r.name().toString();
    if (name == QLatin1String("dict"))
        return parseContainer(r, Value::Type::Dict, depth, out, error);
    if (name == QLatin1String("array"))
        return parseContainer(r, Value::Type::Array, depth, out, error);

    QString text;
    if (!readText(r, &text, error))
        return false;
    if (name == QLatin1String("string")) {
        out->type = Value::Type::String;
        out->text = text;
    } else if (name == QLatin1String("integer")) {
        bool ok = false;
        out->integer = text.trimmed().toLongLong(&ok);
        if (!ok)
            return fail(error, QStringLiteral("\"%1\" is not an integer").arg(text));
        out->type = Value::Type::Integer;
    } else if (name == QLatin1String("real")) {
        bool ok = false;
        text.trimmed().toDouble(&ok);
        if (!ok)
            return fail(error, QStringLiteral("\"%1\" is not a real number").arg(text));
        out->type = Value::Type::Real;
        out->text = text.trimmed();
    } else if (name == QLatin1String("date")) {
        const QDateTime when = QDateTime::fromString(text.trimmed(), Qt::ISODate);
        if (!when.isValid())
            return fail(error, QStringLiteral("\"%1\" is not a date").arg(text));
        out->type = Value::Type::Date;
        out->date = when.toUTC();
    } else if (name == QLatin1String("data")) {
        out->type = Value::Type::Data;
        out->text = text;
    } else if (name == QLatin1String("true") || name == QLatin1String("false")) {
        out->type = Value::Type::Bool;
        out->boolean = name == QLatin1String("true");
    } else {
        return fail(error, QStringLiteral("unknown element <%1>").arg(name));
    }
    return true;
}

}

const Value *Value::find(const QString &key) const
{
    if (type != Type::Dict)
        return nullptr;
    for (const auto &entry : dict) {
        if (entry.first == key)
            return &entry.second;
    }
    return nullptr;
}

bool parse(const QByteArray &xml, Value *root, QString *error)
{
    QXmlStreamReader r(xml);
    if (!nextElement(r))
        return failReader(r, error, QStringLiteral("no document element"));
    if (!r.isStartElement() || r.name().toString() != QLatin1String("plist"))
        return fail(error, QStringLiteral("the root element is not plist"));
    if (!nextElement(r) || !r.isStartElement())
        return failReader(r, error, QStringLiteral("plist holds no value"));

    Value value;
    if (!parseValue(r, 0, &value, error))
        return false;

    // Whatever follows the value must be well formed up to the end of the document.
    for (;;) {
        const QXmlStreamReader::TokenType token = r.readNext();
        if (token == QXmlStreamReader::EndDocument)
            break;
        if (token == QXmlStreamReader::Invalid || r.hasError())
            return failReader(r, error, QStringLiteral("not a well-formed property list"));
    }
    *root = value;
    return true;
}

}
