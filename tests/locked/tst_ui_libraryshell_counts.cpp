// LOCKED ACCEPTANCE TEST (counting without reading). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-x). Runs on the
// offscreen platform with a real library (in memory), a real playlist folder
// (in a temporary directory) and the real window parts.
#include <QFile>
#include <QAction>
#include <QLineEdit>
#include <QSettings>
#include <QLabel>
#include <QPointer>
#include <QSignalSpy>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "shell/libraryshell.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

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

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(name);
}

// Stops a test, as a failure, when the shell has not built its parts.
#define NEED_PARTS(r) \
    do { \
        if (!(r).shell->navigator() || !(r).shell->model() || !(r).shell->trackList()) \
            QFAIL("the shell has not built its navigator, model and track list"); \
    } while (false)

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, c, d, junk;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QStringList folders; // what the folder chooser will answer
    QStringList files; // what the file chooser will answer
    int asked = 0;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        made(QStringLiteral("inbox"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.mp3", "music/b.mp3");
        c = put("basic.ogg", "music/c.ogg");
        junk = put("notaudio.mp3", "music/junk.mp3");
        d = put("basic.flac", "inbox/d.flac");
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(fixture(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void fill() // three in Music, one in the Inbox
    {
        for (const QString &p : { a, b, c })
            QVERIFY(library->addFile(p, QStringLiteral("Music"), now) != 0);
        QVERIFY(library->addFile(d, QStringLiteral("Inbox"), now) != 0);
    }
    void start()
    {
        ShellHooks hooks;
        hooks.chooseFolders = [this] {
            ++asked;
            return folders;
        };
        hooks.chooseFiles = [this] {
            ++asked;
            return files;
        };
        hooks.now = [this] { return now; };
        shell = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, hooks);
    }
    void run(const char16_t *id)
    {
        if (QAction *action = actions.action(s(id)))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    qint64 idOf(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->id : 0;
    }
    QStringList shown() const { return sorted(shell->shownPaths()); }
    QStringList navigatorRows() const
    {
        QStringList out;
        auto *tree = window.findChild<QTreeWidget *>(s(u"navigatorTree"));
        if (!tree)
            return { s(u"NO TREE") };
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            out.append((*it)->text(0));
        return out;
    }
};

LibraryTrack crafted(qint64 id, const char16_t *title)
{
    LibraryTrack t;
    t.id = id;
    t.path = QStringLiteral("/music/%1.flac").arg(id);
    t.category = QStringLiteral("Music");
    t.tags.format = QStringLiteral("flac");
    t.tags.fields.insert(QStringLiteral("Title"), { s(title) });
    return t;
}

QList<qint64> rows(const TrackListModel &model)
{
    QList<qint64> out;
    for (int row = 0; row < model.rowCount(); ++row)
        out.append(model.trackId(row));
    return out;
}

}

class LibraryShellCountsTest : public QObject {
    Q_OBJECT
private slots:
    void countsFromTheCore();
    void aChangeReadsOnlyWhatItNames();
};

void LibraryShellCountsTest::countsFromTheCore()
{
    // L-COUNT: how many tracks each section holds, without reading a track.
    // helpers of the rig this file shares with the test it grew from
    (void)&nextRun;
    (void)&crafted;
    (void)&rows;
    Rig r;
    QMap<QString, int> counts = r.library->categoryCounts();
    QCOMPARE(sorted(counts.keys()), sorted(Library::categories())); // every section, the empty ones too
    for (const int n : std::as_const(counts))
        QCOMPARE(n, 0);
    r.fill();
    counts = r.library->categoryCounts();
    QCOMPARE(sorted(counts.keys()), sorted(Library::categories()));
    QCOMPARE(counts.value(s(u"Music")), 3);
    QCOMPARE(counts.value(s(u"Inbox")), 1);
    int sum = 0;
    for (const int n : std::as_const(counts))
        sum += n;
    QCOMPARE(sum, r.library->count());
    const qint64 a = r.idOf(r.a);
    QCOMPARE(r.library->setCategory({ a }, s(u"Inbox")), 1);
    counts = r.library->categoryCounts();
    QCOMPARE(counts.value(s(u"Music")), 2);
    QCOMPARE(counts.value(s(u"Inbox")), 2);
    QCOMPARE(r.library->remove({ a }), 1);
    QCOMPARE(r.library->categoryCounts().value(s(u"Inbox")), 1);

    // L-COUNT: tracksRead() counts every track handed out; counting reads none.
    qint64 read = r.library->tracksRead();
    (void)r.library->categoryCounts();
    (void)r.library->count();
    QCOMPARE(r.library->tracksRead(), read);
    QCOMPARE(r.library->tracks(s(u"Music")).size(), 2);
    QCOMPARE(r.library->tracksRead(), read + 2);
    QCOMPARE(r.library->tracks().size(), 3);
    QCOMPARE(r.library->tracksRead(), read + 5);
    QVERIFY(r.library->trackByPath(r.b).has_value());
    QCOMPARE(r.library->tracksRead(), read + 6);
    QVERIFY(r.library->track(r.idOf(r.b)).has_value()); // idOf reads it by path first
    QCOMPARE(r.library->tracksRead(), read + 8);
    QVERIFY(!r.library->track(987654).has_value());
    QVERIFY(!r.library->trackByPath(s(u"/no/such/file.mp3")).has_value());
    QCOMPARE(r.library->tracksRead(), read + 8);
}

void LibraryShellCountsTest::aChangeReadsOnlyWhatItNames()
{
    // SH-COUNTS: the counts come from categoryCounts(); a change of the
    // library does not make the shell read every track of every section.
    Rig r;
    r.fill();
    QList<qint64> more;
    for (int i = 0; i < 12; ++i) {
        const QString p = r.put("basic.mp3", qPrintable(QStringLiteral("more%1.mp3").arg(i)));
        const qint64 id = r.library->addFile(p, i % 2 ? s(u"Music") : s(u"Inbox"), r.now);
        QVERIFY(id != 0);
        more.append(id);
    }
    r.start();
    NEED_PARTS(r);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(r.shell->nodeTracks().size(), 9);
    const qint64 a = r.idOf(r.a);

    qint64 read = r.library->tracksRead();
    QVERIFY(r.library->recordPlay(a, r.now));
    QVERIFY2(r.library->tracksRead() - read <= 3, qPrintable(QString::number(r.library->tracksRead() - read)));
    QCOMPARE(r.shell->nodeTracks().size(), 9);

    read = r.library->tracksRead();
    QCOMPARE(r.library->setCategory({ more.at(0), more.at(2) }, s(u"Music")), 2);
    QVERIFY2(r.library->tracksRead() - read <= 6, qPrintable(QString::number(r.library->tracksRead() - read)));
    QCOMPARE(r.shell->nodeTracks().size(), 11);

    read = r.library->tracksRead();
    QCOMPARE(r.library->remove({ more.at(1) }), 1);
    QVERIFY2(r.library->tracksRead() - read <= 3, qPrintable(QString::number(r.library->tracksRead() - read)));
    QCOMPARE(r.shell->nodeTracks().size(), 10);

    // A change in another section, with a playlist on show: nothing of the sections is read.
    const QString mix = r.playlists.create(s(u"Mix"));
    QVERIFY(r.playlists.setTracks(mix, { r.b }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + mix));
    read = r.library->tracksRead();
    QVERIFY(r.library->recordPlay(more.at(4), r.now));
    QVERIFY2(r.library->tracksRead() - read <= 4, qPrintable(QString::number(r.library->tracksRead() - read)));
}

QTEST_MAIN(LibraryShellCountsTest)
#include "tst_ui_libraryshell_counts.moc"
