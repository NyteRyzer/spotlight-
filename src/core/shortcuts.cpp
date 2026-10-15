#include "core/shortcuts.h"

#include <QSettings>

namespace spotlight {

ShortcutManager *ShortcutManager::instance()
{
    static ShortcutManager *s = [] {
        auto *m = new ShortcutManager;
        m->load();
        return m;
    }();
    return s;
}

QKeySequence ShortcutManager::registerShortcut(const ShortcutDef &def)
{
    for (auto &d : m_defs) {
        if (d.id == def.id) {
            d = def;
            return key(def.id);
        }
    }
    m_defs.append(def);
    return key(def.id);
}

QKeySequence ShortcutManager::key(const QString &id) const
{
    if (m_overrides.contains(id))
        return m_overrides.value(id);
    for (const auto &d : m_defs) {
        if (d.id == id)
            return d.defaultKey;
    }
    return {};
}

void ShortcutManager::setKey(const QString &id, const QKeySequence &k)
{
    QKeySequence def;
    for (const auto &d : m_defs) {
        if (d.id == id)
            def = d.defaultKey;
    }
    if (k == def)
        m_overrides.remove(id);
    else
        m_overrides.insert(id, k);
    save();
    emit keyChanged(id, k);
}

void ShortcutManager::resetAll()
{
    const auto ids = m_overrides.keys();
    m_overrides.clear();
    save();
    for (const auto &id : ids)
        emit keyChanged(id, key(id));
}

QStringList ShortcutManager::conflicts(const QString &id, const QKeySequence &k) const
{
    QStringList r;
    if (k.isEmpty())
        return r;
    for (const auto &d : m_defs) {
        if (d.id != id && key(d.id) == k)
            r << d.id;
    }
    return r;
}

void ShortcutManager::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("shortcuts"));
    // Ids use dots, which QSettings would treat specially; they are stored
    // with underscores instead (ids therefore never contain underscores).
    for (const QString &k : s.childKeys()) {
        QString id = k;
        id.replace(QLatin1Char('_'), QLatin1Char('.'));
        m_overrides.insert(id, QKeySequence(s.value(k).toString(), QKeySequence::PortableText));
    }
    s.endGroup();
}

void ShortcutManager::save() const
{
    QSettings s;
    s.remove(QStringLiteral("shortcuts"));
    s.beginGroup(QStringLiteral("shortcuts"));
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it) {
        QString k = it.key();
        s.setValue(k.replace(QLatin1Char('.'), QLatin1Char('_')), it.value().toString(QKeySequence::PortableText));
    }
    s.endGroup();
}

} // namespace spotlight
