#pragma once

#include "core/projectdata.h"

#include <QObject>
#include <QUndoStack>

#include <functional>

namespace spotlight {

// The live, observable project shared by all workspaces.
//
// All modifications go through edit(), which snapshots the data before and
// after the change and pushes an undo command. Because ProjectData uses
// implicitly shared Qt containers, snapshots cost little memory.
class Project : public QObject {
    Q_OBJECT
public:
    explicit Project(QObject *parent = nullptr);

    const ProjectData &data() const { return m_data; }

    // Applies fn to a copy of the data; if anything changed, records an undo
    // step named `text`. Returns true if the project changed.
    //
    // Consecutive edits with the same non-empty mergeKey collapse into a single
    // undo step (used for sliders and spin boxes that fire continuously).
    bool edit(const QString &text, const std::function<void(ProjectData &)> &fn,
              const QString &mergeKey = QString());

    // Replaces the whole project (new/open/recover). Clears undo history.
    void reset(const ProjectData &data, const QString &filePath = QString());

    QUndoStack *undoStack() { return &m_undo; }

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path);
    QString displayName() const;

    bool isModified() const { return !m_undo.isClean() || m_forceModified; }
    // True while reset() replaces the whole project (opening a file, New,
    // recovery), as opposed to an edit.
    bool isResetting() const { return m_resetting; }
    // Marks the current state as saved.
    void markSaved();
    // Marks a recovered project as unsaved even though its undo stack is empty.
    void markModified();

    // Incremented on every change; lets autosave detect unsaved work cheaply.
    quint64 revision() const { return m_revision; }

    // Saving over an existing file first copies it to "Backups/" next to
    // the project (the last kBackupCount versions are kept).
    static constexpr int kBackupCount = 10;
    bool save(QString *error);
    bool saveAs(const QString &path, QString *error);
    bool open(const QString &path, QString *error);

signals:
    // Emitted after any change to the data (edit, undo, redo, reset).
    void changed();
    void filePathChanged(const QString &path);
    void modifiedChanged(bool modified);

private:
    friend class SnapshotCommand;
    void setData(const ProjectData &data);

    ProjectData m_data;
    QUndoStack m_undo;
    QString m_filePath;
    quint64 m_revision = 0;
    bool m_forceModified = false;
    bool m_resetting = false;
};

} // namespace spotlight
