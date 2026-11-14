// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/convert-dialog.md (CD-x). Runs on
// the offscreen platform. Every test but the last gives the dialog's batch a
// converter that only waits; the last converts a plan-08 fixture for real.
#include <QFile>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>
#include <QtTest>

#include <atomic>

#include "convert/convertdialog.h"

using namespace linuxbee;
using linuxbee::ui::ConvertDialog;

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

// The named child, or (failing the test) a stand-in, so that a missing widget
// is a failure and not a crash.
template <typename T> T *need(QWidget &d, const char16_t *name)
{
    if (T *found = d.findChild<T *>(s(name)))
        return found;
    QTest::qFail(qPrintable(QStringLiteral("no widget named ") + s(name)), __FILE__, __LINE__);
    static T standIn;
    return &standIn;
}

const QStringList kFiles = { s(u"/in/a.flac"), s(u"/in/b.flac"), s(u"/in/skip.flac"), s(u"/in/c.wav") };

// A converter that waits instead of converting. The source's name decides the outcome.
struct Stand {
    std::atomic<int> calls { 0 };
    int ms = 12;
    QMutex mutex;
    QList<ConvertJob> seen;
    ConvertBatch::Converter converter()
    {
        return [this](const ConvertJob &job, const ConvertProgress &progress) {
            ++calls;
            {
                QMutexLocker lock(&mutex);
                seen.append(job);
            }
            ConvertResult r;
            for (int step = 1; step <= 2; ++step) {
                QThread::msleep(ms / 2);
                if (!progress(step / 2.0)) {
                    r.status = ConvertResult::Status::Cancelled;
                    return r;
                }
            }
            const QString name = job.source.section(QLatin1Char('/'), -1);
            if (name.startsWith(s(u"skip"))) {
                r.status = ConvertResult::Status::Skipped;
            } else if (name.startsWith(s(u"bad"))) {
                r.status = ConvertResult::Status::Failed;
                r.message = s(u"encoder said no");
            } else if (name.startsWith(s(u"gone"))) {
                r.status = ConvertResult::Status::SourceNotFound;
            } else {
                r.status = ConvertResult::Status::Converted;
                r.outputPath = job.destination;
            }
            return r;
        };
    }
};

struct Fixture {
    QTemporaryDir dir;
    QSettings settings;
    Stand stand;
    Fixture()
        : settings(dir.path() + QStringLiteral("/t.ini"), QSettings::IniFormat)
    {
        qRegisterMetaType<linuxbee::ConvertResult>();
    }
};

QStringList column(QTreeWidget *table, int c)
{
    QStringList out;
    for (int i = 0; i < table->topLevelItemCount(); ++i)
        out.append(table->topLevelItem(i)->text(c));
    return out;
}

}

class ConvertDialogTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void defaults();
    void optionsFollowControls();
    void tableFollowsOptions();
    void run();
    void lockedWhileRunning();
    void failures();
    void stop();
    void remembered();
    void realConversion();
};

void ConvertDialogTest::structure()
{
    Fixture f;
    ConvertDialog d(kFiles, &f.settings);
    QCOMPARE(d.objectName(), s(u"convertDialog"));
    QVERIFY(!d.windowTitle().isEmpty());
    QVERIFY(d.batch());
    for (const char16_t *n : { u"presetCombo", u"ifExistsCombo" })
        QVERIFY2(d.findChild<QComboBox *>(s(n)), qPrintable(s(n)));
    QVERIFY(d.findChild<QLineEdit *>(s(u"folderEdit")));
    QVERIFY(d.findChild<QSpinBox *>(s(u"parallelSpin")));
    for (const char16_t *n : { u"copyTagsCheck", u"copyPicturesCheck" })
        QVERIFY2(d.findChild<QCheckBox *>(s(n)), qPrintable(s(n)));
    for (const char16_t *n : { u"browseButton", u"runButton", u"closeButton" })
        QVERIFY2(d.findChild<QPushButton *>(s(n)), qPrintable(s(n)));
    QVERIFY(d.findChild<QLabel *>(s(u"summaryLabel")));
    QVERIFY(d.findChild<QProgressBar *>(s(u"progressBar")));
    auto *table = d.findChild<QTreeWidget *>(s(u"jobTable"));
    QVERIFY(table);
    QCOMPARE(table->columnCount(), 3);
    QCOMPARE(table->headerItem()->text(0), s(u"From"));
    QCOMPARE(table->headerItem()->text(1), s(u"To"));
    QCOMPARE(table->headerItem()->text(2), s(u"Status"));
    // The presets are the converter's, in its order, by name; each carries its id.
    auto *preset = need<QComboBox>(d, u"presetCombo");
    const QList<EncoderPreset> presets = encoderPresets();
    QCOMPARE(preset->count(), int(presets.size()));
    for (int i = 0; i < presets.size() && i < preset->count(); ++i) {
        QCOMPARE(preset->itemText(i), presets.at(i).name);
        QCOMPARE(preset->itemData(i).toString(), presets.at(i).id);
    }
    auto *ifExists = need<QComboBox>(d, u"ifExistsCombo");
    QStringList ids;
    for (int i = 0; i < ifExists->count(); ++i)
        ids.append(ifExists->itemData(i).toString());
    QCOMPARE(ids, QStringList({ s(u"skip"), s(u"overwrite"), s(u"rename"), s(u"updateTags") }));
    d.resize(800, 600);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QVERIFY(!need<QProgressBar>(d, u"progressBar")->isVisible());
    const int tableY = table->mapTo(&d, QPoint(0, 0)).y();
    QVERIFY(preset->mapTo(&d, QPoint(0, 0)).y() < tableY);
    QVERIFY(need<QPushButton>(d, u"runButton")->mapTo(&d, QPoint(0, 0)).y() > tableY + table->height() - 1);
}

