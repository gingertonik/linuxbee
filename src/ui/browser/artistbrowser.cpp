#include "browser/artistbrowser.h"

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "library/artistindex.h"
#include "search/search.h"

// Contract: docs/spec/artist-browser.md (AB-LIST, AB-CHOICE, AB-FILTER, AB-BYFIELD).
namespace linuxbee::ui {

namespace {

// AB-BYFIELD: the words the list uses for its first row and for the row of
// tracks with no value of the tag.
QString allRowFormat(const QString &field)
{
    if (field == QStringLiteral("Album"))
        return ArtistBrowser::tr("All Albums (%1)");
    if (field == QStringLiteral("Composer"))
        return ArtistBrowser::tr("All Composers (%1)");
    if (field == QStringLiteral("Genre"))
        return ArtistBrowser::tr("All Genres (%1)");
    if (field == QStringLiteral("Grouping"))
        return ArtistBrowser::tr("All Groupings (%1)");
    if (field == QStringLiteral("Year"))
        return ArtistBrowser::tr("All Years (%1)");
    return ArtistBrowser::tr("All Artists (%1)");
}

QString noneName(const QString &field)
{
    if (field == QStringLiteral("Album"))
        return ArtistBrowser::tr("[Unknown Album]");
    if (field == QStringLiteral("Composer"))
        return ArtistBrowser::tr("[Unknown Composer]");
    if (field == QStringLiteral("Genre"))
        return ArtistBrowser::tr("[Unknown Genre]");
    if (field == QStringLiteral("Grouping"))
        return ArtistBrowser::tr("[Unknown Grouping]");
    if (field == QStringLiteral("Year"))
        return ArtistBrowser::tr("[Unknown Year]");
    return ArtistBrowser::tr("[Unknown Artist]");
}

// AB-LIST: the first row, and one row per artist (NAME (T)).
QString allRow(const QString &field, int count)
{
    return allRowFormat(field).arg(count);
}

QString artistRow(const QString &field, const ArtistEntry &entry)
{
    const QString name = entry.name.isEmpty() ? noneName(field) : entry.name;
    return QStringLiteral("%1 (%2)").arg(name, QString::number(entry.tracks));
}

}

struct ArtistBrowser::Private {
    ArtistBrowser *owner = nullptr;
    QComboBox *fieldCombo = nullptr; // browseFieldCombo
    QLineEdit *filter = nullptr; // artistFilterEdit
    QListWidget *list = nullptr; // artistList
    QString field = QStringLiteral("Album Artist");
    QList<LibraryTrack> tracks; // the tracks last given
    QList<ArtistEntry> entries; // the artists of `tracks`, in list order: row N is entries[N - 1]
    bool hasChoice = false; // AB-CHOICE
    QString chosen; // the chosen artist, spelt as the list spells it

