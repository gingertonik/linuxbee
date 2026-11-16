// LOCKED ACCEPTANCE TEST (the Infer panel makes its pattern ready once for a preview). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-tools-dialog.md (TT-INFER,
// "Speed"). The cost is counted, not timed.
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include "tags/tagreader.h"
#include "tagtools/capitalisepanel.h"
#include "tagtools/inferpanel.h"
#include "tagtools/removetagspanel.h"
#include "tagtools/renumberpanel.h"
#include "tagtools/searchreplacepanel.h"
#include "tagtools/tagtooldialog.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// A settings file as the next run of the program reads it. Within one run Qt
// hands back the value that was put in (a bool stays a bool); read from disk
// by a later run, every value is text. Copying the file gives the later view.
QString nextRun(QSettings &settings)
{
    static int count = 0;
    settings.sync();
    const QString copy = settings.fileName() + QStringLiteral(".run%1.ini").arg(++count);
    QFile::remove(copy);
    QFile::copy(settings.fileName(), copy);
    return copy;
}

EditTarget target(qint64 id, const char16_t *file, const char16_t *title, const char16_t *artist)
{
    EditTarget t;
    t.id = id;
    t.path = s(u"/music/album/") + s(file);
    t.tags.fields.insert(s(u"Title"), { s(title) });
    t.tags.fields.insert(s(u"Artist"), { s(artist) });
    t.tags.fields.insert(s(u"Album"), { s(u"the album") });
    return t;
}

QList<EditTarget> three()
{
    return { target(11, u"01.flac", u"hello world", u"the band"), target(12, u"02.flac", u"Second Song", u"The Band"),
        target(13, u"03.flac", u"world's end", u"the band") };
}

// A panel whose answers the test sets.
class Script : public ToolPanel {
public:
    using ToolPanel::ToolPanel;
    QString title() const override { return QStringLiteral("Scripted Tool"); }
    QString problem() const override { return trouble; }
    QList<TagChanges> changes(const QList<EditTarget> &targets) const override
    {
        ++asked;
        seen = targets;
        return maker ? maker(targets) : QList<TagChanges>(targets.size());
    }
    void load(QSettings &settings) override
    {
        ++loads;
        loaded = settings.value(QStringLiteral("script/value")).toString();
    }
    void save(QSettings &settings) const override
    {
        ++saves;
        settings.setValue(QStringLiteral("script/value"), QStringLiteral("stored"));
    }
    void changed() { emit optionsChanged(); }

    QString trouble;
    std::function<QList<TagChanges>(const QList<EditTarget> &)> maker;
    mutable int asked = 0;
    mutable int saves = 0;
    mutable QList<EditTarget> seen;
    int loads = 0;
    QString loaded;
};

// Sets Title to its upper case wherever that is a change.
QList<TagChanges> shout(const QList<EditTarget> &targets)
{
    QList<TagChanges> out;
    for (const EditTarget &t : targets) {
        TagChanges c;
        const QString now = t.tags.first(QStringLiteral("Title"));
        if (now != now.toUpper())
            c.fields.insert(QStringLiteral("Title"), { now.toUpper() });
        out.append(c);
    }
    return out;
}

struct Recorder {
    QStringList paths;
    QList<TagChanges> changes;
    QStringList failFor;
    QStringList unchangedFor;
    TagToolDialog::Saver saver()
    {
        return [this](const QString &path, const TagChanges &c) {
            paths.append(path);
            changes.append(c);
            WriteResult r;
            r.status = failFor.contains(path) ? WriteResult::Status::NotWritable
                                              : (unchangedFor.contains(path) ? WriteResult::Status::Unchanged : WriteResult::Status::Ok);
            return r;
        };
    }
};

template <typename T> T *need(QWidget &in, const char16_t *name)
{
    if (T *found = in.findChild<T *>(s(name)))
        return found;
    QTest::qFail(qPrintable(QStringLiteral("no widget named ") + s(name)), __FILE__, __LINE__);
    static T standIn;
    return &standIn;
}