void ConvertDialogTest::defaults()
{
    // CD-OPTIONS
    Fixture f;
    ConvertDialog d(kFiles, &f.settings);
    QCOMPARE(need<QComboBox>(d, u"presetCombo")->currentIndex(), 0);
    QCOMPARE(need<QLineEdit>(d, u"folderEdit")->text(), QString());
    QVERIFY(!need<QLineEdit>(d, u"folderEdit")->placeholderText().isEmpty());
    QCOMPARE(need<QComboBox>(d, u"ifExistsCombo")->currentData().toString(), s(u"skip"));
    QVERIFY(need<QCheckBox>(d, u"copyTagsCheck")->isChecked());
    QVERIFY(need<QCheckBox>(d, u"copyPicturesCheck")->isChecked());
    QCOMPARE(need<QSpinBox>(d, u"parallelSpin")->value(), ConvertBatch::defaultParallel());
    QCOMPARE(need<QSpinBox>(d, u"parallelSpin")->minimum(), 1);
    QCOMPARE(need<QSpinBox>(d, u"parallelSpin")->maximum(), 16);
    const BatchOptions o = d.options();
    const EncoderSettings first = encoderPresets().first().settings;
    QVERIFY(o.settings.format == first.format);
    QCOMPARE(o.settings.bitrateKbps, first.bitrateKbps);
    QCOMPARE(o.outputFolder, QString());
    QVERIFY(o.ifExists == ConvertJob::IfExists::Skip);
    QVERIFY(o.copyTags && o.copyPictures);
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"4 files"));
    QVERIFY(need<QPushButton>(d, u"runButton")->isEnabled());
    ConvertDialog one({ s(u"/in/a.flac") }, &f.settings);
    QCOMPARE(need<QLabel>(one, u"summaryLabel")->text(), s(u"1 file"));
    ConvertDialog none({ }, &f.settings);
    QCOMPARE(need<QLabel>(none, u"summaryLabel")->text(), s(u"0 files"));
    QVERIFY(!need<QPushButton>(none, u"runButton")->isEnabled());
    QVERIFY(!none.run());
}

void ConvertDialogTest::optionsFollowControls()
{
    Fixture f;
    ConvertDialog d(kFiles, &f.settings);
    auto *preset = need<QComboBox>(d, u"presetCombo");
    const int flac = preset->findData(s(u"flac"));
    QVERIFY2(flac >= 0, "the converter has a preset with the id flac");
    preset->setCurrentIndex(flac);
    QVERIFY(d.options().settings.format == AudioFormat::Flac);
    need<QLineEdit>(d, u"folderEdit")->setText(s(u"  /out/here "));
    QCOMPARE(d.options().outputFolder, s(u"/out/here"));
    auto *ifExists = need<QComboBox>(d, u"ifExistsCombo");
    ifExists->setCurrentIndex(ifExists->findData(s(u"overwrite")));
    QVERIFY(d.options().ifExists == ConvertJob::IfExists::Overwrite);
    ifExists->setCurrentIndex(ifExists->findData(s(u"rename")));
    QVERIFY(d.options().ifExists == ConvertJob::IfExists::Rename);
    ifExists->setCurrentIndex(ifExists->findData(s(u"updateTags")));
    QVERIFY(d.options().ifExists == ConvertJob::IfExists::UpdateTags);
    // Without tags there is no artwork to copy: the box greys out and counts as off, keeping its tick.
    need<QCheckBox>(d, u"copyTagsCheck")->setChecked(false);
    QVERIFY(!d.options().copyTags && !d.options().copyPictures);
    QVERIFY(!need<QCheckBox>(d, u"copyPicturesCheck")->isEnabled());
    QVERIFY(need<QCheckBox>(d, u"copyPicturesCheck")->isChecked());
    need<QCheckBox>(d, u"copyTagsCheck")->setChecked(true);
    QVERIFY(need<QCheckBox>(d, u"copyPicturesCheck")->isEnabled());
    QVERIFY(d.options().copyPictures);
    need<QCheckBox>(d, u"copyPicturesCheck")->setChecked(false);
    QVERIFY(d.options().copyTags && !d.options().copyPictures);
}

