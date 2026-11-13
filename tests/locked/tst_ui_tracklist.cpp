// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/track-list.md (T-MODEL, T-PAGE).
// Runs on the offscreen platform.
#include <QFile>
#include <QAbstractItemModelTester>
#include <QAction>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>

#include "actions/actionregistry.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using linuxbee::ui::ActionRegistry;
using linuxbee::ui::TrackListModel;
using linuxbee::ui::TrackListPage;

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

LibraryTrack track(qint64 id, const char16_t *artist, const char16_t *album, const char16_t *year, int disc, int no,
    const char16_t *title, int seconds, qint64 bytes)
{
    LibraryTrack t;
    t.id = id;
    t.path = QStringLiteral("/music/%1.flac").arg(id);
    t.category = QStringLiteral("Music");
    t.sizeBytes = bytes;
    t.tags.format = QStringLiteral("flac");
    t.tags.audio.durationMs = seconds * 1000;
    auto set = [&t](const char16_t *field, const QString &value) {
        if (!value.isEmpty())
            t.tags.fields.insert(s(field), { value });
    };
    set(u"Artist", s(artist));
    set(u"Album Artist", s(artist));
    set(u"Album", s(album));
    set(u"Year", s(year));
    set(u"Disc#", disc ? QString::number(disc) : QString());
    set(u"Track#", no ? QString::number(no) : QString());
    set(u"Title", s(title));
    return t;
}

const qint64 MB = 1048576;

// Eight tracks, given out of order on purpose.
QList<LibraryTrack> tracks()
{
    return {
        track(1, u"The Beatles", u"Abbey Road", u"1969", 1, 2, u"Something", 183, 20 * MB),
        track(2, u"Bj\u00f6rk", u"Homogenic", u"1997", 1, 10, u"All Is Full of Love", 272, 30 * MB),
        track(3, u"The Beatles", u"Abbey Road", u"1969", 1, 1, u"Come Together", 260, 28 * MB),
        track(4, u"Abba", u"Arrival", u"1976", 1, 2, u"Dancing Queen", 231, 25 * MB),
        track(5, u"Bj\u00f6rk", u"Homogenic", u"1997", 1, 2, u"J\u00f3ga", 305, 33 * MB),
        track(6, u"", u"", u"", 0, 0, u"Untagged", 60, 6 * MB),
        track(7, u"The Beatles", u"Abbey Road", u"1969", 2, 1, u"Here Comes the Sun", 185, 20 * MB),
        track(8, u"Bob Dylan", u"Desire", u"1976", 1, 1, u"Hurricane", 513, 55 * MB),
    };
}

QList<qint64> ids(const TrackListModel &m)
{
    QList<qint64> out;
    for (int r = 0; r < m.rowCount(); ++r)
        out.append(m.trackId(r));
    return out;
}

QString cell(const TrackListModel &m, int row, const char16_t *column)
{
    const int c = int(m.columns().indexOf(s(column)));
    return c < 0 ? QStringLiteral("NO SUCH COLUMN") : m.data(m.index(row, c)).toString();
}

}

class TrackListTest : public QObject {
    Q_OBJECT
private slots:
    void defaults();
    void cells();
    void sorting();
    void headerSort();
    void filter();
    void changes();
    void columns();
    void stored();
    void pageStructure();
    void pageSearch();
    void pageSelection();
    void pageActivation();
    void pageCommands();
};

void TrackListTest::defaults()
{
    // T-MODEL
    TrackListModel m;
    QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(TrackListModel::defaultColumns(),
        QStringList({ s(u"Track#"), s(u"Title"), s(u"Artist"), s(u"Album"), s(u"Year"), s(u"Genre"), s(u"Rating"), s(u"Time") }));
    QCOMPARE(TrackListModel::defaultSort(),
        QStringList({ s(u"Album Artist"), s(u"Album"), s(u"Disc#"), s(u"Track#"), s(u"Title") }));
    QCOMPARE(m.columns(), TrackListModel::defaultColumns());
    QCOMPARE(m.sortFields(), TrackListModel::defaultSort());
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.columnCount(), 8);
    QCOMPARE(m.totalCount(), 0);
    QCOMPARE(m.summary(), s(u"0 tracks, 0.0 MB, 0:00:00"));
    QCOMPARE(m.trackId(0), 0);
    QCOMPARE(m.rowOf(1), -1);
    for (int c = 0; c < 8; ++c)
        QCOMPARE(m.headerData(c, Qt::Horizontal).toString(), TrackListModel::defaultColumns().at(c));
    QVERIFY(!m.headerData(8, Qt::Horizontal).isValid());
    // A flat list: no row has children.
    m.setTracks(tracks());
    QCOMPARE(m.rowCount(m.index(0, 0)), 0);
    QCOMPARE(m.columnCount(m.index(0, 0)), 0);
}

