#include "catalogue.h"

#include "tagreader.h"

namespace linuxbee::tags {

const QList<FieldKeys> &fieldTable()
{
    static const QList<FieldKeys> table = {
        { QStringLiteral("Title"), { QStringLiteral("TITLE") } },
        { QStringLiteral("Artist"), { QStringLiteral("ARTIST") } },
        { QStringLiteral("Album"), { QStringLiteral("ALBUM") } },
        { QStringLiteral("Album Artist"),
            { QStringLiteral("ALBUMARTIST"), QStringLiteral("ALBUM ARTIST") } },
        { QStringLiteral("Year"), { QStringLiteral("DATE"), QStringLiteral("YEAR") } },
        { QStringLiteral("Track#"), { QStringLiteral("TRACKNUMBER") } },
        { QStringLiteral("Track Count"),
            { QStringLiteral("TRACKTOTAL"), QStringLiteral("TOTALTRACKS") } },
        { QStringLiteral("Disc#"), { QStringLiteral("DISCNUMBER") } },
        { QStringLiteral("Disc Count"),
            { QStringLiteral("DISCTOTAL"), QStringLiteral("TOTALDISCS") } },
        { QStringLiteral("Genre"), { QStringLiteral("GENRE") } },
        { QStringLiteral("Composer"), { QStringLiteral("COMPOSER") } },
        { QStringLiteral("Conductor"), { QStringLiteral("CONDUCTOR") } },
        { QStringLiteral("Publisher"),
            { QStringLiteral("LABEL"), QStringLiteral("ORGANIZATION"),
                QStringLiteral("PUBLISHER") } },
        { QStringLiteral("Grouping"),
            { QStringLiteral("GROUPING"), QStringLiteral("CONTENTGROUP") } },
        { QStringLiteral("Comment"), { QStringLiteral("COMMENT") } },
        { QStringLiteral("Rating"), { } },
        { QStringLiteral("BPM"), { QStringLiteral("BPM") } },
        { QStringLiteral("Mood"), { QStringLiteral("MOOD") } },
        { QStringLiteral("Lyricist"), { QStringLiteral("LYRICIST") } },
        { QStringLiteral("Lyrics"),
            { QStringLiteral("LYRICS"), QStringLiteral("UNSYNCEDLYRICS") } },
        { QStringLiteral("Compilation"), { QStringLiteral("COMPILATION") } },
        { QStringLiteral("Copyright"), { QStringLiteral("COPYRIGHT") } },
        { QStringLiteral("ISRC"), { QStringLiteral("ISRC") } },
        { QStringLiteral("Sort Title"), { QStringLiteral("TITLESORT") } },
        { QStringLiteral("Sort Artist"), { QStringLiteral("ARTISTSORT") } },
        { QStringLiteral("Sort Album"), { QStringLiteral("ALBUMSORT") } },
        { QStringLiteral("Sort Album Artist"), { QStringLiteral("ALBUMARTISTSORT") } },
        { QStringLiteral("Sort Composer"), { QStringLiteral("COMPOSERSORT") } },
        { QStringLiteral("Original Year"),
            { QStringLiteral("ORIGINALDATE"), QStringLiteral("ORIGYEAR") } },
        { QStringLiteral("Original Artist"),
            { QStringLiteral("ORIGINALARTIST"), QStringLiteral("ORIGARTIST") } },
        { QStringLiteral("Original Album"),
            { QStringLiteral("ORIGINALALBUM"), QStringLiteral("ORIGALBUM") } },
        { QStringLiteral("Subtitle"), { QStringLiteral("SUBTITLE") } },
        { QStringLiteral("Language"), { QStringLiteral("LANGUAGE") } },
        { QStringLiteral("Initial Key"), { QStringLiteral("INITIALKEY") } },
        { QStringLiteral("Encoded By"), { QStringLiteral("ENCODEDBY") } },
        { QStringLiteral("Mix Artist"), { QStringLiteral("REMIXER") } },
    };
    return table;
}

}

namespace linuxbee {

QStringList tagFieldNames()
{
    QStringList names;
    for (const tags::FieldKeys &entry : tags::fieldTable())
        names.append(entry.field);
    return names;
}

}
