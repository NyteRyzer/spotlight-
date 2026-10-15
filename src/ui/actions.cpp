#include "ui/actions.h"

#include "core/shortcuts.h"

namespace spotlight {

QAction *makeAction(QObject *parent, const QString &id, const QString &category, const QString &text,
                    const QKeySequence &defaultKey)
{
    auto *a = new QAction(text, parent);
    QString description = text;
    description.remove(QLatin1Char('&'));
    description.remove(QStringLiteral("..."));
    auto *sm = ShortcutManager::instance();
    a->setShortcut(sm->registerShortcut({id, category, description, defaultKey}));
    QObject::connect(sm, &ShortcutManager::keyChanged, a, [a, id](const QString &changed, const QKeySequence &key) {
        if (changed == id)
            a->setShortcut(key);
    });
    return a;
}

} // namespace spotlight