void TrackListTest::cells()
{
    TrackListModel m;
    QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy summary(&m, &TrackListModel::summaryChanged);
    m.setTracks(tracks());
    QCOMPARE(summary.size(), 1);
    QCOMPARE(m.rowCount(), 8);
    QCOMPARE(m.totalCount(), 8);
    // The default order: album artist ("The" ignored), album, disc, track; no album artist last.
    QCOMPARE(ids(m), QList<qint64>({ 4, 3, 1, 7, 5, 2, 8, 6 }));
    QCOMPARE(cell(m, 0, u"Title"), s(u"Dancing Queen"));
    QCOMPARE(cell(m, 0, u"Artist"), s(u"Abba"));
    QCOMPARE(cell(m, 0, u"Album"), s(u"Arrival"));
    QCOMPARE(cell(m, 0, u"Year"), s(u"1976"));
    QCOMPARE(cell(m, 0, u"Time"), s(u"3:51"));
    QCOMPARE(cell(m, 0, u"Genre"), QString()); // no value: an empty cell
    QCOMPARE(cell(m, 5, u"Track#"), s(u"10"));
    QCOMPARE(cell(m, 4, u"Title"), s(u"J\u00f3ga"));
    QCOMPARE(m.trackId(2), 1);
    QCOMPARE(m.rowOf(1), 2);
    QCOMPARE(m.rowOf(99), -1);
    QCOMPARE(m.trackId(-1), 0);
    QCOMPARE(m.trackId(8), 0);
    const QModelIndex first = m.index(0, 1);
    QCOMPARE(m.data(first, TrackListModel::TrackIdRole).toLongLong(), 4);
    QCOMPARE(m.data(first, TrackListModel::MissingRole).toBool(), false);
    QCOMPARE(m.data(first, Qt::ToolTipRole).toString(), s(u"/music/4.flac"));
    QVERIFY(!(m.flags(first) & Qt::ItemIsEditable));
    // Numbers, lengths and sizes line up on the right; text on the left.
    auto alignment = [&m](const char16_t *column) {
        return Qt::Alignment(m.data(m.index(0, int(m.columns().indexOf(s(column)))), Qt::TextAlignmentRole).toInt());
    };
    QVERIFY(alignment(u"Track#") & Qt::AlignRight);
    QVERIFY(alignment(u"Time") & Qt::AlignRight);
    QVERIFY(alignment(u"Title") & Qt::AlignLeft);
    QVERIFY(alignment(u"Year") & Qt::AlignLeft);
    QCOMPARE(m.summary(), s(u"8 tracks, 217.0 MB, 0:33:29"));
    QVERIFY(!m.data(QModelIndex()).isValid());
    QVERIFY(!m.data(m.index(0, 0), Qt::DecorationRole).isValid());
}

void TrackListTest::sorting()
{
    TrackListModel m;
    QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
    m.setTracks(tracks());
    QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
    m.setSortFields({ s(u"Title") });
    QCOMPARE(reset.size(), 1);
    QCOMPARE(m.sortFields(), QStringList({ s(u"Title") }));
    QCOMPARE(ids(m), QList<qint64>({ 2, 3, 4, 7, 8, 5, 1, 6 }));
    m.setSortFields({ s(u"Title") }); // the same again: nothing happens
    QCOMPARE(reset.size(), 1);
    m.setSortFields({ s(u"-Title") });
    QCOMPARE(ids(m), QList<qint64>({ 6, 1, 5, 8, 7, 4, 3, 2 }));
    // Several keys; ties after the last key go by path.
    m.setSortFields({ s(u"-Year"), s(u"Track#") });
    QCOMPARE(ids(m), QList<qint64>({ 5, 2, 8, 4, 3, 7, 1, 6 }));
    // A blank value comes last in both directions.
    m.setSortFields({ s(u"Year") });
    QCOMPARE(ids(m).last(), 6);
    m.setSortFields({ s(u"-Year") });
    QCOMPARE(ids(m).last(), 6);
    // Numbers by value, lengths by length, sizes by size.
    m.setSortFields({ s(u"Track#"), s(u"Title") });
    QCOMPARE(ids(m), QList<qint64>({ 3, 7, 8, 4, 5, 1, 2, 6 }));
    m.setSortFields({ s(u"-Time") });
    QCOMPARE(ids(m), QList<qint64>({ 8, 5, 2, 3, 4, 7, 1, 6 }));
    m.setSortFields({ s(u"Size"), s(u"Title") });
    QCOMPARE(ids(m), QList<qint64>({ 6, 7, 1, 4, 3, 2, 5, 8 }));
    // No keys: by path.
    m.setSortFields({ });
    QCOMPARE(ids(m), QList<qint64>({ 1, 2, 3, 4, 5, 6, 7, 8 }));
    // A field nobody has: every value is blank, so the path decides.
    m.setSortFields({ s(u"No Such Field") });
    QCOMPARE(ids(m), QList<qint64>({ 1, 2, 3, 4, 5, 6, 7, 8 }));
}

