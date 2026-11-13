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

    // The buttons (PF-APPLY, PF-CANCEL).
    connect(d->applyButton, &QPushButton::clicked, this, &PreferencesDialog::apply);
    connect(d->saveButton, &QPushButton::clicked, this, [this] {
        if (apply())
            accept(); // Save closes when every modified page stored its edits
    });
    connect(d->cancelButton, &QPushButton::clicked, this, &PreferencesDialog::reject);

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
    d->errorLabel->clear(); // the message goes with the next try
    bool stored = false;
    for (PreferencesPage *page : d->pageOrder) {
        if (!page->isModified())
            continue; // pages not modified are left alone
        const QString reason = page->apply();
        if (!reason.isEmpty()) {
            d->errorLabel->setText(page->title() + QStringLiteral(": ") + reason);
            selectPage(page->title()); // the page that refused is shown
            if (stored) // what stored before the refusal counts
                emit applied();
            return false;
        }
        stored = true;
    }
    if (stored)
        emit applied();
    return true;
}

void PreferencesDialog::reject()
{
    // Qt calls this for Escape and the close button too, so Cancel is one path.
    for (PreferencesPage *page : d->pageOrder)
        page->load(); // every page forgets its edits, in the order added
    d->errorLabel->clear();
    QDialog::reject();
}

}
