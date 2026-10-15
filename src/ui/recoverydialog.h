#pragma once

#include "core/autosave.h"

#include <QDialog>

class QListWidget;

namespace spotlight {

// Shown at startup when autosaves from an interrupted session exist.
class RecoveryDialog : public QDialog {
    Q_OBJECT
public:
    explicit RecoveryDialog(const QVector<RecoveryEntry> &entries, QWidget *parent = nullptr);

    // The entry the user chose to recover, or nullptr.
    const RecoveryEntry *chosen() const;

private:
    QVector<RecoveryEntry> m_entries;
    QListWidget *m_list;
    int m_chosen = -1;
};

} // namespace spotlight