void TrackListTest::headerSort()
{
    // T-MODEL: sort(column, order) is what a click on a header does.
    TrackListModel m;
    m.setTracks(tracks());
    const int year = int(m.columns().indexOf(s(u"Year")));
    m.sort(year, Qt::AscendingOrder);
    QCOMPARE(m.sortFields(), QStringList({ s(u"Year"), s(u"Album Artist"), s(u"Album"), s(u"Disc#"), s(u"Track#"), s(u"Title") }));
    QCOMPARE(ids(m), QList<qint64>({ 3, 1, 7, 4, 8, 5, 2, 6 }));
    m.sort(year, Qt::DescendingOrder);
    QCOMPARE(m.sortFields().first(), s(u"-Year"));
    QCOMPARE(ids(m), QList<qint64>({ 5, 2, 4, 8, 3, 1, 7, 6 }));
    // A column that is one of the usual keys is not listed twice.
    m.sort(int(m.columns().indexOf(s(u"Title"))), Qt::AscendingOrder);
    QCOMPARE(m.sortFields(), QStringList({ s(u"Title"), s(u"Album Artist"), s(u"Album"), s(u"Disc#"), s(u"Track#") }));
    const QStringList before = m.sortFields();
    m.sort(99, Qt::AscendingOrder); // no such column: ignored
    m.sort(-1, Qt::AscendingOrder);
    QCOMPARE(m.sortFields(), before);
}

void TrackListTest::filter()
{
    TrackListModel m;
    QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
    m.setTracks(tracks());
    QSignalSpy summary(&m, &TrackListModel::summaryChanged);
    m.setFilter(SearchQuery::parse(s(u"beatles")));
    QCOMPARE(ids(m), QList<qint64>({ 3, 1, 7 }));
    QCOMPARE(m.totalCount(), 8);
    QCOMPARE(m.summary(), s(u"3 tracks, 68.0 MB, 0:10:28"));
    QCOMPARE(summary.size(), 1);
    QCOMPARE(m.rowOf(4), -1); // filtered out
    m.setFilter(SearchQuery::parse(s(u"BEATLES"))); // the same search: nothing happens
    QCOMPARE(summary.size(), 1);
    m.setFilter(SearchQuery::parse(s(u"bjork OR year:1976")));
    QCOMPARE(ids(m), QList<qint64>({ 4, 5, 2, 8 }));
    m.setFilter(SearchQuery::parse(s(u"zzz")));
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.summary(), s(u"0 tracks, 0.0 MB, 0:00:00"));
    // The filter stays while the order and the tracks change.
    m.setFilter(SearchQuery::parse(s(u"year:1976")));
    m.setSortFields({ s(u"-Title") });
    QCOMPARE(ids(m), QList<qint64>({ 8, 4 }));
    m.upsertTracks({ track(9, u"Eagles", u"Hotel California", u"1976", 1, 1, u"Hotel California", 391, 40 * MB) });
    QCOMPARE(ids(m), QList<qint64>({ 8, 9, 4 }));
    m.setFilter(SearchQuery::parse(QString()));
    QCOMPARE(m.rowCount(), 9);
}

