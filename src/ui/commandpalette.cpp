#include "ui/commandpalette.h"

#include <QAction>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace spotlight {

namespace {

QString plain(QString text)
{
    text.remove(QLatin1Char('&'));
    text.remove(QStringLiteral("..."));
    text.remove(QStringLiteral("…"));
    return text.trimmed();
}

void collect(QMenu *menu, const QString &prefix, QList<QPair<QAction *, QString>> *out)
{
    for (QAction *a : menu->actions()) {
        if (a->isSeparator())
            continue;
        const QString path = prefix + QStringLiteral(" ▸ ") + plain(a->text());
        if (a->menu())
            collect(a->menu(), path, out);
        else
            out->append({a, path});
    }
}

} // namespace

int CommandPalette::matchScore(const QString &query, const QString &text)
{
    const QString t = text.toLower();
    const QStringList words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return 1;
    int score = 0;
    for (const QString &w : words) {
        const int at = t.indexOf(w);
        if (at < 0)
            return 0;
        score += 10;
        if (at == 0 || !t.at(at - 1).isLetterOrNumber())
            score += 5; // starts a word
    }
    return score;
}

CommandPalette::CommandPalette(QMenuBar *menus, QWidget *parent)
    : QDialog(parent)
    , m_search(new QLineEdit(this))
    , m_list(new QListWidget(this))
{
    setWindowTitle(tr("Command Palette"));
    resize(560, 420);
    auto *v = new QVBoxLayout(this);
    m_search->setPlaceholderText(tr("Type a command… (e.g. “split”, “export”, “marker”)"));
    m_search->setClearButtonEnabled(true);
    v->addWidget(m_search);
    v->addWidget(m_list, 1);
    m_search->installEventFilter(this);

    QList<QPair<QAction *, QString>> found;
    for (QAction *top : menus->actions()) {
        if (top->menu())
            collect(top->menu(), plain(top->text()), &found);
    }
    for (const auto &[a, path] : found) {
        if (a->isVisible())
            m_entries.append({a, path});
    }
    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::filter);
    connect(m_list, &QListWidget::itemActivated, this, &CommandPalette::runCurrent);
    filter(QString());
}

void CommandPalette::filter(const QString &text)
{
    QList<QPair<int, int>> ranked; // score, entry
    for (int i = 0; i < m_entries.size(); ++i) {
        const int s = matchScore(text, m_entries[i].path);
        if (s > 0 && m_entries[i].action->isEnabled())
            ranked.append({s, i});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    m_list->clear();
    for (const auto &[score, i] : ranked) {
        const Entry &e = m_entries[i];
        QString label = e.path;
        const QKeySequence key = e.action->shortcut();
        if (!key.isEmpty())
            label += QStringLiteral("    ") + key.toString(QKeySequence::NativeText);
        auto *item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, i);
        if (e.action->isCheckable())
            item->setCheckState(e.action->isChecked() ? Qt::Checked : Qt::Unchecked);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

void CommandPalette::runCurrent()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    QAction *a = m_entries[item->data(Qt::UserRole).toInt()].action;
    accept();
    // Run after the palette has closed so dialogs open on the main window.
    QTimer::singleShot(0, a, [a] { a->trigger(); });
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const int row = m_list->currentRow();
        switch (ke->key()) {
        case Qt::Key_Down:
            m_list->setCurrentRow(std::min(row + 1, m_list->count() - 1));
            return true;
        case Qt::Key_Up:
            m_list->setCurrentRow(std::max(row - 1, 0));
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            runCurrent();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace spotlight
