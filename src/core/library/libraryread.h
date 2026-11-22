#pragma once
#include <QString>
#include <QtGlobal>

#include <utility>

namespace linuxbee {

// L-READFAIL 1: what a typed reader of the library gives. Either the value the read found (which
// may be nothing at all: an empty list, no track, 0) or the fact that the read could not be made,
// with the database's words. A read that fails part way is a failed read, not a shorter answer.
//
// It is [[nodiscard]] and has no conversion to its value, so a caller can neither drop it nor use
// it as the value unnoticed: every site asks failed() first. value() of a failed read is a fault
// of the caller and a Q_ASSERT. In a Release build the assertion is gone and value() of a failed
// read is a default-constructed T, which is the old "nothing there"; that is safe only because
// every site checks failed() before it asks. (The tag reader's own ReadResult is another thing.)
template <typename T> class [[nodiscard]] LibraryRead {
public:
    static LibraryRead ok(T value)
    {
        LibraryRead read;
        read.m_value = std::move(value);
        return read;
    }

    // `words` is never left empty, so a failed read always has something to say.
    static LibraryRead failure(const QString &words)
    {
        LibraryRead read;
        read.m_failed = true;
        read.m_words = words.isEmpty() ? QStringLiteral("the database could not be read") : words;
        return read;
    }

    bool failed() const { return m_failed; }
    // The database's words; empty when the read was made.
    const QString &words() const { return m_words; }
    const T &value() const
    {
        Q_ASSERT_X(!m_failed, "LibraryRead::value", "value() of a failed read");
        return m_value;
    }

private:
    LibraryRead() = default;

    T m_value { };
    bool m_failed = false;
    QString m_words;
};

} // namespace linuxbee