void TrackListTest::changes()
{
    TrackListModel m;
    QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
    m.setTracks(tracks());
    QSignalSpy summary(&m, &TrackListModel::summaryChanged);
    // Replace by id: the row moves to where it now belongs.
    LibraryTrack changed = track(6, u"Abba", u"Arrival", u"1976", 1, 1, u"When I Kissed the Teacher", 180, 19 * MB);
    changed.missing = true;
    m.upsertTracks({ changed });
    QCOMPARE(m.totalCount(), 8);
    QCOMPARE(ids(m), QList<qint64>({ 6, 4, 3, 1, 7, 5, 2, 8 }));
    QCOMPARE(cell(m, 0, u"Title"), s(u"When I Kissed the Teacher"));
    QCOMPARE(m.data(m.index(0, 0), TrackListModel::MissingRole).toBool(), true);
    QCOMPARE(m.summary(), s(u"8 tracks, 230.0 MB, 0:35:29"));
    QCOMPARE(summary.size(), 1);
    // Add and replace in one call.
    m.upsertTracks({ track(10, u"Zappa", u"Hot Rats", u"1969", 1, 1, u"Peaches en Regalia", 218, 23 * MB),
        track(4, u"Abba", u"Arrival", u"1976", 1, 2, u"Dancing Queen (Remaster)", 231, 25 * MB) });
    QCOMPARE(m.totalCount(), 9);
    QCOMPARE(ids(m).last(), 10);
    QCOMPARE(cell(m, 1, u"Title"), s(u"Dancing Queen (Remaster)"));
    m.upsertTracks({ }); // nothing: no signal
    QCOMPARE(summary.size(), 2);
    m.removeTracks({ 3, 1, 7, 999 });
    QCOMPARE(ids(m), QList<qint64>({ 6, 4, 5, 2, 8, 10 }));
    QCOMPARE(m.totalCount(), 6);
    QCOMPARE(summary.size(), 3);
    m.removeTracks({ 999 }); // not there: no signal
    QCOMPARE(summary.size(), 3);
    m.setTracks({ });
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.totalCount(), 0);
}

void TrackListTest::columns()
{
    TrackListModel m;
    QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
    m.setTracks(tracks());
    QVERIFY(m.setColumns({ s(u"Title"), s(u"Size"), s(u"Filename"), s(u"Play Count"), s(u"No Such Field") }));
    QCOMPARE(m.columnCount(), 5);
    QCOMPARE(m.headerData(1, Qt::Horizontal).toString(), s(u"Size"));
    QCOMPARE(cell(m, 0, u"Size"), s(u"25.0 MB"));
    QCOMPARE(cell(m, 0, u"Filename"), s(u"4.flac"));
    QCOMPARE(cell(m, 0, u"Play Count"), s(u"0"));
    QCOMPARE(cell(m, 0, u"No Such Field"), QString());
    QVERIFY(Qt::Alignment(m.data(m.index(0, 1), Qt::TextAlignmentRole).toInt()) & Qt::AlignRight);
    QCOMPARE(ids(m), QList<qint64>({ 4, 3, 1, 7, 5, 2, 8, 6 })); // the order does not depend on the columns
    // Refused, changing nothing: no columns, a blank name, a name twice (in any case).
    QVERIFY(!m.setColumns({ }));
    QVERIFY(!m.setColumns({ s(u"Title"), s(u" ") }));
    QVERIFY(!m.setColumns({ s(u"Title"), s(u"Artist"), s(u"title") }));
    QCOMPARE(m.columnCount(), 5);
}

void TrackListTest::stored()
{
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/t.ini"), QSettings::IniFormat);
    {
        TrackListModel m;
        m.setColumns({ s(u"Title"), s(u"Album"), s(u"Time") });
        m.setSortFields({ s(u"-Year"), s(u"Title") });
        m.save(settings, s(u"tracklist/music"));
        TrackListModel other; // a second list keeps its own settings
        other.save(settings, s(u"tracklist/inbox"));
    }
    settings.sync();
    QCOMPARE(settings.value(s(u"tracklist/music/columns")).toStringList(), QStringList({ s(u"Title"), s(u"Album"), s(u"Time") }));
    QCOMPARE(settings.value(s(u"tracklist/music/sort")).toStringList(), QStringList({ s(u"-Year"), s(u"Title") }));
    // Read as the next run reads it.
    settings.setValue(s(u"tracklist/single/columns"), QStringList({ s(u"Album") })); // one entry is stored as plain text
    settings.setValue(s(u"tracklist/single/sort"), QStringList({ s(u"-Title") }));
    settings.setValue(s(u"tracklist/bad/columns"), QStringList({ s(u"Title"), s(u"Title") }));
    settings.setValue(s(u"tracklist/bad/sort"), QStringList({ s(u"Title") }));
    QSettings later(nextRun(settings), QSettings::IniFormat);
    TrackListModel m;
    m.setTracks(tracks());
    m.load(later, s(u"tracklist/music"));
    QCOMPARE(m.columns(), QStringList({ s(u"Title"), s(u"Album"), s(u"Time") }));
    QCOMPARE(m.sortFields(), QStringList({ s(u"-Year"), s(u"Title") }));
    QCOMPARE(ids(m).first(), 2);
    m.load(later, s(u"tracklist/inbox"));
    QCOMPARE(m.columns(), TrackListModel::defaultColumns());
    QCOMPARE(m.sortFields(), TrackListModel::defaultSort());
    // Nothing stored, or columns that cannot be used: the defaults.
    m.setColumns({ s(u"Title") });
    m.load(later, s(u"tracklist/never"));
    QCOMPARE(m.columns(), TrackListModel::defaultColumns());
    QCOMPARE(m.sortFields(), TrackListModel::defaultSort());
    m.load(later, s(u"tracklist/bad"));
    QCOMPARE(m.columns(), TrackListModel::defaultColumns());
    QCOMPARE(m.sortFields(), QStringList({ s(u"Title") }));
    m.load(later, s(u"tracklist/single"));
    QCOMPARE(m.columns(), QStringList({ s(u"Album") }));
    QCOMPARE(m.sortFields(), QStringList({ s(u"-Title") }));
}

