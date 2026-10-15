#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

namespace spotlight {

class Project;

// Information about an autosave left behind by a session that did not exit
// cleanly (crash, power loss, force quit).
struct RecoveryEntry {
    QString autosavePath;  // the .spotlight autosave file
    QString originalPath;  // where the project was saved by the user, if ever
    QString projectName;
    QDateTime savedAt;
};

// Periodically writes unsaved work to the autosave directory and finds
// recoverable projects at startup.
//
// Layout: <dir>/<project-uuid>.spotlight plus <project-uuid>.json metadata.
// Autosaves are removed when the project is saved or closed normally, so any
// file still present at startup means the previous session was interrupted.
class AutosaveManager : public QObject {
    Q_OBJECT
public:
    explicit AutosaveManager(Project *project, const QString &directory, QObject *parent = nullptr);

    static QString defaultDirectory();

    void setIntervalSeconds(int seconds);
    int intervalSeconds() const;
    void setEnabled(bool enabled);

    // Writes an autosave now if there are unsaved changes since the last one.
    bool autosaveNow();

    // Removes the autosave for the current project (after save / clean close).
    void discardCurrent();

    QVector<RecoveryEntry> findRecoverable() const;
    static void discard(const RecoveryEntry &e);

    QString directory() const { return m_dir; }

signals:
    void autosaved(const QString &path);

private:
    QString autosavePathFor(const QString &projectId) const;

    Project *m_project;
    QString m_dir;
    QTimer m_timer;
    quint64 m_lastSavedRevision = 0;
};

} // namespace spotlight
