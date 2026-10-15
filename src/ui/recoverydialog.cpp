#include "ui/recoverydialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace spotlight {

RecoveryDialog::RecoveryDialog(const QVector<RecoveryEntry> &entries, QWidget *parent)
    : QDialog(parent)
    , m_entries(entries)
    , m_list(new QListWidget(this))
{
    setWindowTitle(tr("Recover Unsaved Work"));
    resize(520, 320);
    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel(tr("SPOTLIGHT! didn't close normally last time. These projects had unsaved changes "
                                "that were autosaved:"),
                             this);
    label->setWordWrap(true);
    layout->addWidget(label);
    for (const auto &e : entries) {
        QString text = QStringLiteral("%1 — %2").arg(e.projectName, QLocale().toString(e.savedAt, QLocale::ShortFormat));
        if (!e.originalPath.isEmpty())
            text += QStringLiteral("\n%1").arg(e.originalPath);
        m_list->addItem(text);
    }
    m_list->setCurrentRow(0);
    layout->addWidget(m_list);

    auto *buttons = new QHBoxLayout;
    auto *discard = new QPushButton(tr("Discard All"), this);
    auto *later = new QPushButton(tr("Not Now"), this);
    auto *recover = new QPushButton(tr("Recover"), this);
    recover->setObjectName(QStringLiteral("primary"));
    recover->setDefault(true);
    buttons->addWidget(discard);
    buttons->addStretch();
    buttons->addWidget(later);
    buttons->addWidget(recover);
    layout->addLayout(buttons);

    connect(recover, &QPushButton::clicked, this, [this] {
        m_chosen = m_list->currentRow();
        accept();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, recover, &QPushButton::click);
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    connect(discard, &QPushButton::clicked, this, [this] {
        for (const auto &e : m_entries)
            AutosaveManager::discard(e);
        reject();
    });
}

const RecoveryEntry *RecoveryDialog::chosen() const
{
    return m_chosen >= 0 && m_chosen < m_entries.size() ? &m_entries[m_chosen] : nullptr;
}

} // namespace spotlight
