#include "ui/shortcutsdialog.h"

#include "core/shortcuts.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace spotlight {

ShortcutsDialog::ShortcutsDialog(QWidget *parent)
    : QDialog(parent)
    , m_table(new QTableWidget(this))
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(640, 560);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Click a shortcut and press the new key combination. Changes apply immediately."),
                                 this));
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Category"), tr("Action"), tr("Shortcut")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table);

    auto *buttons = new QHBoxLayout;
    auto *reset = new QPushButton(tr("Reset All to Defaults"), this);
    auto *close = new QPushButton(tr("Close"), this);
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(reset, &QPushButton::clicked, this, [this] {
        ShortcutManager::instance()->resetAll();
        populate();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    populate();
}

void ShortcutsDialog::populate()
{
    auto *sm = ShortcutManager::instance();
    auto defs = sm->definitions();
    std::sort(defs.begin(), defs.end(), [](const ShortcutDef &a, const ShortcutDef &b) {
        return a.category == b.category ? a.description < b.description : a.category < b.category;
    });
    m_table->setRowCount(defs.size());
    for (int i = 0; i < defs.size(); ++i) {
        const ShortcutDef &d = defs[i];
        m_table->setItem(i, 0, new QTableWidgetItem(d.category));
        m_table->setItem(i, 1, new QTableWidgetItem(d.description));
        for (int c = 0; c < 2; ++c)
            m_table->item(i, c)->setFlags(Qt::ItemIsEnabled);
        auto *edit = new QKeySequenceEdit(sm->key(d.id), m_table);
        connect(edit, &QKeySequenceEdit::editingFinished, this, [edit, id = d.id] {
            auto *mgr = ShortcutManager::instance();
            const QKeySequence k = edit->keySequence();
            const QStringList clash = mgr->conflicts(id, k);
            mgr->setKey(id, k);
            if (!clash.isEmpty())
                edit->setToolTip(tr("Also used by: %1").arg(clash.join(QStringLiteral(", "))));
            else
                edit->setToolTip(QString());
        });
        m_table->setCellWidget(i, 2, edit);
    }
    m_table->resizeColumnToContents(0);
    m_table->setColumnWidth(2, 180);
}

} // namespace spotlight
