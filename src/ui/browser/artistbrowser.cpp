#include "browser/artistbrowser.h"

#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "library/artistindex.h"

// Contract: docs/spec/artist-browser.md (AB-LIST, AB-CHOICE, AB-FILTER, AB-BYFIELD).
namespace linuxbee::ui {

namespace {

// AB-LIST: the first row, and one row per artist.
QString allRow(int artistCount)
{
    return ArtistBrowser::tr("All Artists (%1)").arg(artistCount);
}

QString artistRow(const ArtistEntry &entry)
{
    const QString name = entry.name.isEmpty() ? ArtistBrowser::tr("[Unknown Artist]") : entry.name;
    return QStringLiteral("%1 (%2)").arg(name, QString::number(entry.tracks));
}

}

struct ArtistBrowser::Private {
    ArtistBrowser *owner = nullptr;
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
        list->addItem(allRow(int(entries.size())));
        for (const ArtistEntry &entry : entries)
            list->addItem(artistRow(entry));
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
    if (choiceEnded)
        emit owner->choiceChanged();
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
    d->filter = new QLineEdit(this);
    d->filter->setObjectName(QStringLiteral("artistFilterEdit"));
    d->filter->setClearButtonEnabled(true);
    d->filter->setPlaceholderText(tr("Filter artists"));
    layout->addWidget(d->filter);
    d->list = new QListWidget(this);
    d->list->setObjectName(QStringLiteral("artistList"));
    layout->addWidget(d->list);
    connect(d->list, &QListWidget::currentRowChanged, this, [this](int row) { d->rowChosen(row); });
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
    return { };
}
void ArtistBrowser::setField(const QString &field)
{
    Q_UNUSED(field);
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
    QSignalBlocker quiet(d->list);
    d->list->setCurrentRow(row);
}

void ArtistBrowser::chooseAll()
{
    d->rowChosen(0);
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
