#pragma once
#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

#include "template/template.h"

namespace linuxbee::cli {

// The fields given on a command line with --field NAME=VALUE. A name nobody
// supplied is "no such field".
class FieldSource : public TagSource {
public:
    std::optional<QString> value(const QString &fieldName) const override;
    void set(const QString &name, const QString &value);

private:
    QHash<QString, QString> m_fields;
};

// NAME is the text before the first '=', VALUE everything after it, which may
// be empty. Returns false when a specification has no '=' or an empty NAME.
bool readFields(const QStringList &specifications, FieldSource *fields);

}
