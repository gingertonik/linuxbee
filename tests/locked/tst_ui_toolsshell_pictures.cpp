// LOCKED ACCEPTANCE TEST (the tools shell: Remove Tags sees the pictures a track has). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-TOOLS, "Pictures")
// and docs/spec/tag-tools-dialog.md (TT-REMOVE). Offscreen, with the real
// window, library and shells; a function is handed each dialog and works it.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "convert/convertdialog.h"
#include "mainwindow/mainwindow.h"
#include "organise/organisedialog.h"
#include "preferences/hotkeyspage.h"
#include "preferences/preferencesdialog.h"
#include "shell/libraryshell.h"
#include "shell/toolsshell.h"
#include "tageditor/tageditordialog.h"
#include "tagtools/capitalisepanel.h"
#include "tagtools/searchreplacepanel.h"
#include "tagtools/tagtooldialog.h"

#include <QTableWidget>
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, tone;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> lib;
    std::unique_ptr<ToolsShell> tools;
    int presented = 0;
    QString kind; // class name of the dialog last presented
    QPointer<QDialog> last;
    std::function<void(QDialog *)> work; // what "the user" does with the dialog

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        a = put("tags/basic.flac", "music/a.flac");
        b = put("tags/basic.m4a", "music/b.m4a");
        tone = put("convert/tone.flac", "music/tone.flac");
        for (const QString &p : { a, b, tone })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        lib = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        tools = std::make_unique<ToolsShell>(&window, &actions, library.get(), lib.get(), &settings, [this](QDialog *dialog) {
            ++presented;
            last = dialog;
            kind = QString::fromLatin1(dialog->metaObject()->className());
            if (work)
                work(dialog);
        });
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QLatin1Char('/') + QLatin1String(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void run(const char16_t *id)
    {
        if (QAction *action = actions.action(s(id)))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    void select(const QStringList &paths)
    {
        QList<qint64> ids;
        for (const QString &p : paths)
            if (const auto t = library->trackByPath(p))
                ids.append(t->id);
        lib->trackList()->selectTracks(ids);
    }
    QString title(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->tags.first(QStringLiteral("Title")) : QStringLiteral("NOT IN THE LIBRARY");
    }
};

void type(QDialog *dialog, const char16_t *name, const QString &text)
{
    if (auto *edit = dialog->findChild<QLineEdit *>(s(name))) {
        // Keys, as a person types: a line edit knows it was modified only then.
        edit->setFocus();
        edit->selectAll();
        if (text.isEmpty())
            QTest::keyClick(edit, Qt::Key_Delete);
        else
            QTest::keyClicks(edit, text);
    } else {
        QTest::qFail(qPrintable(QStringLiteral("no line edit named ") + s(name)), __FILE__, __LINE__);
    }
}

qint64 idOf(const Rig &r, const QString &path)
{
    const auto t = r.library->trackByPath(path);
    return t ? t->id : 0;
}

// Every id the library reported as updated, in order.
QList<qint64> updated(const QSignalSpy &spy)
{
    QList<qint64> ids;
    for (const QList<QVariant> &call : spy)
        ids += call.at(0).value<QList<qint64>>();
    return ids;
}

QByteArray png()
{
    return QByteArray::fromHex(
        "89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de"
        "0000000c49444154789c63f8cfc0000003010100c9fe92ef0000000049454e44ae426082");
}

// Gives the file `count` embedded pictures and has the library read it again.
void givePictures(Rig &r, const QString &path, int count)
{
    TagChanges give;
    QList<Picture> list;
    for (int i = 0; i < count; ++i) {
        Picture p;
        p.data = png() + QByteArray::number(i);
        p.mimeType = QStringLiteral("image/png");
        list.append(p);
    }
    give.pictures = list;
    QVERIFY(writeTags(path, give).ok());
    r.library->refresh(idOf(r, path));
}

// The preview's rows for the field "Pictures": "Now>Becomes".
QStringList pictureRows(QDialog *dialog)
{
    QStringList out;
    auto *table = dialog->findChild<QTableWidget *>(QStringLiteral("previewTable"));
    if (!table)
        return { QStringLiteral("NO PREVIEW TABLE") };
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 1) && table->item(row, 1)->text() == QLatin1String("Pictures"))
            out.append(table->item(row, 2)->text() + QLatin1Char('>') + table->item(row, 3)->text());
    return out;
}

}

class ToolsShellPicturesTest : public QObject {
    Q_OBJECT
private slots:
    void embeddedPicturesGo();
    void withAllTags();
    void theOtherToolsAndTheEditor();
};