void TrackListTest::pageStructure()
{
    // T-PAGE
    ActionRegistry actions;
    TrackListModel m;
    m.setTracks(tracks());
    TrackListPage page(&m, &actions);
    QCOMPARE(page.objectName(), s(u"trackListPage"));
    auto *search = page.findChild<QLineEdit *>(s(u"searchBox"));
    auto *view = page.findChild<QTreeView *>(s(u"trackView"));
    auto *summary = page.findChild<QLabel *>(s(u"summaryLabel"));
    QVERIFY(search && view && summary);
    QCOMPARE(view->model(), &m);
    QVERIFY(!view->rootIsDecorated());
    QVERIFY(view->selectionBehavior() == QAbstractItemView::SelectRows);
    QVERIFY(view->selectionMode() == QAbstractItemView::ExtendedSelection);
    QVERIFY(view->editTriggers() == QAbstractItemView::NoEditTriggers);
    QVERIFY(view->header()->sectionsMovable());
    QVERIFY(view->header()->isSortIndicatorShown());
    QVERIFY(view->uniformRowHeights());
    QVERIFY(!search->placeholderText().isEmpty());
    QCOMPARE(summary->text(), s(u"8 tracks, 217.0 MB, 0:33:29"));
    page.resize(900, 500);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    // Top to bottom: search, list, summary; the list takes the room.
    QVERIFY(search->y() < view->y());
    QVERIFY(view->y() < summary->y());
    QVERIFY(view->height() > 300);
    // The context menu is the registry's "Context" branch.
    auto *menu = page.findChild<QMenu *>(s(u"trackContextMenu"));
    QVERIFY(menu);
    QVERIFY(menu->actions().contains(actions.action(s(u"track.playNow"))));
    QVERIFY(menu->findChild<QMenu *>(s(u"menu.Context/Rating")));
    // A click on a header sorts.
    const int year = int(m.columns().indexOf(s(u"Year")));
    view->header()->setSortIndicator(year, Qt::DescendingOrder);
    QCOMPARE(m.sortFields().first(), s(u"-Year"));
    QCOMPARE(m.trackId(0), 5);
}

void TrackListTest::pageSearch()
{
    ActionRegistry actions;
    TrackListModel m;
    m.setTracks(tracks());
    TrackListPage page(&m, &actions);
    auto *search = page.findChild<QLineEdit *>(s(u"searchBox"));
    auto *summary = page.findChild<QLabel *>(s(u"summaryLabel"));
    QVERIFY(search && summary);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    QTest::keyClicks(search, s(u"beatles"));
    QCOMPARE(m.rowCount(), 3); // as the user types, no Return needed
    QCOMPARE(summary->text(), s(u"3 tracks, 68.0 MB, 0:10:28"));
    QCOMPARE(page.searchText(), s(u"beatles"));
    page.setSearchText(s(u"title:\"dancing queen\""));
    QCOMPARE(search->text(), s(u"title:\"dancing queen\""));
    QCOMPARE(ids(m), QList<qint64>({ 4 }));
    page.setSearchText(QString());
    QCOMPARE(m.rowCount(), 8);
    QCOMPARE(summary->text(), s(u"8 tracks, 217.0 MB, 0:33:29"));
}

