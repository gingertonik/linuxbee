#include "navigator/navigatorpanel.h"

#include <QAbstractItemView>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace linuxbee::ui {

namespace {

// The node id of a row that can be chosen. Headings carry no id.
const int kNodeRole = Qt::UserRole;

const QStringList kSections = { QStringLiteral("Music"), QStringLiteral("Inbox"),
    QStringLiteral("Audiobooks"), QStringLiteral("Podcasts"), QStringLiteral("Videos") };

// V-LIBRARY: thousands separated by commas, written here so locales do not change it.
QString withCommas(int value)
{
    const QString digits = QString::number(value);
    QString out;
    const int n = digits.size();
    for (int i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0)
            out.append(QLatin1Char(','));
        out.append(digits.at(i));
    }
    return out;
}

QString sectionLabel(const QString &name, int count)
{
    if (count <= 0)
        return name;
    return name + QStringLiteral(" (") + withCommas(count) + QLatin1Char(')');
}

QTreeWidgetItem *addRow(
    QTreeWidget *tree, QTreeWidgetItem *parent, const QString &text, const QString &nodeId)
{
    QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setText(0, text);
    if (nodeId.isEmpty()) {
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    } else {
        item->setData(0, kNodeRole, nodeId);
    }
    return item;
}

// V-PLAYLISTS: "/Rock//Best of/" becomes "Rock", "Best of"; parts are trimmed and empty parts
// dropped.
QStringList cleanedParts(const QString &name)
{
    QStringList parts;
    for (const QString &part : name.split(QLatin1Char('/'))) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            parts.append(trimmed);
    }
    return parts;
}

// Exact match on the id. An empty id matches nothing, so headings are never found.
QTreeWidgetItem *findNode(QTreeWidget *tree, const QString &nodeId)
{
    if (nodeId.isEmpty())
        return nullptr;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->data(0, kNodeRole).toString() == nodeId)
            return *it;
    }
    return nullptr;
}

// V-MENU: the row id of one tree item, or empty for a row that has none (the Library heading).
// The Playlists heading is "playlists"; a folder under it is "folder:" and its path.
QString rowIdOf(const QTreeWidgetItem *item, const QTreeWidgetItem *playlistsRoot)
{
    if (!item)
        return QString();
    const QString nodeId = item->data(0, kNodeRole).toString();
    if (!nodeId.isEmpty())
        return nodeId;
    if (item == playlistsRoot)
        return QStringLiteral("playlists");
    // The cleaned names from the folder down to this row, joined with '/'.
    QStringList parts;
    const QTreeWidgetItem *up = item;
    while (up && up != playlistsRoot) {
        parts.prepend(up->text(0));
        up = up->parent();
    }
    if (!up)
        return QString(); // not under Playlists
    return QStringLiteral("folder:") + parts.join(QLatin1Char('/'));
}

// V-MENU: a right press on the tree does not choose a row. Qt would choose the row under the
// pointer on that press, so the press is swallowed before the tree sees it.
class RightPressGuard : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // A right double-click arrives as a press, then a double-click event. Swallowing only the
        // press lets QAbstractItemView::mouseDoubleClickEvent make a press of its own and choose
        // the row, so the double-click is swallowed too.
        const QEvent::Type type = event->type();
        if ((type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick)
            && static_cast<QMouseEvent *>(event)->button() == Qt::RightButton)
            return true;
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

struct NavigatorPanel::Private {
    QTreeWidget *tree = nullptr;
    QTreeWidgetItem *playlistsRoot = nullptr;
    QMap<QString, int> counts;
    QString current;
    bool rebuilding = false; // set while setPlaylists rebuilds rows; the tree's signals stay quiet
};

