#pragma once

#include <QDialog>
#include <QList>

class QAction;
class QLineEdit;
class QListWidget;
class QMenuBar;

namespace spotlight {

// Ctrl+K: type to find any menu command and run it (like VS Code's command
// palette or Photoshop's search). Lists every enabled action in the menu
// bar with its menu path and shortcut.
class CommandPalette : public QDialog {
    Q_OBJECT
public:
    CommandPalette(QMenuBar *menus, QWidget *parent = nullptr);

    // Scores how well `query` matches `text`: 0 = no match. Words match in
    // any order; prefixes and whole-word hits score higher.
    static int matchScore(const QString &query, const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Entry {
        QAction *action;
        QString path; // "Timeline ▸ Split at Playhead"
    };
    void filter(const QString &text);
    void runCurrent();

    QLineEdit *m_search;
    QListWidget *m_list;
    QList<Entry> m_entries;
};

} // namespace spotlight