void ConvertDialogTest::tableFollowsOptions()
{
    // CD-TABLE: the table is planBatch of the files and the options, always.
    Fixture f;
    ConvertDialog d(kFiles, &f.settings);
    auto *table = need<QTreeWidget>(d, u"jobTable");
    QCOMPARE(column(table, 0), kFiles);
    const QString ext = fileExtension(encoderPresets().first().settings.format);
    QCOMPARE(column(table, 1).first(), s(u"/in/a.") + ext);
    QCOMPARE(column(table, 2), QStringList({ s(u""), s(u""), s(u""), s(u"") })); // nothing has happened yet
    need<QLineEdit>(d, u"folderEdit")->setText(s(u"/out"));
    auto *preset = need<QComboBox>(d, u"presetCombo");
    preset->setCurrentIndex(preset->findData(s(u"flac")));
    QCOMPARE(column(table, 1), QStringList({ s(u"/out/a.flac"), s(u"/out/b.flac"), s(u"/out/skip.flac"), s(u"/out/c.flac") }));
    QCOMPARE(d.plan().size(), 4);
    QCOMPARE(d.plan().at(3).destination, s(u"/out/c.flac"));
    QVERIFY(d.plan().at(0).settings.format == AudioFormat::Flac);
    // Same format, same folder: planBatch keeps the result off the source.
    need<QLineEdit>(d, u"folderEdit")->clear();
    QCOMPARE(column(table, 1).first(), s(u"/in/a_1.flac"));
}

void ConvertDialogTest::run()
{
    // CD-RUN
    Fixture f;
    f.settings.setValue(s(u"convert/folder"), f.dir.path() + s(u"/out"));
    f.settings.setValue(s(u"convert/preset"), s(u"mp3-320"));
    ConvertDialog d(kFiles, &f.settings);
    d.batch()->setConverter(f.stand.converter());
    need<QSpinBox>(d, u"parallelSpin")->setValue(2);
    QSignalSpy converted(&d, &ConvertDialog::converted);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QTest::mouseClick(need<QPushButton>(d, u"runButton"), Qt::LeftButton);
    QVERIFY(d.batch()->isRunning());
    QCOMPARE(d.batch()->maxParallel(), 2);
    QCOMPARE(d.batch()->jobCount(), 4);
    QVERIFY(QDir(f.dir.path() + s(u"/out")).exists()); // the folder is made before the first job
    QVERIFY(converted.wait(5000));
    QCOMPARE(f.stand.calls.load(), 4);
    // The jobs run are the jobs shown.
    QStringList ran;
    for (const ConvertJob &j : std::as_const(f.stand.seen))
        ran.append(j.destination);
    ran.sort();
    QStringList shown = { f.dir.path() + s(u"/out/a.mp3"), f.dir.path() + s(u"/out/b.mp3"), f.dir.path() + s(u"/out/c.mp3"),
        f.dir.path() + s(u"/out/skip.mp3") };
    QCOMPARE(ran, shown);
    QCOMPARE(f.stand.seen.first().settings.bitrateKbps, 320);
    // converted() names the results, in table order; a skipped file is not one.
    QCOMPARE(converted.size(), 1);
    QCOMPARE(converted.at(0).at(0).toStringList(),
        QStringList({ f.dir.path() + s(u"/out/a.mp3"), f.dir.path() + s(u"/out/b.mp3"), f.dir.path() + s(u"/out/c.mp3") }));
    QCOMPARE(column(need<QTreeWidget>(d, u"jobTable"), 2),
        QStringList({ s(u"Converted"), s(u"Converted"), s(u"Skipped: already there"), s(u"Converted") }));
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"3 converted, 1 skipped, 0 failed"));
    // Nothing failed: the dialog closes.
    QVERIFY(!d.isVisible());
    QCOMPARE(d.result(), int(QDialog::Accepted));
}