QStringList row(QTableWidget *table, int r)
{
    QStringList out;
    for (int c = 0; c < table->columnCount(); ++c)
        out.append(table->item(r, c) ? table->item(r, c)->text() : QStringLiteral("<no item>"));
    return out;
}

QStringList ticked(QListWidget *list)
{
    QStringList out;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            out.append(list->item(i)->text());
    return out;
}

void tick(QListWidget *list, const QString &name, bool on)
{
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text() == name) {
            list->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            return;
        }
    QTest::qFail(qPrintable(QStringLiteral("no row ") + name), __FILE__, __LINE__);
}

QStringList offeredFields()
{
    QStringList names = tagFieldNames();
    names.removeAll(QStringLiteral("Rating"));
    return names;
}

EditTarget at(qint64 id, const QString &path)
{
    EditTarget t;
    t.id = id;
    t.path = path;
    return t;
}

bool same(const TagChanges &a, const TagChanges &b)
{
    return a.fields == b.fields && a.other == b.other && a.pictures.has_value() == b.pictures.has_value()
        && (!a.pictures.has_value() || a.pictures->size() == b.pictures->size());
}

#define NEED_PANEL(panel) \
    do { \
        if ((panel)->title().isEmpty()) \
            QFAIL("this panel has not been built"); \
    } while (false)

QList<EditTarget> many(int n)
{
    QList<EditTarget> out;
    for (int i = 0; i < n; ++i)
        out.append(at(i + 1, QStringLiteral("/music/Artist %1/Album %2/%3 - Title %4.flac").arg(i % 40).arg(i % 300).arg(i % 20 + 1).arg(i)));
    return out;
}

}

class TagToolInferSpeedTest : public QObject {
    Q_OBJECT
private slots:
    void onceForAllTracks();
};

void TagToolInferSpeedTest::onceForAllTracks()
{
    // TT-INFER, speed: a preview of any number of tracks makes the pattern ready once.
    (void)&nextRun;
    (void)&shout;
    (void)&three;
    (void)&row;
    (void)&ticked;
    (void)&tick;
    (void)&offeredFields;
    (void)&same;
    InferPanel bare;
    NEED_PANEL(&bare);
    const QList<EditTarget> tracks = many(500);
    qint64 before = inferPatternsPrepared();
    const QList<TagChanges> got = bare.changes(tracks);
    QCOMPARE(inferPatternsPrepared() - before, 1);
    QCOMPARE(got.size(), 500);
    for (int i = 0; i < 500; i += 37)
        QVERIFY(same(got.at(i), inferTags(tracks.at(i).tags, tracks.at(i).path, bare.pattern(), true)));
    QVERIFY(!got.at(0).isEmpty());

    // Through the dialog: one keystroke, one preview, and the cost does not grow with the tracks.
    qint64 forFew = 0;
    qint64 forMany = 0;
    for (const int n : { 10, 500 }) {
        auto *panel = new InferPanel;
        TagToolDialog d(panel, many(n));
        auto *pattern = need<QLineEdit>(*panel, u"patternEdit");
        before = inferPatternsPrepared();
        pattern->setText(s(u"<Album>/<Track#> - <Title>"));
        (n == 10 ? forFew : forMany) = inferPatternsPrepared() - before;
        QCOMPARE(need<QTableWidget>(d, u"previewTable")->rowCount(), 3 * n);
    }
    QVERIFY2(forFew >= 1 && forFew <= 2, qPrintable(QString::number(forFew)));
    QCOMPARE(forMany, forFew);
    // A pattern with a problem makes nothing ready.
    auto *panel = new InferPanel;
    TagToolDialog d(panel, many(50));
    before = inferPatternsPrepared();
    need<QLineEdit>(*panel, u"patternEdit")->setText(s(u"<Nonsense>"));
    QCOMPARE(inferPatternsPrepared() - before, 0);
}

QTEST_MAIN(TagToolInferSpeedTest)
#include "tst_ui_tagtools_inferspeed.moc"
