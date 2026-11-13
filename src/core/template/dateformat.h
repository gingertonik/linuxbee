#pragma once
#include <QString>

namespace linuxbee::tmpl {

// Spec section 7 (T-DATE). Empty string when `value` is not a date.
// `format` uses the token table of the spec, scanned longest token first.
QString formatDate(const QString &value, const QString &format);

} // namespace linuxbee::tmpl