void TrackListTest::pageSelection()
{
    ActionRegistry actions;
    TrackListModel m;
    m.setTracks(tracks());
    TrackListPage page(&m, &actions);
    QSignalSpy changed(&page, &TrackListPage::selectionChanged);
    QVERIFY(page.selectedTrackIds().isEmpty());
    page.selectTracks({ 7, 4, 999 });
    QCOMPARE(page.selectedTrackIds(), QList<qint64>({ 4, 7 })); // in the order shown; unknown ids ignored
    QVERIFY(changed.size() >= 1);
    // The selection follows the tracks, not the rows, when the order changes ...
    changed.clear();
    m.setSortFields({ s(u"-Title") });
    QCOMPARE(page.selectedTrackIds(), QList<qint64>({ 7, 4 }));
    QCOMPARE(changed.size(), 0); // the same tracks: nothing to tell
    // ... and when tracks are replaced.
    m.upsertTracks({ track(4, u"Abba", u"Arrival", u"1976", 1, 2, u"Zzz", 231, 25 * MB) });
    QCOMPARE(page.selectedTrackIds(), QList<qint64>({ 4, 7 }));
    QCOMPARE(changed.size(), 0);
    // A selected track that is filtered out or removed leaves the selection, and that is told.
    page.setSearchText(s(u"beatles"));
    QCOMPARE(page.selectedTrackIds(), QList<qint64>({ 7 }));
    QCOMPARE(changed.size(), 1);
    m.removeTracks({ 7 });
    QVERIFY(page.selectedTrackIds().isEmpty());
    QCOMPARE(changed.size(), 2);
    page.selectTracks({ });
    QVERIFY(page.selectedTrackIds().isEmpty());
}

void TrackListTest::pageActivation()
{
    ActionRegistry actions;
    TrackListModel m;
    m.setTracks(tracks());
    TrackListPage page(&m, &actions);
    auto *view = page.findChild<QTreeView *>(s(u"trackView"));
    QVERIFY(view);
    page.resize(900, 500);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    QSignalSpy activated(&page, &TrackListPage::trackActivated);
    const QRect third = view->visualRect(m.index(2, 1));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, { }, third.center());
    QCOMPARE(page.selectedTrackIds(), QList<qint64>({ 1 }));
    QCOMPARE(activated.size(), 0);
    QTest::mouseDClick(view->viewport(), Qt::LeftButton, { }, third.center());
    QCOMPARE(activated.size(), 1);
    QCOMPARE(activated.last().at(0).toLongLong(), 1);
    // Shift+click extends the selection over the rows between.
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ShiftModifier, view->visualRect(m.index(4, 1)).center());
    QCOMPARE(page.selectedTrackIds(), QList<qint64>({ 1, 7, 5 }));
    // Return on the current row.
    view->setFocus();
    view->setCurrentIndex(m.index(0, 0));
    QTest::keyClick(view, Qt::Key_Return);
    QCOMPARE(activated.size(), 2);
    QCOMPARE(activated.last().at(0).toLongLong(), 4);
}

void TrackListTest::pageCommands()
{
    // T-PAGE: the page implements Select All and Find.
    ActionRegistry actions;
    TrackListModel m;
    m.setTracks(tracks());
    {
        TrackListPage page(&m, &actions);
        auto *search = page.findChild<QLineEdit *>(s(u"searchBox"));
        auto *view = page.findChild<QTreeView *>(s(u"trackView"));
        QVERIFY(search && view);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        view->setFocus();
        QVERIFY(actions.isImplemented(s(u"edit.selectAll")));
        QVERIFY(actions.isImplemented(s(u"edit.find")));
        page.setSearchText(s(u"1976"));
        actions.action(s(u"edit.selectAll"))->trigger();
        QCOMPARE(page.selectedTrackIds(), QList<qint64>({ 4, 8 })); // the rows shown, no more
        QVERIFY(!search->hasFocus());
        actions.action(s(u"edit.find"))->trigger();
        QTRY_VERIFY(search->hasFocus());
        QCOMPARE(search->selectedText(), s(u"1976")); // ready to be typed over
    }
    // The page is gone: its commands are free again.
    QVERIFY(!actions.isImplemented(s(u"edit.selectAll")));
    QVERIFY(!actions.action(s(u"edit.find"))->isEnabled());
}

QTEST_MAIN(TrackListTest)
#include "tst_ui_tracklist.moc"
