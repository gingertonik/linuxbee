#pragma once
#include <QDialog>
#include <QString>
#include <QStringList>
#include <QWidget>

// The preferences window and what a page of it must offer.
// Contract: docs/spec/preferences.md (PF-x).
namespace linuxbee::ui {

// One page of preferences. Edits are kept by the page until apply().
class PreferencesPage : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual QString title() const = 0;
    virtual void load() = 0; // show what is stored now; forget edits not applied
    virtual QString apply() = 0; // store the edits; empty: done, otherwise why not
    virtual bool isModified() const = 0;

signals:
    void modifiedChanged();
};

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    ~PreferencesDialog() override;

    void addPage(PreferencesPage *page); // the dialog owns it from now on
    QStringList pages() const; // titles, in the order added
    QString currentPage() const;
    bool selectPage(const QString &title);

    bool apply(); // PF-APPLY; false when a page refused
    void reject() override; // PF-CANCEL

signals:
    void applied(); // after every apply that stored something

private:
    struct Private;
    Private *d;
};

}
