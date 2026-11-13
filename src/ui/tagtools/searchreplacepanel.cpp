#include "tagtools/searchreplacepanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSettings>
#include <QVBoxLayout>

namespace linuxbee::ui {

struct SearchReplacePanel::Private {
    QLineEdit *find = nullptr;
    QLineEdit *replace = nullptr;
    QComboBox *field = nullptr;
    QCheckBox *regex = nullptr;
    QCheckBox *matchCase = nullptr;
    QCheckBox *wholeWord = nullptr;
};

namespace {

// TT-REPLACE: the catalogue fields the tool offers, in catalogue order, without Rating.
QStringList offeredFields()
{
    QStringList names = tagFieldNames();
    names.removeAll(QStringLiteral("Rating"));
    return names;
}

}

SearchReplacePanel::SearchReplacePanel(QWidget *parent)
    : ToolPanel(parent)
    , d(new Private)
{
    d->find = new QLineEdit(this);
    d->find->setObjectName(QStringLiteral("findEdit"));
    d->replace = new QLineEdit(this);
    d->replace->setObjectName(QStringLiteral("replaceEdit"));

    d->field = new QComboBox(this);
    d->field->setObjectName(QStringLiteral("fieldCombo"));
    d->field->addItem(tr("All fields"));
    d->field->addItems(offeredFields());

    d->regex = new QCheckBox(tr("Regular expression"), this);
    d->regex->setObjectName(QStringLiteral("regexBox"));
    d->matchCase = new QCheckBox(tr("Match case"), this);
    d->matchCase->setObjectName(QStringLiteral("matchCaseBox"));
    d->wholeWord = new QCheckBox(tr("Whole word"), this);
    d->wholeWord->setObjectName(QStringLiteral("wholeWordBox"));

    auto *form = new QFormLayout;
    form->addRow(tr("Find"), d->find);
    form->addRow(tr("Replace with"), d->replace);
    form->addRow(tr("Field"), d->field);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(d->regex);
    layout->addWidget(d->matchCase);
    layout->addWidget(d->wholeWord);

    auto changed = [this] { emit optionsChanged(); };
    connect(d->find, &QLineEdit::textChanged, this, changed);
    connect(d->replace, &QLineEdit::textChanged, this, changed);
    connect(d->field, &QComboBox::currentIndexChanged, this, changed);
    connect(d->regex, &QCheckBox::toggled, this, changed);
    connect(d->matchCase, &QCheckBox::toggled, this, changed);
    connect(d->wholeWord, &QCheckBox::toggled, this, changed);
}

SearchReplacePanel::~SearchReplacePanel()
{
    delete d;
}

QString SearchReplacePanel::title() const
{
    return tr("Search and Replace");
}

QString SearchReplacePanel::problem() const
{
    return validateSearchReplace(options());
}

QList<TagChanges> SearchReplacePanel::changes(const QList<EditTarget> &targets) const
{
    if (!problem().isEmpty())
        return QList<TagChanges>(targets.size());
    const SearchReplaceOptions o = options();
    QList<TagChanges> out;
    for (const EditTarget &target : targets)
        out.append(searchReplace(target.tags, o));
    return out;
}

void SearchReplacePanel::load(QSettings &settings)
{
    // TT-REPLACE: a stored field that is not offered means All fields. The two
    // texts are not remembered.
    const QString field = settings.value(QStringLiteral("tagtools/replace/field")).toString();
    const int index = field.isEmpty() ? -1 : d->field->findText(field);
    d->field->setCurrentIndex(index > 0 ? index : 0);
    d->regex->setChecked(settings.value(QStringLiteral("tagtools/replace/regex")).toBool());
    d->matchCase->setChecked(settings.value(QStringLiteral("tagtools/replace/matchCase")).toBool());
    d->wholeWord->setChecked(settings.value(QStringLiteral("tagtools/replace/wholeWord")).toBool());
}

void SearchReplacePanel::save(QSettings &settings) const
{
    const bool allFields = d->field->currentIndex() <= 0;
    settings.setValue(
        QStringLiteral("tagtools/replace/field"), allFields ? QString() : d->field->currentText());
    settings.setValue(QStringLiteral("tagtools/replace/regex"), d->regex->isChecked());
    settings.setValue(QStringLiteral("tagtools/replace/matchCase"), d->matchCase->isChecked());
    settings.setValue(QStringLiteral("tagtools/replace/wholeWord"), d->wholeWord->isChecked());
}

SearchReplaceOptions SearchReplacePanel::options() const
{
    SearchReplaceOptions o;
    o.find = d->find->text();
    o.replace = d->replace->text();
    if (d->field->currentIndex() > 0)
        o.fields = QStringList { d->field->currentText() };
    o.regex = d->regex->isChecked();
    o.matchCase = d->matchCase->isChecked();
    o.wholeWord = d->wholeWord->isChecked();
    return o;
}

}
