#include "preferences/preferencesdialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

// The preferences window: a list of pages at the left, the chosen page
// taking the room, and Save / Apply / Cancel.
// Contract: docs/spec/preferences.md (PF-FRAME, PF-PAGES, PF-APPLY, PF-CANCEL).
namespace linuxbee::ui {

struct PreferencesDialog::Private {
    QListWidget *pageList = nullptr;
    QStackedWidget *pageStack = nullptr;
    QLabel *errorLabel = nullptr;
    QPushButton *saveButton = nullptr;
    QPushButton *applyButton = nullptr;
    QPushButton *cancelButton = nullptr;
    QList<PreferencesPage *> pageOrder; // the pages, in the order added
};

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
    , d(new Private)
{
    setWindowTitle(tr("Preferences"));

    d->pageList = new QListWidget(this);
    d->pageList->setObjectName(QStringLiteral("pageList"));
    d->pageList->setFixedWidth(160);

    d->pageStack = new QStackedWidget(this);
    d->pageStack->setObjectName(QStringLiteral("pageStack"));
    d->pageStack->setMinimumSize(420, 320);

    d->errorLabel = new QLabel(this);
    d->errorLabel->setObjectName(QStringLiteral("errorLabel"));

    d->saveButton = new QPushButton(tr("Save"), this);
    d->saveButton->setObjectName(QStringLiteral("saveButton"));
    d->applyButton = new QPushButton(tr("Apply"), this);
    d->applyButton->setObjectName(QStringLiteral("applyButton"));
    d->applyButton->setEnabled(false); // PF-APPLY: enabled when a page is modified
    d->cancelButton = new QPushButton(tr("Cancel"), this);
    d->cancelButton->setObjectName(QStringLiteral("cancelButton"));

    // Choosing a title in the list shows that page (PF-PAGES).
    connect(d->pageList, &QListWidget::currentRowChanged, d->pageStack,
        &QStackedWidget::setCurrentIndex);

    auto *pagesRow = new QHBoxLayout;
    pagesRow->addWidget(d->pageList);
    pagesRow->addWidget(d->pageStack, 1);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(d->saveButton);
    buttonRow->addWidget(d->applyButton);
    buttonRow->addWidget(d->cancelButton);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(pagesRow, 1);
    mainLayout->addWidget(d->errorLabel);
    mainLayout->addLayout(buttonRow);

    resize(720, 520);
}

PreferencesDialog::~PreferencesDialog()
{
    delete d;
}

void PreferencesDialog::addPage(PreferencesPage *page)
{
    d->pageOrder.append(page);
    d->pageList->addItem(page->title());
    d->pageStack->addWidget(page); // reparents to the stack: the dialog owns it (PF-PAGES)
    if (d->pageOrder.size() == 1) {
        d->pageList->setCurrentRow(0); // the first page added is the one shown
        d->pageStack->setCurrentIndex(0);
    }
    // PF-APPLY: Apply is enabled when at least one page is modified.
    connect(page, &PreferencesPage::modifiedChanged, this, [this] {
        bool anyModified = false;
        for (PreferencesPage *p : d->pageOrder)
            anyModified = anyModified || p->isModified();
        d->applyButton->setEnabled(anyModified);
    });
    page->load();
}

QStringList PreferencesDialog::pages() const
{
    QStringList out;
    for (PreferencesPage *p : d->pageOrder)
        out.append(p->title());
    return out;
}

QString PreferencesDialog::currentPage() const
{
    const int index = d->pageOrder.indexOf(d->pageStack->currentWidget());
    return index < 0 ? QString() : d->pageOrder[index]->title();
}

bool PreferencesDialog::selectPage(const QString &title)
{
    for (PreferencesPage *p : d->pageOrder) {
        if (p->title() == title) {
            d->pageList->setCurrentRow(d->pageOrder.indexOf(p));
            return true;
        }
    }
    return false;
}

bool PreferencesDialog::apply()
{
    return false; // Task 3: PF-APPLY
}

void PreferencesDialog::reject()
{
    QDialog::reject(); // Task 3: PF-CANCEL
}

}
