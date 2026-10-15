#pragma once

#include <QAction>
#include <QKeySequence>

namespace spotlight {

// Creates an action whose shortcut is registered with ShortcutManager under
// `id`, so the user can rebind it and the change applies immediately.
QAction *makeAction(QObject *parent, const QString &id, const QString &category, const QString &text,
                    const QKeySequence &defaultKey = {});

} // namespace spotlight
