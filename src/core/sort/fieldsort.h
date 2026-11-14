#pragma once
#include <QString>
#include <QStringList>

// How values are put in order in a track list, and the one-line summary
// under it. Contract: docs/spec/track-list.md (T-KIND, T-COMPARE, T-SUMMARY).
// Pure, and the same on every machine: no locale is consulted.
namespace linuxbee {

enum class FieldKind { Text, Number, Duration, Size };

// T-KIND
FieldKind fieldKind(const QString &fieldName);

struct SortOptions {
    QStringList ignoreWords = { QStringLiteral("The") };
};

// T-COMPARE: below 0 when a comes first, above 0 when b does, 0 when they tie.
int compareFieldValues(
    const QString &fieldName, const QString &a, const QString &b, const SortOptions &options = { });

// T-PREPARED: a value made ready for comparing, once. Only `blank` is for
// callers to read; the rest is what comparePrepared needs.
struct PreparedValue {
    bool blank = true; // the value, trimmed, is empty
    FieldKind kind = FieldKind::Text;
    QString text; // Text: folded, trimmed, without an ignore word
    bool hasNumber = false; // the other kinds: whether a number was read
    double number = 0;
};
PreparedValue prepareFieldValue(
    const QString &fieldName, const QString &value, const SortOptions &options = { });
// The sign of compareFieldValues for the two values, prepared for one field.
int comparePrepared(const PreparedValue &a, const PreparedValue &b);
// For tests and timing, for the whole process: calls of prepareFieldValue,
// and calls of compareFieldValues.
qint64 fieldValuesPrepared();
qint64 fieldValueComparisons();

// T-SUMMARY: "1,234 tracks, 5.6 GB, 3d 04:05:06"
QString formatTrackSummary(int count, qint64 totalBytes, qint64 totalMs);

}