NavigatorPanel::NavigatorPanel(QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->tree = new QTreeWidget(this);
    d->tree->setObjectName(QStringLiteral("navigatorTree"));
    d->tree->setColumnCount(1);
    d->tree->setHeaderHidden(true);
    d->tree->setSelectionMode(QAbstractItemView::SingleSelection);
    d->tree->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // V-MENU: a context menu asked for on a row is reported; the panel shows no menu itself.
    d->tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(d->tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QString rowId = rowAt(pos);
        if (!rowId.isEmpty())
            emit menuRequested(rowId, d->tree->viewport()->mapToGlobal(pos));
    });
    // The guard is owned by the viewport, so it goes when the viewport does.
    d->tree->viewport()->installEventFilter(new RightPressGuard(d->tree->viewport()));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(d->tree);

    // V-LIBRARY: the fixed rows. Every group is open.
    addRow(d->tree, nullptr, QStringLiteral("Playing Tracks"), QStringLiteral("playing"));

    QTreeWidgetItem *library = addRow(d->tree, nullptr, QStringLiteral("Library"), QString());
    for (const QString &name : kSections)
        addRow(d->tree, library, sectionLabel(name, 0), QStringLiteral("library:") + name);
    library->setExpanded(true);

    d->playlistsRoot = addRow(d->tree, nullptr, QStringLiteral("Playlists"), QString());
    d->playlistsRoot->setExpanded(true);

    // V-NODES: the panel opens on the music library.
    d->current = QStringLiteral("library:Music");
    d->tree->setCurrentItem(findNode(d->tree, d->current));

    // Connected after the opening choice, so the opening itself says nothing.
    // The current row is the choice: a row with no id (a heading) is not a choice.
    connect(d->tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current) {
        if (d->rebuilding)
            return;
        const QString nodeId = current ? current->data(0, kNodeRole).toString() : QString();
        if (nodeId.isEmpty() || nodeId == d->current)
            return;
        d->current = nodeId;
        emit nodeSelected(nodeId);
    });
    connect(d->tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const QString nodeId = item ? item->data(0, kNodeRole).toString() : QString();
        if (!nodeId.isEmpty())
            emit nodeActivated(nodeId);
    });
}

NavigatorPanel::~NavigatorPanel()
{
    delete d;
}

void NavigatorPanel::setLibraryCounts(const QMap<QString, int> &counts)
{
    d->counts = counts;
    const QString prefix = QStringLiteral("library:");
    for (QTreeWidgetItemIterator it(d->tree); *it; ++it) {
        const QString nodeId = (*it)->data(0, kNodeRole).toString();
        if (nodeId.startsWith(prefix)) {
            const QString name = nodeId.mid(prefix.size());
            (*it)->setText(0, sectionLabel(name, d->counts.value(name, 0)));
        }
    }
}

void NavigatorPanel::setPlaylists(const QStringList &names)
{
    // Replace the whole list: drop the old rows under Playlists, then build the new ones.
    d->rebuilding = true;
    qDeleteAll(d->playlistsRoot->takeChildren());

    QStringList seenPaths;
    QMap<QString, QTreeWidgetItem *> folders; // folder path ("Rock/Live") to its row
    for (const QString &name : names) {
        const QStringList parts = cleanedParts(name);
        if (parts.isEmpty())
            continue;
        const QString path = parts.join(QLatin1Char('/'));
        if (seenPaths.contains(path))
            continue;
        seenPaths.append(path);

        QTreeWidgetItem *parent = d->playlistsRoot;
        QString folderPath;
        for (int i = 0; i < parts.size() - 1; ++i) {
            folderPath
                = folderPath.isEmpty() ? parts.at(i) : folderPath + QLatin1Char('/') + parts.at(i);
            QTreeWidgetItem *folder = folders.value(folderPath, nullptr);
            if (!folder) {
                folder = addRow(d->tree, parent, parts.at(i), QString());
                folder->setExpanded(true);
                folders.insert(folderPath, folder);
            }
            parent = folder;
        }
        addRow(d->tree, parent, parts.last(), QStringLiteral("playlist:") + path);
    }

    // V-PLAYLISTS: the choice stays when its node still exists, and nothing is said.
    // When the chosen playlist is gone, Music becomes the choice, and that is said once.
    QTreeWidgetItem *chosen = findNode(d->tree, d->current);
    const bool kept = chosen != nullptr;
    if (!kept) {
        d->current = QStringLiteral("library:Music");
        chosen = findNode(d->tree, d->current);
    }
    d->tree->setCurrentItem(chosen);
    d->rebuilding = false;
    if (!kept)
        emit nodeSelected(d->current);
}

QString NavigatorPanel::currentNode() const
{
    return d->current;
}

bool NavigatorPanel::selectNode(const QString &nodeId)
{
    QTreeWidgetItem *item = findNode(d->tree, nodeId);
    if (!item)
        return false;
    // The current-row handler records the choice and says so, once; choosing the same row says
    // nothing.
    d->tree->setCurrentItem(item);
    return true;
}

QString NavigatorPanel::rowAt(const QPoint &viewportPos) const
{
    return rowIdOf(d->tree->itemAt(viewportPos), d->playlistsRoot);
}

QStringList NavigatorPanel::rows() const
{
    QStringList out;
    for (QTreeWidgetItemIterator it(d->tree); *it; ++it) {
        const QString rowId = rowIdOf(*it, d->playlistsRoot);
        if (!rowId.isEmpty())
            out.append(rowId);
    }
    return out;
}

QStringList NavigatorPanel::nodes() const
{
    QStringList out;
    for (QTreeWidgetItemIterator it(d->tree); *it; ++it) {
        const QString nodeId = (*it)->data(0, kNodeRole).toString();
        if (!nodeId.isEmpty())
            out.append(nodeId);
    }
    return out;
}

}
