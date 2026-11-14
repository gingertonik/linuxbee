#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>

// A small reader for Apple property lists in XML form. Private to the import;
// nothing outside src/core/import/ includes it.
namespace linuxbee::plist {

struct Value {
    enum class Type { None, Dict, Array, String, Integer, Real, Date, Bool, Data };
    Type type = Type::None;
    QString text; // String, Real, Data
    qint64 integer = 0;
    bool boolean = false;
    QDateTime date; // UTC
    QList<QPair<QString, Value>> dict; // in file order
    QList<Value> array;
    const Value *find(const QString &key) const; // Dict: first entry with this key
};

// False with *error set when the bytes are not a well-formed property list.
// On success *root is the value inside the plist element.
bool parse(const QByteArray &xml, Value *root, QString *error);

}
