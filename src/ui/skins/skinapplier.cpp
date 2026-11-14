#include "skins/skinapplier.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPalette>
#include <QSettings>

#include "skin/skin.h"

namespace linuxbee::ui {

namespace {

QString storedKey()
{
    return QStringLiteral("appearance/skin");
}

// SK-APPLY, step 3: the colour names that go into the palette, and the role
// each one replaces.
struct PaletteRole {
    QLatin1String name;
    QPalette::ColorRole role;
};

const QList<PaletteRole> &paletteRoles()
{
    static const QList<PaletteRole> roles = {
        { QLatin1String("window"), QPalette::Window },
        { QLatin1String("windowText"), QPalette::WindowText },
        { QLatin1String("base"), QPalette::Base },
        { QLatin1String("alternateBase"), QPalette::AlternateBase },
        { QLatin1String("text"), QPalette::Text },
        { QLatin1String("dimText"), QPalette::PlaceholderText },
        { QLatin1String("highlight"), QPalette::Highlight },
        { QLatin1String("highlightText"), QPalette::HighlightedText },
        { QLatin1String("button"), QPalette::Button },
        { QLatin1String("buttonText"), QPalette::ButtonText },
    };
    return roles;
}

// Reads one skin file. False: `reason` says why it is not a skin.
bool loadSkin(const QString &path, Skin *skin, QString *reason)
{
    const QFileInfo info(path);
    if (info.exists() && !info.isFile()) {
        *reason = SkinApplier::tr("it is not a file");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *reason = file.errorString();
        return false;
    }
    *skin = readSkin(file.readAll());
    if (!skin->ok) {
        *reason = skin->error;
        return false;
    }
    return true;
}

}

struct SkinApplier::Private {
    QApplication *application = nullptr;
    QSettings *settings = nullptr;
    QPalette ownPalette; // linuxBee's own look, as it was when this object was made
    QString file; // the skin applied; empty: none
    QString basis;
    QString error;
};

SkinApplier::SkinApplier(QApplication *application, QSettings *settings, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->application = application;
    d->settings = settings;
    d->ownPalette = application->palette();
    // A stored skin that cannot be applied leaves linuxBee's own look and the
    // stored path as it was: the file may be on a drive that is not there today.
    const QString stored = settings->value(storedKey()).toString();
    if (!stored.isEmpty())
        apply(stored);
}

SkinApplier::~SkinApplier()
{
    delete d;
}

QString SkinApplier::currentFile() const
{
    return d->file;
}

QString SkinApplier::basis() const
{
    return d->basis;
}

QString SkinApplier::lastError() const
{
    return d->error;
}

bool SkinApplier::apply(const QString &file)
{
    // Step 1: read the file. Nothing changes unless it is a skin.
    const QString absolute = QFileInfo(file).absoluteFilePath();
    Skin top;
    QString reason;
    if (!loadSkin(absolute, &top, &reason)) {
        d->error = reason;
        return false;
    }

    // Step 2: a skin laid over another is read with it. The base is a name in
    // the same folder; one that has a folder in it is not looked for.
    Skin skin = top;
    QString basis = QStringLiteral("none");
    if (!top.dependsOn.isEmpty()) {
        basis = QStringLiteral("unreadable");
        if (QFileInfo(top.dependsOn).fileName() == top.dependsOn) {
            Skin base;
            QString notUsed;
            if (loadSkin(QFileInfo(absolute).dir().filePath(top.dependsOn), &base, &notUsed)) {
                skin = layeredSkin(base, top);
                basis = QStringLiteral("read");
            }
        }
    }
    const QMap<QString, SkinColour> colours = skinColours(skin);

    // Steps 3 and 4: always from linuxBee's own palette, never from the one
    // the skin before left.
    QPalette palette = d->ownPalette;
    for (const PaletteRole &entry : paletteRoles()) {
        const auto colour = colours.constFind(QString(entry.name));
        if (colour != colours.cend())
            palette.setColor(entry.role, QColor(colour->r, colour->g, colour->b, colour->a));
    }
    d->application->setPalette(palette);
    d->application->setStyleSheet(skinStyleSheet(colours));

    // Step 5: remembered at once.
    d->file = absolute;
    d->basis = basis;
    d->error.clear();
    d->settings->setValue(storedKey(), absolute);
    d->settings->sync();
    emit changed();
    return true;
}

void SkinApplier::clear()
{
    if (d->file.isEmpty())
        return;
    d->application->setPalette(d->ownPalette);
    d->application->setStyleSheet(QString());
    d->file.clear();
    d->basis.clear();
    d->settings->remove(storedKey());
    d->settings->sync();
    emit changed();
}

}
