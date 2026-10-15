#pragma once

#include <QDialog>

class QTableWidget;

namespace spotlight {

// Lists every registered shortcut and lets the user rebind it.
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget *parent = nullptr);

private:
    void populate();
    QTableWidget *m_table;
};

} // namespace spotlight