void ToolsShellPicturesTest::embeddedPicturesGo()
{
    // TS-TOOLS: Remove Tags, "Embedded pictures", from the window: the pictures of the files that have any go.
    (void)&sorted;
    (void)&updated;
    Rig r;
    givePictures(r, r.a, 2);
    givePictures(r, r.b, 0); // whatever the fixture came with
    QCOMPARE(readTags(r.a).tags.pictures.size(), 2);
    QCOMPARE(readTags(r.b).tags.pictures.size(), 0);
    r.select({ r.a, r.b });
    QStringList rows;
    int pending = 0;
    QString asked;
    r.work = [&](QDialog *dialog) {
        auto *tool = qobject_cast<TagToolDialog *>(dialog);
        QVERIFY(tool);
        tool->setConfirm([&asked](const QString &question) {
            asked = question;
            return true;
        });
        auto *box = tool->findChild<QCheckBox *>(s(u"picturesBox"));
        QVERIFY(box);
        box->setChecked(true);
        rows = pictureRows(tool);
        for (const TagChanges &c : tool->pendingChanges())
            pending += c.isEmpty() ? 0 : 1;
        QVERIFY(tool->apply());
    };
    r.run(u"tools.tagging.removeTags");
    QCOMPARE(rows, QStringList({ s(u"2>0") })); // the one file that has pictures, and how many
    QCOMPARE(pending, 1);
    QVERIFY2(asked.startsWith(s(u"Remove the embedded pictures from 1 file?")), qPrintable(asked));
    QCOMPARE(readTags(r.a).tags.pictures.size(), 0);
    QCOMPARE(r.title(r.a), s(u"Flac Title")); // the tags stay
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 1 track"));
    // The library has read the file again: a second time there is nothing to remove.
    bool applied = true;
    r.work = [&](QDialog *dialog) {
        auto *tool = qobject_cast<TagToolDialog *>(dialog);
        QVERIFY(tool);
        tool->setConfirm([](const QString &) { return true; });
        tool->findChild<QCheckBox *>(s(u"picturesBox"))->setChecked(true);
        rows = pictureRows(tool);
        applied = tool->apply();
        tool->reject();
    };
    r.run(u"tools.tagging.removeTags");
    QCOMPARE(rows, QStringList());
    QVERIFY(!applied);
}

void ToolsShellPicturesTest::withAllTags()
{
    // TT-REMOVE: "All tags" with "Embedded pictures": both go; "All tags" alone: the pictures stay.
    Rig r;
    givePictures(r, r.a, 1);
    r.select({ r.a });
    for (const bool withPictures : { false, true }) {
        r.work = [&](QDialog *dialog) {
            auto *tool = qobject_cast<TagToolDialog *>(dialog);
            QVERIFY(tool);
            tool->setConfirm([](const QString &) { return true; });
            auto *all = tool->findChild<QCheckBox *>(s(u"allTagsBox"));
            QVERIFY(all);
            all->setChecked(true);
            tool->findChild<QCheckBox *>(s(u"picturesBox"))->setChecked(withPictures);
            if (!tool->apply())
                tool->reject(); // nothing left to change the second time but the pictures
        };
        r.run(u"tools.tagging.removeTags");
        QCOMPARE(readTags(r.a).tags.pictures.size(), withPictures ? 0 : 1);
        QVERIFY(readTags(r.a).tags.first(s(u"Title")).isEmpty());
    }
}

void ToolsShellPicturesTest::theOtherToolsAndTheEditor()
{
    // TS-TOOLS: what the shell tells a dialog about pictures is never written back: the pictures are as they were.
    Rig r;
    givePictures(r, r.a, 2);
    const QList<Picture> before = readTags(r.a).tags.pictures;
    r.select({ r.a });
    r.work = [&](QDialog *dialog) {
        auto *tool = qobject_cast<TagToolDialog *>(dialog);
        QVERIFY(tool);
        type(tool, u"findEdit", s(u"Flac Title"));
        type(tool, u"replaceEdit", s(u"Lossless"));
        QVERIFY(tool->apply());
    };
    r.run(u"tools.tagging.searchReplace");
    r.work = [&](QDialog *dialog) {
        type(dialog, u"field.Album", s(u"An Album"));
        QVERIFY(qobject_cast<TagEditorDialog *>(dialog)->save());
    };
    r.run(u"file.editTags");
    QCOMPARE(r.title(r.a), s(u"Lossless"));
    const QList<Picture> after = readTags(r.a).tags.pictures;
    QCOMPARE(after.size(), 2);
    for (int i = 0; i < 2; ++i)
        QCOMPARE(after.at(i).data, before.at(i).data);
}

QTEST_MAIN(ToolsShellPicturesTest)
#include "tst_ui_toolsshell_pictures.moc"
