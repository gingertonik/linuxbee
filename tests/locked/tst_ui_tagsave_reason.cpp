// LOCKED ACCEPTANCE TEST (the tag editor and the tools say why a file was not saved). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. Expected values: docs/spec/tag-editor.md (E-SAVE step 4), tag-tools-dialog.md
// (TT-APPLY step 4). The helpers are those of tst_ui_tagtools.cpp; the savers are the test's own.
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include "tags/tagreader.h"
#include "tagtools/capitalisepanel.h"
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
    return { target(11, u"01.flac", u"hello world", u"the band"),
        target(12, u"02.flac", u"Second Song", u"The Band"),
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
            r.status = failFor.contains(path)
                ? WriteResult::Status::NotWritable
                : (unchangedFor.contains(path) ? WriteResult::Status::Unchanged
                                               : WriteResult::Status::Ok);
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

// What a save answers when the file has another name (WS-LINK), as writeTags words it.
WriteResult linked()
{
    WriteResult r;
    r.status = WriteResult::Status::HardLinked;
    r.problems = { s(u"the file has 2 names (hard links)") };
    return r;
}

WriteResult changed()
{
    WriteResult r;
    r.status = WriteResult::Status::ChangedOnDisk;
    r.problems = { s(u"the file was changed by another program") };
    return r;
}

WriteResult plain(WriteResult::Status status)
{
    WriteResult r;
    r.status = status;
    return r;
}

}

class TagSaveReasonTest : public QObject {
    Q_OBJECT
private slots:
    void theEditorSaysWhy();
    void theToolsSayWhy();
};

void TagSaveReasonTest::theEditorSaysWhy()
{
    // E-SAVE: a file that was refused for a reason the user can act on is named with the reason.
    (void)&nextRun;
    (void)&row;
    (void)&ticked;
    (void)&tick;
    (void)&offeredFields;
    (void)&shout;
    TagEditorDialog d(three());
    d.setSaver([](const QString &path, const TagChanges &) {
        if (path.endsWith(s(u"01.flac")))
            return linked();
        if (path.endsWith(s(u"02.flac")))
            return plain(WriteResult::Status::NotWritable);
        return changed();
    });
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QLineEdit *album = need<QLineEdit>(d, u"field.Album");
    album->setFocus();
    album->selectAll();
    QTest::keyClicks(album, s(u"Post"));
    QVERIFY(!d.save());
    QVERIFY(d.isVisible());
    QCOMPARE(need<QLabel>(d, u"problemLabel")->text(),
        s(u"Could not save 3 of 3: 01.flac (the file has 2 names (hard links)), 02.flac, 03.flac "
          u"(the file was changed by another program)"));
}

void TagSaveReasonTest::theToolsSayWhy()
{
    // TT-APPLY, step 4: the same in the tools' dialog.
    auto *panel = new Script;
    panel->maker = shout;
    TagToolDialog d(panel, three(), nullptr);
    d.setSaver([](const QString &path, const TagChanges &) {
        if (path.endsWith(s(u"02.flac")))
            return linked();
        return plain(WriteResult::Status::Ok);
    });
    d.show();
    QVERIFY(!d.apply());
    QVERIFY(d.isVisible());
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(),
        s(u"Could not save 1 of 3: 02.flac (the file has 2 names (hard links))"));
}

QTEST_MAIN(TagSaveReasonTest)
#include "tst_ui_tagsave_reason.moc"
