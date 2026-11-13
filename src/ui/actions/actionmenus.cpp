#include "actions/actionregistry.h"
#include "actionregistry_p.h"

#include <QMenu>
#include <QMenuBar>
#include <QStringList>

// H-MENU: the menus are the catalogue. docs/spec/main-window.md.
// The commands arrive in catalogue order; a sub-menu is created where its
// first command comes in the list; a separator goes in before a command that
// asks for one, unless it would be the first thing in its menu.
namespace linuxbee::ui {

// The menu for a path ("File/Library"), created with its parents under `root`
// (which stands for rootPath) if it does not exist yet. Object name: "menu." + path.
QMenu *ActionRegistry::branchMenu(QMenu *root, const QString &rootPath, const QString &path)
{
    if (path == rootPath)
        return root; // the anchor: never by name, the caller's menu may carry any name
    QMenu *menu
        = root->findChild<QMenu *>(QLatin1String("menu.") + path, Qt::FindDirectChildrenOnly);
    if (menu)
        return menu;
    const int cut = path.lastIndexOf(QLatin1Char('/'));
    if (cut < 0)
        return nullptr; // a top level always exists before its commands are added
    QMenu *parent = branchMenu(root, rootPath, path.left(cut));
    if (!parent)
        return nullptr;
    menu = new QMenu(parent);
    menu->setObjectName(QLatin1String("menu.") + path);
    menu->setTitle(path.mid(cut + 1));
    return menu;
}

// Fill `root` (a menu that stands for `rootPath`) with the commands of one
// branch of the catalogue, in catalogue order.
void ActionRegistry::fillBranch(const Private *d, QMenu *root, const QString &rootPath)
{
    for (const Private::Entry &entry : d->entries) {
        const QString &path = entry.info.menu;
        if (path != rootPath && !path.startsWith(rootPath + QLatin1Char('/')))
            continue; // not a command of this branch
        QMenu *menu = root;
        if (path != rootPath) {
            menu = branchMenu(root, rootPath, path);
            if (!menu)
                continue;
            if (menu->actions().isEmpty())
                root->addMenu(menu); // the sub-menu joins the parent at its first command
        }
        if (entry.info.separatorBefore && !menu->actions().isEmpty())
            menu->addSeparator();
        menu->addAction(entry.action);
    }
}

void ActionRegistry::populateMenuBar(QMenuBar *menuBar) const
{
    QStringList tops; // top-level menus in catalogue order (W-MENU's order is the data's)
    for (const Private::Entry &entry : d->entries) {
        const QString &path = entry.info.menu;
        const QString top = path.section(QLatin1Char('/'), 0, 0);
        if (path.isEmpty() || top == QLatin1String("Context") || tops.contains(top))
            continue; // hotkey-only and context commands are not in the bar
        tops.append(top);
        QMenu *menu = new QMenu(menuBar);
        menu->setObjectName(QLatin1String("menu.") + top);
        menu->setTitle(top);
        menuBar->addMenu(menu);
    }
    for (const QString &top : tops) {
        QMenu *menu
            = menuBar->findChild<QMenu *>(QLatin1String("menu.") + top, Qt::FindDirectChildrenOnly);
        fillBranch(d, menu, top);
    }
}

void ActionRegistry::populateMenu(QMenu *menu, const QString &menuPath) const
{
    if (!menu || menuPath.isEmpty())
        return;
    fillBranch(d, menu, menuPath);
}

} // namespace linuxbee::ui
