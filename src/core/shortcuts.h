#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QVector>

namespace spotlight {

struct ShortcutDef {
    QString id;          // stable identifier, e.g. "timeline.split"
    QString category;    // "File", "Edit", "Timeline", ...
    QString description;
    QKeySequence defaultKey;
};

// Central registry of customizable keyboard shortcuts.
//
// Actions register their id and default key; the user can override keys in
// the Keyboard Shortcuts dialog. Overrides persist in QSettings and every
// bound QAction is updated live.
class ShortcutManager : public QObject {
    Q_OBJECT
public:
    static ShortcutManager *instance();

    // Registers a shortcut; returns the effective key (override or default).
    QKeySequence registerShortcut(const ShortcutDef &def);

    QVector<ShortcutDef> definitions() const { return m_defs; }
    QKeySequence key(const QString &id) const;
    void setKey(const QString &id, const QKeySequence &key);
    void resetAll();

    // Ids of other shortcuts already using `key` (for conflict warnings).
    QStringList conflicts(const QString &id, const QKeySequence &key) const;

    void load();
    void save() const;

signals:
    void keyChanged(const QString &id, const QKeySequence &key);

private:
    ShortcutManager() = default;
    QVector<ShortcutDef> m_defs;
    QHash<QString, QKeySequence> m_overrides;
};

} // namespace spotlight