    // Rebuilds the rows for `tracks`. A choice whose artist is still held stays;
    // one whose artist is gone ends, and that is a change of choice.
    void fill();
    // AB-FILTER: hides the rows whose artist does not hold the filter text. The
    // first row and the chosen artist's row stay shown.
    void applyFilter();
    // The row the user (or the program, through chooseArtist) made current.
    void rowChosen(int row);
    // Makes `name` the choice, or all artists. choiceChanged is emitted only
    // when the choice becomes a different one.
    void setChoice(bool has, const QString &name);
};

void ArtistBrowser::Private::fill()
{
    entries = artistIndex(tracks, SortOptions { }, field);
    int row = 0;
    bool choiceEnded = false;
    {
        QSignalBlocker quiet(list); // filling the list is not the user choosing
        list->clear();
        list->addItem(allRow(field, int(entries.size())));
        for (const ArtistEntry &entry : entries)
            list->addItem(artistRow(field, entry));
        if (hasChoice) {
            for (int i = 0; i < entries.size(); ++i) {
                if (sameArtist(entries.at(i).name, chosen)) {
                    chosen = entries.at(i).name;
                    row = i + 1;
                    break;
                }
            }
            if (row == 0) {
                hasChoice = false;
                chosen.clear();
                choiceEnded = true;
            }
        }
        list->setCurrentRow(row);
    }
    applyFilter();
    if (choiceEnded)
        emit owner->choiceChanged();
}

void ArtistBrowser::Private::applyFilter()
{
    const QString wanted = foldForSearch(filter->text());
    QSignalBlocker quiet(list); // hiding a row is not a change of the current row
    for (int row = 0; row < list->count(); ++row) {
        bool shown = true;
        if (row > 0) {
            const ArtistEntry &entry = entries.at(row - 1);
            shown = foldForSearch(entry.name).contains(wanted)
                || (hasChoice && sameArtist(entry.name, chosen));
        }
        list->item(row)->setHidden(!shown);
    }
}

void ArtistBrowser::Private::rowChosen(int row)
{
    if (row >= 1 && row <= entries.size())
        setChoice(true, entries.at(row - 1).name);
    else
        setChoice(false, QString());
}

void ArtistBrowser::Private::setChoice(bool has, const QString &name)
{
    const bool changed = has != hasChoice || (has && !sameArtist(name, chosen));
    hasChoice = has;
    chosen = has ? name : QString();
    if (changed)
        emit owner->choiceChanged();
}

ArtistBrowser::ArtistBrowser(QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->owner = this;
    auto *layout = new QVBoxLayout(this);
    d->fieldCombo = new QComboBox(this);
    d->fieldCombo->setObjectName(QStringLiteral("browseFieldCombo"));
    d->fieldCombo->addItems(browseFields());
    layout->addWidget(d->fieldCombo);
    d->filter = new QLineEdit(this);
    d->filter->setObjectName(QStringLiteral("artistFilterEdit"));
    d->filter->setClearButtonEnabled(true);
    d->filter->setPlaceholderText(tr("Filter artists"));
    layout->addWidget(d->filter);
    d->list = new QListWidget(this);
    d->list->setObjectName(QStringLiteral("artistList"));
    layout->addWidget(d->list);
    connect(d->list, &QListWidget::currentRowChanged, this, [this](int row) { d->rowChosen(row); });
    connect(d->filter, &QLineEdit::textChanged, this, [this] { d->applyFilter(); });
    connect(d->fieldCombo, &QComboBox::currentTextChanged, this,
        [this](const QString &text) { setField(text); });
    d->fill();
}

ArtistBrowser::~ArtistBrowser()
{
    delete d;
}

void ArtistBrowser::setTracks(const QList<LibraryTrack> &tracks)
{
    d->tracks = tracks;
    d->fill();
}

QString ArtistBrowser::field() const
{
    return d->field;
}

void ArtistBrowser::setField(const QString &field)
{
    const QString next = browseFields().contains(field) ? field : QStringLiteral("Album Artist");
    if (next == d->field)
        return;
    d->field = next;
    d->setChoice(false, QString()); // the choice ends; choiceChanged says so
    {
        QSignalBlocker blockCombo(d->fieldCombo); // the combo box already shows it
        d->fieldCombo->setCurrentIndex(d->fieldCombo->findText(next));
        QSignalBlocker blockFilter(d->filter);
        d->filter->clear();
    }
    d->fill();
    emit fieldChanged();
}

QStringList ArtistBrowser::artists() const
{
    QStringList names;
    for (const ArtistEntry &entry : d->entries)
        names.append(entry.name);
    return names;
}

bool ArtistBrowser::hasChoice() const
{
    return d->hasChoice;
}

QString ArtistBrowser::currentArtist() const
{
    return d->hasChoice ? d->chosen : QString();
}

void ArtistBrowser::chooseArtist(const QString &name)
{
    int row = 0;
    for (int i = 0; i < d->entries.size(); ++i) {
        if (sameArtist(d->entries.at(i).name, name)) {
            row = i + 1;
            break;
        }
    }
    d->rowChosen(row);
    d->applyFilter(); // the chosen artist's row is shown, whatever the filter says
    QSignalBlocker quiet(d->list);
    d->list->setCurrentRow(row);
}

void ArtistBrowser::chooseAll()
{
    d->rowChosen(0);
    d->applyFilter();
    QSignalBlocker quiet(d->list);
    d->list->setCurrentRow(0);
}

bool ArtistBrowser::keeps(const LibraryTrack &track) const
{
    if (!d->hasChoice)
        return true;
    for (const QString &value : browseValues(track, d->field)) {
        if (sameArtist(value, d->chosen))
            return true;
    }
    return false;
}

}