void ConvertDialogTest::lockedWhileRunning()
{
    Fixture f;
    f.stand.ms = 80;
    ConvertDialog d(kFiles, &f.settings);
    d.batch()->setConverter(f.stand.converter());
    need<QSpinBox>(d, u"parallelSpin")->setValue(1);
    QSignalSpy converted(&d, &ConvertDialog::converted);
    QSignalSpy firstDone(d.batch(), &ConvertBatch::jobFinished);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QVERIFY(d.run());
    QVERIFY(!d.run()); // one run at a time
    auto *bar = need<QProgressBar>(d, u"progressBar");
    auto *table = need<QTreeWidget>(d, u"jobTable");
    QVERIFY(bar->isVisible());
    QCOMPARE(bar->maximum(), 4);
    QCOMPARE(bar->value(), 0);
    // The options cannot be changed under a running batch.
    for (const char16_t *n : { u"presetCombo", u"folderEdit", u"ifExistsCombo", u"copyTagsCheck", u"copyPicturesCheck", u"parallelSpin", u"runButton" })
        QVERIFY2(!need<QWidget>(d, n)->isEnabled(), qPrintable(s(n)));
    QVERIFY(need<QPushButton>(d, u"closeButton")->isEnabled());
    // Every row says where it stands: waiting, a percentage, or its result.
    QCOMPARE(column(table, 2).last(), s(u"Waiting"));
    QVERIFY(firstDone.wait(5000));
    QCOMPARE(column(table, 2).first(), s(u"Converted"));
    QCOMPARE(bar->value(), 1);
    QTRY_VERIFY(column(table, 2).at(1).endsWith(QLatin1Char('%')));
    QCOMPARE(column(table, 2).last(), s(u"Waiting"));
    QVERIFY(converted.wait(5000));
    QVERIFY(!bar->isVisible());
}

void ConvertDialogTest::failures()
{
    Fixture f;
    ConvertDialog d({ s(u"/in/a.flac"), s(u"/in/bad.flac"), s(u"/in/gone.flac") }, &f.settings);
    d.batch()->setConverter(f.stand.converter());
    QSignalSpy converted(&d, &ConvertDialog::converted);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QVERIFY(d.run());
    QVERIFY(converted.wait(5000));
    // Something failed: the dialog stays and says what.
    QVERIFY(d.isVisible());
    QCOMPARE(column(need<QTreeWidget>(d, u"jobTable"), 2),
        QStringList({ s(u"Converted"), s(u"Failed: encoder said no"), s(u"File not found") }));
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"1 converted, 0 skipped, 2 failed"));
    QCOMPARE(converted.at(0).at(0).toStringList().size(), 1);
    // The controls are back, and the run can be tried again.
    QVERIFY(need<QComboBox>(d, u"presetCombo")->isEnabled());
    QVERIFY(need<QPushButton>(d, u"runButton")->isEnabled());
    QVERIFY(d.run());
    QVERIFY(converted.wait(5000));
    QCOMPARE(f.stand.calls.load(), 6);
    QCOMPARE(converted.size(), 2);
}

void ConvertDialogTest::stop()
{
    // CD-RUN: while it runs, Close (and Escape) ask the run to stop.
    Fixture f;
    f.stand.ms = 60;
    QStringList many;
    for (int i = 0; i < 12; ++i)
        many.append(QStringLiteral("/in/%1.flac").arg(i));
    ConvertDialog d(many, &f.settings);
    d.batch()->setConverter(f.stand.converter());
    need<QSpinBox>(d, u"parallelSpin")->setValue(2);
    QSignalSpy converted(&d, &ConvertDialog::converted);
    QSignalSpy started(d.batch(), &ConvertBatch::jobStarted);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QVERIFY(d.run());
    QTRY_VERIFY(started.size() >= 1);
    QTest::mouseClick(need<QPushButton>(d, u"closeButton"), Qt::LeftButton);
    QVERIFY(d.isVisible()); // not closed: stopping
    QVERIFY(converted.wait(5000));
    QVERIFY(d.isVisible());
    QVERIFY(f.stand.calls.load() < 12);
    const QStringList status = column(need<QTreeWidget>(d, u"jobTable"), 2);
    QCOMPARE(status.last(), s(u"Cancelled"));
    const int cancelled = int(status.count(s(u"Cancelled")));
    const int done = int(status.count(s(u"Converted")));
    QCOMPARE(cancelled + done, 12);
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), QStringLiteral("%1 converted, 0 skipped, 0 failed, %2 cancelled").arg(done).arg(cancelled));
    // Not running any more: Close closes. So does Escape, the same way.
    QTest::keyClick(&d, Qt::Key_Escape);
    QVERIFY(!d.isVisible());
    QCOMPARE(d.result(), int(QDialog::Rejected));
}

void ConvertDialogTest::remembered()
{
    // CD-OPTIONS: stored when a run starts, and at no other time.
    Fixture f;
    {
        ConvertDialog d(kFiles, &f.settings);
        need<QLineEdit>(d, u"folderEdit")->setText(s(u"/only/looked"));
        d.reject();
    }
    QVERIFY(!f.settings.contains(s(u"convert/folder")));
    const QString out = f.dir.path() + s(u"/kept");
    {
        ConvertDialog d(kFiles, &f.settings);
        d.batch()->setConverter(f.stand.converter());
        auto *preset = need<QComboBox>(d, u"presetCombo");
        preset->setCurrentIndex(preset->findData(s(u"flac")));
        need<QLineEdit>(d, u"folderEdit")->setText(out);
        auto *ifExists = need<QComboBox>(d, u"ifExistsCombo");
        ifExists->setCurrentIndex(ifExists->findData(s(u"rename")));
        need<QCheckBox>(d, u"copyPicturesCheck")->setChecked(false);
        need<QSpinBox>(d, u"parallelSpin")->setValue(3);
        QSignalSpy converted(&d, &ConvertDialog::converted);
        QVERIFY(d.run());
        QVERIFY(converted.wait(5000));
    }
    f.settings.sync();
    QCOMPARE(f.settings.value(s(u"convert/preset")).toString(), s(u"flac"));
    QCOMPARE(f.settings.value(s(u"convert/ifExists")).toString(), s(u"rename"));
    QSettings later(nextRun(f.settings), QSettings::IniFormat); // as the next run reads it
    ConvertDialog d(kFiles, &later);
    QCOMPARE(need<QComboBox>(d, u"presetCombo")->currentData().toString(), s(u"flac"));
    QCOMPARE(need<QLineEdit>(d, u"folderEdit")->text(), out);
    QCOMPARE(need<QComboBox>(d, u"ifExistsCombo")->currentData().toString(), s(u"rename"));
    QVERIFY(need<QCheckBox>(d, u"copyTagsCheck")->isChecked());
    QVERIFY(!need<QCheckBox>(d, u"copyPicturesCheck")->isChecked());
    QCOMPARE(need<QSpinBox>(d, u"parallelSpin")->value(), 3);
    // A stored preset that no longer exists, and a nonsense number, fall back to the defaults.
    f.settings.setValue(s(u"convert/preset"), s(u"no-such-preset"));
    f.settings.setValue(s(u"convert/parallel"), 500);
    f.settings.setValue(s(u"convert/ifExists"), s(u"explode"));
    QSettings strange(nextRun(f.settings), QSettings::IniFormat);
    ConvertDialog odd(kFiles, &strange);
    QCOMPARE(need<QComboBox>(odd, u"presetCombo")->currentIndex(), 0);
    QCOMPARE(need<QSpinBox>(odd, u"parallelSpin")->value(), 16);
    QCOMPARE(need<QComboBox>(odd, u"ifExistsCombo")->currentData().toString(), s(u"skip"));
}

void ConvertDialogTest::realConversion()
{
    // With the batch's converter left alone, the dialog converts.
    Fixture f;
    const QString out = f.dir.path() + s(u"/real");
    ConvertDialog d({ QStringLiteral(LINUXBEE_TEST_DATA "/convert/tone.flac") }, &f.settings);
    auto *preset = need<QComboBox>(d, u"presetCombo");
    preset->setCurrentIndex(preset->findData(s(u"mp3-320")));
    need<QLineEdit>(d, u"folderEdit")->setText(out);
    QSignalSpy converted(&d, &ConvertDialog::converted);
    QVERIFY(d.run());
    QVERIFY(converted.wait(60000));
    QCOMPARE(converted.at(0).at(0).toStringList(), QStringList({ out + s(u"/tone.mp3") }));
    QVERIFY(QFileInfo(out + s(u"/tone.mp3")).size() > 1000);
    QCOMPARE(d.result(), int(QDialog::Accepted));
}

QTEST_MAIN(ConvertDialogTest)
#include "tst_ui_convert.moc"
